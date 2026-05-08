'use strict';

// =============================================================================
// ghost-base.js — Clase base para todos los ghosts de MSXon
//
// Encapsula el plumbing común que estaba copy-pasted en las 5 funciones del
// ghost-service.js original: socket, parser FM, ping, reconexión con backoff
// exponencial, cleanup de timers, manejo de SIGTERM.
//
// Uso: subclase implementa hooks (onAuthOk, onRoomInfo, onPacket, onTick,
// onShutdown). El plumbing se gestiona en la base.
// =============================================================================

const net = require('net');

const MAGIC_0 = 0x46; // 'F'
const MAGIC_1 = 0x4D; // 'M'

const CMD = {
    PING:           0x01,
    AUTH:           0x10,
    AUTH_OK:        0x11,
    ROOM_CREATE:    0x20,
    ROOM_JOIN:      0x21,
    ROOM_LEAVE:     0x22,
    ROOM_INFO:      0x23,
    ROOM_FULL:      0x24,
    ROOM_NOT_FOUND: 0x25,
    PLAYER_JOINED:  0x30,
    PLAYER_LEFT:    0x31,
    GAME_START:     0x32,
    STATE_UPDATE:   0x40,
};

const SERVER_IP        = process.env.MSX_SERVER_IP   || '127.0.0.1';
const SERVER_PORT      = parseInt(process.env.MSX_SERVER_PORT || '9876', 10);
const AUTH_TOKEN       = Buffer.from(process.env.MSX_AUTH_TOKEN || 'DEADBEEF', 'hex');

const MAX_RECV_BUF     = 65536;   // 64KB cap antes de bailar
const MAX_BACKOFF_MS   = 60000;   // tope reconexión 60s
const BASE_BACKOFF_MS  = 1000;
const DEFAULT_PING_MS  = 15000;   // 15s, bajo el timeout 30s del server

function buildPacket(cmd, roomId, pid, payload) {
    payload = payload || Buffer.alloc(0);
    const pkt = Buffer.alloc(6 + payload.length);
    pkt[0] = MAGIC_0;
    pkt[1] = MAGIC_1;
    pkt[2] = cmd;
    pkt[3] = roomId || 0;
    pkt[4] = pid || 0;
    pkt[5] = payload.length;
    payload.copy(pkt, 6);
    return pkt;
}

class GhostBase {
    /**
     * @param {object} opts
     * @param {string} opts.name           — etiqueta para logs (ej: 'DAMAS', 'BURDYN#0')
     * @param {number} opts.gameId         — gameId del juego (para ROOM_CREATE)
     * @param {number} opts.maxPlayers     — capacidad de la sala
     * @param {number} [opts.protoVer=1]   — versión de protocolo (3er byte de ROOM_CREATE)
     * @param {number} [opts.tickMs]       — periodo del game tick (omitir si no se usa)
     * @param {number} [opts.pingMs=15000] — periodo del ping
     */
    constructor(opts) {
        this.name           = opts.name;
        this.gameId         = opts.gameId;
        this.maxPlayers     = opts.maxPlayers;
        this.protoVer       = opts.protoVer || 0x01;
        this.tickMs         = opts.tickMs;
        this.pingMs         = opts.pingMs || DEFAULT_PING_MS;

        this.sock           = null;
        this.recvBuf        = Buffer.alloc(0);
        this.tickInterval   = null;
        this.pingInterval   = null;
        this.reconnectTimer = null;
        this.scheduledTimers = new Set();
        this.failCount      = 0;
        this.shutdownRequested = false;
        this.connected      = false;
        this.authed         = false;

        this.roomId         = 0;
        this.pid            = 0;

        GhostBase._instances.add(this);
        GhostBase._hookSignals();
    }

