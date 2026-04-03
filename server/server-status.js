'use strict';

// =============================================================================
// server-status.js — Monitor del servidor MSX Online
//
// Conecta al servidor como cliente, consulta el estado y lo muestra.
// Uso: node server-status.js [ip] [puerto]
// =============================================================================

const net      = require('net');
const readline = require('readline');

const SERVER_IP   = process.argv[2] || '217.154.107.144';
const SERVER_PORT = parseInt(process.argv[3] || '9876', 10);
const AUTH_TOKEN  = Buffer.from([0xDE, 0xAD, 0xBE, 0xEF]);

const MAGIC_0 = 0x46; // 'F'
const MAGIC_1 = 0x4D; // 'M'
const CMD = {
    PING:         0x01,
    PONG:         0x02,
    AUTH:         0x10,
    AUTH_OK:      0x11,
    AUTH_FAIL:    0x12,
    ROOM_CREATE:  0x20,
    ROOM_JOIN:    0x21,
    ROOM_LIST:    0x26,
    ROOM_INFO:    0x23,
    STATE_UPDATE: 0x40,
};

const GAME_NAMES = {
    0x01: 'Prueba (sprites)',
    0x02: 'Damas',
    0x03: 'Burdyn',
};

let socket = null;
let authenticated = false;
let buffer = Buffer.alloc(0);

// ── Protocolo ──────────────────────────────────────────────────

function buildPacket(cmd, roomId, pid, payload) {
    payload = payload || Buffer.alloc(0);
    const pkt = Buffer.allocUnsafe(6 + payload.length);
    pkt[0] = MAGIC_0;
    pkt[1] = MAGIC_1;
    pkt[2] = cmd;
    pkt[3] = roomId || 0;
    pkt[4] = pid || 0;
    pkt[5] = payload.length;
    payload.copy(pkt, 6);
    return pkt;
}

function parsePackets() {
    const packets = [];
    while (buffer.length >= 6) {
        const idx = buffer.indexOf(Buffer.from([MAGIC_0, MAGIC_1]));
        if (idx < 0) { buffer = Buffer.alloc(0); break; }
        if (idx > 0) { buffer = buffer.subarray(idx); continue; }
        const len = buffer[5];
        if (buffer.length < 6 + len) break;
        packets.push({
            cmd:     buffer[2],
            roomId:  buffer[3],
            pid:     buffer[4],
            payload: buffer.subarray(6, 6 + len),
        });
        buffer = buffer.subarray(6 + len);
    }
    return packets;
}

// ── Conexion ───────────────────────────────────────────────────

function connect() {
    return new Promise((resolve, reject) => {
        if (socket && !socket.destroyed) {
            resolve();
            return;
        }
        authenticated = false;
        buffer = Buffer.alloc(0);
        socket = new net.Socket();
        socket.setTimeout(10000);

        socket.connect(SERVER_PORT, SERVER_IP, () => {
            socket.write(buildPacket(CMD.AUTH, 0, 0, AUTH_TOKEN));
        });

        socket.on('data', (chunk) => {
            buffer = Buffer.concat([buffer, chunk]);
            for (const pkt of parsePackets()) {
                if (pkt.cmd === CMD.AUTH_OK) {
                    authenticated = true;
                    resolve();
                } else if (pkt.cmd === CMD.AUTH_FAIL) {
                    reject(new Error('Auth rechazada'));
                }
            }
        });

        socket.on('error', (err) => reject(err));
        socket.on('timeout', () => { socket.destroy(); reject(new Error('Timeout')); });
        socket.on('close', () => { authenticated = false; });
    });
}

function disconnect() {
    if (socket && !socket.destroyed) socket.destroy();
    socket = null;
    authenticated = false;
}

