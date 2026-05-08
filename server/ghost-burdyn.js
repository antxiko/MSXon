'use strict';

// =============================================================================
// ghost-burdyn.js — Ghost de Burdyn migrado a GhostBase
//
// Reproduce fielmente el comportamiento del original:
//  - N instancias (default 2). Idx 0 crea sala, resto JOIN
//  - gameId=0x03, maxPlayers=14, protoVer=0x02
//  - Movimiento rebote diagonal en mapa 64x64
//  - Tick 300ms (~3 movimientos/seg)
//  - Cada ghost arranca en posición distinta segun idx
//
// Usa GhostRoomRegistry con gameKey='burdyn' para compartir roomId entre instancias.
// =============================================================================

const { GhostBase, CMD } = require('./ghost-base');
const registry = require('./ghost-room-registry');

const GAME_ID_BURDYN = 0x03;
const BURDYN_MAX     = 14;
const BURDYN_PROTO   = 0x02;
const BURDYN_TICK_MS = 300;
const BURDYN_GAME_KEY = 'burdyn';
const LOG_EVERY_MOVES = 75;

class BurdynGhost extends GhostBase {
    /**
     * @param {number} idx — índice del ghost (0 = host que crea sala)
     */
    constructor(idx) {
        super({
            name: `BURDYN#${idx}`,
            gameId: GAME_ID_BURDYN,
            maxPlayers: BURDYN_MAX,
            protoVer: BURDYN_PROTO,
            tickMs: BURDYN_TICK_MS,
        });
        this.idx = idx;
        this.gameStarted = false;
        // Posición y velocidad iniciales (mismas fórmulas que el original)
        this.x = 3 + idx * 5;
        this.y = 3 + idx * 4;
        this.dx = (idx % 2 === 0) ?  1 : -1;
        this.dy = (idx % 3 === 0) ? -1 :  1;
        this.moveCounter = 0;
    }

    onAuthOk() {
        const roomCreatePayload = Buffer.from([this.gameId, this.maxPlayers, this.protoVer]);
        if (this.idx === 0) {
            this.sendRaw(CMD.ROOM_CREATE, 0, 0, roomCreatePayload);
            this.log(`Auth OK (idx=0) — creando sala`);
        } else {
            const roomId = registry.get(BURDYN_GAME_KEY);
            if (roomId > 0) {
                this.log(`Auth OK (idx=${this.idx}) — JOIN sala 0x${roomId.toString(16).padStart(2, '0')}`);
                this.sendRaw(CMD.ROOM_JOIN, 0, 0, Buffer.from([roomId]));
            } else {
                this.log(`Auth OK (idx=${this.idx}) — sin host, creando sala propia`);
                this.sendRaw(CMD.ROOM_CREATE, 0, 0, roomCreatePayload);
            }
        }
    }

    onRoomInfo(payload, len) {
        // base ya guardo this.roomId y this.pid
        if (this.idx === 0) {
            registry.claim(BURDYN_GAME_KEY, this.roomId, this.name);
        }
        this.log(`Sala 0x${this.roomId.toString(16).padStart(2, '0')} | PID=${this.pid} | hostRoomId=${registry.get(BURDYN_GAME_KEY)}`);
        // Mandar GAME_START si soy el host (pid === 1)
        if (this.pid === 1) {
            this.send(CMD.GAME_START);
            this.log('GAME_START enviado');
        }
        this.gameStarted = true;
    }

    onPacket(cmd, payload, len) {
        if (cmd === CMD.PLAYER_JOINED) {
            this.log('Jugador conectado');
        } else if (cmd === CMD.PLAYER_LEFT) {
            this.log('Jugador desconectado');
        }
    }

    onTick() {
        if (!this.gameStarted) return;

        // Mover y rebotar
        this.x += this.dx;
        this.y += this.dy;
        if (this.x <= 1 || this.x >= 62) this.dx = -this.dx;
        if (this.y <= 1 || this.y >= 62) this.dy = -this.dy;
        if (this.x < 1)  this.x = 1;
        if (this.x > 62) this.x = 62;
        if (this.y < 1)  this.y = 1;
        if (this.y > 62) this.y = 62;

        const pl = Buffer.from([this.x, this.y, 0, 100, 0, 0, 1, 0]);
        this.send(CMD.STATE_UPDATE, pl);

        this.moveCounter++;
        if (this.moveCounter % LOG_EVERY_MOVES === 0) {
            this.log(`pos=(${this.x},${this.y}) pid=${this.pid} room=${this.roomId}`);
        }
    }

    onShutdown() {
        // Si soy el host y aún tengo claim, liberar para que otro pueda intentar
        if (this.idx === 0) {
            registry.release(BURDYN_GAME_KEY, this.name);
        }
        this.gameStarted = false;
    }
}

module.exports = BurdynGhost;

// Standalone: `node ghost-burdyn.js [N]` lanza N ghosts (default 2)
if (require.main === module) {
    const N = parseInt(process.argv[2] || '2', 10);
    new BurdynGhost(0).start();
    for (let i = 1; i < N; i++) {
        setTimeout(() => new BurdynGhost(i).start(), 5000 + i * 1500);
    }
}