    // -------------------------------------------------------------
    // SIGTERM/SIGINT global handler (instalado una sola vez)
    // -------------------------------------------------------------
    static _instances = new Set();
    static _signalsHooked = false;
    static _hookSignals() {
        if (GhostBase._signalsHooked) return;
        GhostBase._signalsHooked = true;
        const handler = (sig) => {
            console.log(`[ghost-base] ${sig} received, shutting down ${GhostBase._instances.size} ghosts`);
            for (const g of Array.from(GhostBase._instances)) {
                try { g.shutdown(); } catch (e) { console.error('shutdown error:', e); }
            }
            // Dar 500ms para que sockets se cierren antes de salir
            setTimeout(() => process.exit(0), 500).unref();
        };
        process.on('SIGTERM', () => handler('SIGTERM'));
        process.on('SIGINT',  () => handler('SIGINT'));
    }

    // -------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------
    log(msg) {
        const ts = new Date().toTimeString().substring(0, 8);
        console.log(`[${ts}] [${this.name}] ${msg}`);
    }

    start() {
        this._connect();
    }

    shutdown() {
        if (this.shutdownRequested) return;
        this.shutdownRequested = true;
        this._stop();
        GhostBase._instances.delete(this);
    }

    /** setTimeout tracked: se cancela en _stop. Usa esto en lugar del setTimeout global. */
    setTimer(fn, ms) {
        const handle = setTimeout(() => {
            this.scheduledTimers.delete(handle);
            try { fn(); } catch (e) { this.log(`Error en timer: ${e.stack || e}`); }
        }, ms);
        this.scheduledTimers.add(handle);
        return handle;
    }

    /** Envia paquete usando roomId/pid actuales. */
    send(cmd, payload) {
        if (!this.sock || this.sock.destroyed) return false;
        try {
            this.sock.write(buildPacket(cmd, this.roomId, this.pid, payload));
            return true;
        } catch (e) {
            this.log(`Send error: ${e.message}`);
            return false;
        }
    }

    /** Envia paquete con roomId/pid arbitrarios (para JOIN antes de tener sala). */
    sendRaw(cmd, roomId, pid, payload) {
        if (!this.sock || this.sock.destroyed) return false;
        try {
            this.sock.write(buildPacket(cmd, roomId, pid, payload));
            return true;
        } catch (e) {
            this.log(`Send error: ${e.message}`);
            return false;
        }
    }

    // -------------------------------------------------------------
    // Hooks que las subclases pueden sobrescribir
    // -------------------------------------------------------------
    onAuthOk() {}
    onRoomInfo(payload, len) {}
    onPacket(cmd, payload, len) {}
    onTick() {}
    onShutdown() {}

    // -------------------------------------------------------------
    // Internals
    // -------------------------------------------------------------
    _connect() {
        if (this.shutdownRequested) return;
        this._stop(); // garantizar estado limpio (idempotente)
        this.shutdownRequested = false; // _stop NO lo activa, pero por seguridad

        this.recvBuf  = Buffer.alloc(0);
        this.connected = false;
        this.authed    = false;
        this.roomId    = 0;
        this.pid       = 0;

        const sock = new net.Socket();
        this.sock = sock;

        sock.on('connect', () => {
            this.connected = true;
            this.log('Conectado al servidor');
            this.send(CMD.AUTH, AUTH_TOKEN);
        });

        sock.on('data',  (chunk) => this._onData(chunk));
        sock.on('error', (err)   => {
            if (err.code !== 'ECONNRESET') this.log(`Socket error: ${err.message}`);
            // 'close' siempre sigue, allí se reconecta
        });
        sock.on('close', () => this._onClose());

        try {
            sock.connect(SERVER_PORT, SERVER_IP);
        } catch (e) {
            this.log(`connect() throw: ${e.message}`);
            this._onClose();
        }
    }

    _onClose() {
        if (this.shutdownRequested) {
            this.log('Cerrando (shutdown)');
            this._stop();
            return;
        }
        this.log('Desconectado');
        this._stop();
        this._scheduleReconnect();
    }

    /** Limpia TODOS los timers y socket. Idempotente. */
    _stop() {
        if (this.tickInterval)   { clearInterval(this.tickInterval);   this.tickInterval = null; }
        if (this.pingInterval)   { clearInterval(this.pingInterval);   this.pingInterval = null; }
        if (this.reconnectTimer) { clearTimeout(this.reconnectTimer);  this.reconnectTimer = null; }
        for (const t of this.scheduledTimers) clearTimeout(t);
        this.scheduledTimers.clear();
        if (this.sock) {
            try { this.sock.removeAllListeners(); this.sock.destroy(); } catch (_) {}
            this.sock = null;
        }
        try { this.onShutdown(); } catch (e) { this.log(`onShutdown error: ${e.stack || e}`); }
    }