function sendAndWait(cmd, payload, waitCmd, timeoutMs) {
    return new Promise((resolve, reject) => {
        timeoutMs = timeoutMs || 5000;
        const timer = setTimeout(() => reject(new Error('Timeout esperando respuesta')), timeoutMs);

        const handler = (chunk) => {
            buffer = Buffer.concat([buffer, chunk]);
            for (const pkt of parsePackets()) {
                if (pkt.cmd === waitCmd) {
                    clearTimeout(timer);
                    socket.removeListener('data', handler);
                    resolve(pkt);
                    return;
                }
            }
        };
        socket.on('data', handler);
        socket.write(buildPacket(cmd, 0, 0, payload));
    });
}

// ── Comandos ───────────────────────────────────────────────────

async function showRooms() {
    try {
        await connect();
        const pkt = await sendAndWait(CMD.ROOM_LIST, null, CMD.ROOM_LIST);
        const count = pkt.payload.length > 0 ? pkt.payload[0] : 0;

        console.log('');
        console.log('╔══════════════════════════════════════╗');
        console.log('║         SALAS ABIERTAS               ║');
        console.log('╠══════╦══════════╦════════════════════╣');
        console.log('║ SALA ║  JUEGO   ║    JUGADORES       ║');
        console.log('╠══════╬══════════╬════════════════════╣');

        if (count === 0) {
            console.log('║      ║  No hay salas abiertas        ║');
        } else {
            for (let i = 0; i < count; i++) {
                const off = 1 + i * 3;
                const roomId  = pkt.payload[off];
                const gameId  = pkt.payload[off + 1];
                const players = pkt.payload[off + 2];
                const gameName = GAME_NAMES[gameId] || `Juego 0x${gameId.toString(16).padStart(2, '0')}`;
                const roomStr = `0x${roomId.toString(16).padStart(2, '0')}`.padEnd(5);
                const gameStr = ` ${gameName}`.padEnd(9).substring(0, 9);
                const playStr = ` ${players} jugador(es)`.padEnd(19).substring(0, 19);
                console.log(`║ ${roomStr}║${gameStr}║${playStr}║`);
            }
        }
        console.log('╚══════╩══════════╩════════════════════╝');
        console.log(`Total: ${count} sala(s)`);
        console.log('');
    } catch (err) {
        console.log(`Error: ${err.message}`);
    }
}

async function showStatus() {
    console.log('');
    console.log('╔══════════════════════════════════════╗');
    console.log('║       ESTADO DEL SERVIDOR            ║');
    console.log('╠══════════════════════════════════════╣');
    console.log(`║ Servidor: ${SERVER_IP}:${SERVER_PORT}`.padEnd(39) + '║');

    try {
        await connect();
        console.log('║ Conexion: OK (autenticado)           ║');
    } catch (err) {
        console.log(`║ Conexion: FALLO - ${err.message}`.padEnd(39) + '║');
    }
    console.log('╚══════════════════════════════════════╝');
    console.log('');
}

function askQuestion(rl, question) {
    return new Promise((resolve) => {
        rl.question(question, (answer) => resolve(answer.trim()));
    });
}

async function createTestRoom(rl) {
    try {
        console.log('\nJuegos disponibles:');
        for (const [id, name] of Object.entries(GAME_NAMES)) {
            console.log(`  ${id}. ${name}`);
        }
        const gameChoice = await askQuestion(rl, 'Game ID (1-255): ');
        const gameId = parseInt(gameChoice, 10);
        if (!gameId || gameId < 1 || gameId > 255) {
            console.log('Game ID no valido.');
            return;
        }
        const maxChoice = await askQuestion(rl, 'Max jugadores (1-16, default 4): ');
        const maxPlayers = parseInt(maxChoice, 10) || 4;

        await connect();
        // gameId 0x03 (Burdyn) usa AGGREGATE mode (proto version 0x02)
        const protoVer = (gameId === 0x03) ? 0x02 : 0x01;
        const pkt = await sendAndWait(CMD.ROOM_CREATE, Buffer.from([gameId, Math.min(maxPlayers, 16), protoVer]), CMD.ROOM_INFO);
        const roomId  = pkt.payload[0];
        const gId     = pkt.payload[1];
        const players = pkt.payload[2];
        const myPid   = pkt.payload[3];
        const gameName = GAME_NAMES[gId] || `Juego 0x${gId.toString(16).padStart(2, '0')}`;
        console.log(`\nSala creada: 0x${roomId.toString(16).padStart(2, '0')} | ${gameName} | jugadores=${players} | max=${maxPlayers} | tu PID=${myPid}\n`);
    } catch (err) {
        console.log(`Error: ${err.message}`);
    }
}

const ghosts = []; // Array de { socket, interval, roomId, pid }

function createGhostConnection() {
    return new Promise((resolve, reject) => {
        const sock = new net.Socket();
        sock.setTimeout(10000);
        sock.connect(SERVER_PORT, SERVER_IP, () => {
            sock.write(buildPacket(CMD.AUTH, 0, 0, AUTH_TOKEN));
        });
        let buf = Buffer.alloc(0);
        sock.on('data', (chunk) => {
            buf = Buffer.concat([buf, chunk]);
            while (buf.length >= 6) {
                const idx = buf.indexOf(Buffer.from([0x46, 0x4D]));
                if (idx < 0) { buf = Buffer.alloc(0); break; }
                if (idx > 0) { buf = buf.subarray(idx); continue; }
                const len = buf[5];
                if (buf.length < 6 + len) break;
                const cmd = buf[2];
                const payload = buf.subarray(6, 6 + len);
                buf = buf.subarray(6 + len);
                if (cmd === 0x11) resolve({ sock, buf }); // AUTH_OK
                if (cmd === 0x12) reject(new Error('Auth fail'));
            }
        });
        sock.on('error', reject);
        sock.on('timeout', () => { sock.destroy(); reject(new Error('Timeout')); });
    });
}

function ghostSendAndWait(sock, cmd, payload, waitCmd) {
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('Timeout')), 5000);
        let buf = Buffer.alloc(0);
        const handler = (chunk) => {
            buf = Buffer.concat([buf, chunk]);
            while (buf.length >= 6) {
                if (buf[0] !== 0x46 || buf[1] !== 0x4D) { buf = buf.subarray(1); continue; }
                const len = buf[5];
                if (buf.length < 6 + len) break;
                const c = buf[2];
                const p = buf.subarray(6, 6 + len);
                buf = buf.subarray(6 + len);
                if (c === waitCmd) {
                    clearTimeout(timer);
                    sock.removeListener('data', handler);
                    resolve({ cmd: c, payload: p });
                    return;
                }
            }
        };
        sock.on('data', handler);
        sock.write(buildPacket(cmd, 0, 0, payload));
    });
}

async function startGhostPlayer() {
    try {
        const { sock } = await createGhostConnection();

        let roomId, pid;

        if (ghosts.length === 0) {
            // Primer ghost: crear sala Burdyn AGGREGATE
            const infoPkt = await ghostSendAndWait(sock, CMD.ROOM_CREATE, Buffer.from([0x03, 14, 0x02]), CMD.ROOM_INFO);
            roomId = infoPkt.payload[0];
            pid = infoPkt.payload[3];
            console.log(`\nGhost ${ghosts.length + 1}: sala creada 0x${roomId.toString(16).padStart(2, '0')} | PID=${pid}`);
            sock.write(buildPacket(0x32, roomId, pid)); // GAME_START
            console.log('GAME_START enviado');
        } else {
            // Siguientes ghosts: unirse a la sala del primer ghost
            roomId = ghosts[0].roomId;
            const joinPkt = await ghostSendAndWait(sock, CMD.ROOM_JOIN, Buffer.from([roomId]), CMD.ROOM_INFO);
            roomId = joinPkt.payload[0];
            pid = joinPkt.payload[3];
            console.log(`\nGhost ${ghosts.length + 1}: unido a sala 0x${roomId.toString(16).padStart(2, '0')} | PID=${pid}`);
        }

        // Posicion y velocidad aleatoria
        let x = 3 + ghosts.length * 5;
        let y = 3 + ghosts.length * 4;
        let dx = (ghosts.length % 2 === 0) ? 1 : -1;
        let dy = (ghosts.length % 3 === 0) ? -1 : 1;
        let frame = 0;

        const interval = setInterval(() => {
            x += dx;
            y += dy;
            if (x <= 1 || x >= 62) dx = -dx;
            if (y <= 1 || y >= 62) dy = -dy;
            if (x < 1) x = 1; if (x > 62) x = 62;
            if (y < 1) y = 1; if (y > 62) y = 62;
            frame = (frame + 1) & 0xFF;

            const payload = Buffer.from([x, y, 0, 100, 0, 0, 1, 0]);
            if (!sock.destroyed) {
                sock.write(buildPacket(CMD.STATE_UPDATE, roomId, pid, payload));
            }
        }, 250);

        ghosts.push({ socket: sock, interval, roomId, pid, x, y });
        console.log(`  Ghost #${ghosts.length} | PID=${pid} | Sala=0x${roomId.toString(16).padStart(2,'0')} | Pos=(${x},${y}) | dx=${dx} dy=${dy}`);
        console.log(`  Total: ${ghosts.length} ghost(s). "stop" para parar todos.\n`);

    } catch (err) {
        console.log(`Error: ${err.message}`);
    }
}