    _scheduleReconnect() {
        const delay = Math.min(MAX_BACKOFF_MS, BASE_BACKOFF_MS * Math.pow(2, this.failCount));
        this.failCount++;
        this.log(`Reconectando en ${delay}ms (fallo #${this.failCount})`);
        this.reconnectTimer = setTimeout(() => {
            this.reconnectTimer = null;
            this._connect();
        }, delay);
    }

    _onData(chunk) {
        // Defensa: cap de buffer
        if (this.recvBuf.length + chunk.length > MAX_RECV_BUF) {
            this.log(`recvBuf overflow (${this.recvBuf.length}+${chunk.length}B), reset y reconnect`);
            this.recvBuf = Buffer.alloc(0);
            if (this.sock) this.sock.destroy();
            return;
        }
        this.recvBuf = Buffer.concat([this.recvBuf, chunk]);

        while (this.recvBuf.length >= 6) {
            // Buscar magic byte a byte (sin alocar Buffer cada iter)
            let magicPos = -1;
            const upper = this.recvBuf.length - 1;
            for (let i = 0; i < upper; i++) {
                if (this.recvBuf[i] === MAGIC_0 && this.recvBuf[i + 1] === MAGIC_1) {
                    magicPos = i;
                    break;
                }
            }
            if (magicPos < 0) {
                // Mantener el último byte por si es el inicio de un magic partido
                this.recvBuf = this.recvBuf.length > 0
                    ? this.recvBuf.subarray(this.recvBuf.length - 1)
                    : Buffer.alloc(0);
                break;
            }
            if (magicPos > 0) {
                this.recvBuf = this.recvBuf.subarray(magicPos);
                continue;
            }

            const len = this.recvBuf[5];
            if (this.recvBuf.length < 6 + len) break;

            const cmd = this.recvBuf[2];
            const payload = Buffer.from(this.recvBuf.subarray(6, 6 + len));
            this.recvBuf = this.recvBuf.subarray(6 + len);

            try {
                this._dispatch(cmd, payload, len);
            } catch (e) {
                this.log(`dispatch error cmd=0x${cmd.toString(16)}: ${e.stack || e}`);
            }
        }
    }

    _dispatch(cmd, payload, len) {
        if (cmd === CMD.AUTH_OK) {
            this.authed = true;
            this.failCount = 0; // reset backoff al lograr auth completa
            this.log('Auth OK');
            try { this.onAuthOk(); } catch (e) { this.log(`onAuthOk error: ${e.stack || e}`); }
            // Iniciar ping loop (independiente del game tick)
            this.pingInterval = setInterval(() => this._ping(), this.pingMs);
            // Iniciar game tick si la subclase lo definió
            if (this.tickMs && typeof this.onTick === 'function') {
                this.tickInterval = setInterval(() => {
                    try { this.onTick(); } catch (e) { this.log(`onTick error: ${e.stack || e}`); }
                }, this.tickMs);
            }
            return;
        }
        if (cmd === CMD.ROOM_INFO) {
            // Base actualiza roomId/pid; subclase puede leer payload[0..n] para más
            if (len >= 1) this.roomId = payload[0];
            if (len >= 4) this.pid    = payload[3];
            try { this.onRoomInfo(payload, len); } catch (e) { this.log(`onRoomInfo error: ${e.stack || e}`); }
            return;
        }
        // Resto pasa al hook genérico
        try { this.onPacket(cmd, payload, len); } catch (e) { this.log(`onPacket error: ${e.stack || e}`); }
    }

    _ping() {
        if (this.roomId) this.send(CMD.PING);
    }
}

module.exports = { GhostBase, CMD, buildPacket, AUTH_TOKEN, SERVER_IP, SERVER_PORT };