function stopGhostPlayer() {
    if (ghosts.length === 0) {
        console.log('\nNo hay ghosts activos.\n');
        return;
    }
    for (const g of ghosts) {
        clearInterval(g.interval);
        if (!g.socket.destroyed) g.socket.destroy();
    }
    console.log(`\n${ghosts.length} ghost(s) detenido(s).\n`);
    ghosts.length = 0;
}

async function pingServer() {
    try {
        await connect();
        const start = Date.now();
        await sendAndWait(CMD.PING, null, CMD.PONG);
        const ms = Date.now() - start;
        console.log(`\nPing: ${ms}ms\n`);
    } catch (err) {
        console.log(`Error: ${err.message}`);
    }
}

// ── Menu ───────────────────────────────────────────────────────

function showMenu() {
    console.log('');
    console.log('═══════════════════════════════════════');
    console.log('  MSX ONLINE — Monitor del servidor');
    console.log('═══════════════════════════════════════');
    console.log('  1. Ver salas abiertas');
    console.log('  2. Estado del servidor (ping)');
    console.log('  3. Crear sala de prueba');
    console.log('  4. Ping al servidor');
    console.log('  5. Reconectar');
    console.log('  6. Jugador fantasma (rebota por pantalla)');
    console.log('  7. Parar jugador fantasma');
    console.log('  0. Salir');
    console.log('═══════════════════════════════════════');
}

async function main() {
    const rl = readline.createInterface({ input: process.stdin, output: process.stdout });

    const ask = () => {
        rl.question('\n> ', async (input) => {
            const choice = input.trim();
            switch (choice) {
                case '1': case 'rooms': case 'r':
                    await showRooms();
                    break;
                case '2': case 'status': case 's':
                    await showStatus();
                    break;
                case '3': case 'create': case 'c':
                    await createTestRoom(rl);
                    break;
                case '4': case 'ping': case 'p':
                    await pingServer();
                    break;
                case '5': case 'reconnect':
                    disconnect();
                    console.log('Desconectado. Se reconectara al siguiente comando.');
                    break;
                case '6': case 'ghost': case 'g':
                    await startGhostPlayer();
                    break;
                case '7': case 'stop':
                    stopGhostPlayer();
                    break;
                case '0': case 'q': case 'quit': case 'exit':
                    disconnect();
                    rl.close();
                    process.exit(0);
                    break;
                case '': case 'help': case 'h': case 'menu':
                    showMenu();
                    break;
                default:
                    console.log('Opcion no valida. Escribe "help" para ver el menu.');
            }
            ask();
        });
    };

    showMenu();
    ask();
}

main().catch(console.error);
