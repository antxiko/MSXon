'use strict';

// =============================================================================
// ghost-tetris.js — Ghost de Tetris migrado a GhostBase
//
// Reproduce fielmente el original:
//  - N instancias (default 3). Idx 0 crea sala
//  - gameId=0x06, maxPlayers=4, protoVer=0x01
//  - Tick 800ms con IA evaluativa
//  - Sala completa (4) → host envía GAME_START tras 3s
//  - Estado full sync 86 bytes empaquetado
//  - Maneja paquetes garbage entrantes (PKT_GARBAGE 0x03)
//
// Usa GhostRoomRegistry con gameKey='tetris'.
// =============================================================================

const { GhostBase, CMD } = require('./ghost-base');
const registry = require('./ghost-room-registry');

const GAME_ID_TETRIS = 0x06;
const TETRIS_MAX     = 4;
const TETRIS_TICK_MS = 800;
const TETRIS_GAME_KEY = 'tetris';

const TET_BW = 8, TET_BH = 20, TET_NUM_PIECES = 7;
const ACT_MOVE = 0, ACT_LOCK = 1, ACT_DEAD = 3; // (no se usan directamente, mantenidos por paridad)

const TET_PIECES = [
    // I
    { n: 2, r: [{ w: 4, h: 1, b: 0xF000 }, { w: 1, h: 4, b: 0x8888 }] },
    // O
    { n: 1, r: [{ w: 2, h: 2, b: 0xCC00 }] },
    // T
    { n: 4, r: [{ w: 3, h: 2, b: 0x4E00 }, { w: 2, h: 3, b: 0x8C80 }, { w: 3, h: 2, b: 0xE400 }, { w: 2, h: 3, b: 0x4C40 }] },
    // S
    { n: 2, r: [{ w: 3, h: 2, b: 0x6C00 }, { w: 2, h: 3, b: 0x8C40 }] },
    // Z
    { n: 2, r: [{ w: 3, h: 2, b: 0xC600 }, { w: 2, h: 3, b: 0x4C80 }] },
    // L
    { n: 4, r: [{ w: 2, h: 3, b: 0x88C0 }, { w: 3, h: 2, b: 0xE800 }, { w: 2, h: 3, b: 0xC440 }, { w: 3, h: 2, b: 0x2E00 }] },
    // J
    { n: 4, r: [{ w: 2, h: 3, b: 0x44C0 }, { w: 3, h: 2, b: 0x8E00 }, { w: 2, h: 3, b: 0xC880 }, { w: 3, h: 2, b: 0xE200 }] },
];

const TET_BIT_MASK = [
    [0x8000, 0x4000, 0x2000, 0x1000],
    [0x0800, 0x0400, 0x0200, 0x0100],
    [0x0080, 0x0040, 0x0020, 0x0010],
    [0x0008, 0x0004, 0x0002, 0x0001],
];

function tetBit(bits, row, col) { return (bits & TET_BIT_MASK[row][col]) ? 1 : 0; }
function tetGetRot(pi, rot) { return TET_PIECES[pi].r[rot % TET_PIECES[pi].n]; }

function tetValid(board, pi, rot, px, py) {
    const s = tetGetRot(pi, rot);
    for (let r = 0; r < s.h; r++)
        for (let c = 0; c < s.w; c++)
            if (tetBit(s.b, r, c)) {
                const nx = px + c, ny = py + r;
                if (nx < 0 || nx >= TET_BW || ny >= TET_BH) return false;
                if (ny >= 0 && board[ny][nx]) return false;
            }
    return true;
}

function tetEval(board, pi, rot, px, py) {
    let dy = py;
    while (tetValid(board, pi, rot, px, dy + 1)) dy++;
    const tmp = board.map(r => [...r]);
    const s = tetGetRot(pi, rot);
    for (let r = 0; r < s.h; r++)
        for (let c = 0; c < s.w; c++)
            if (tetBit(s.b, r, c)) {
                const by = dy + r, bx = px + c;
                if (by >= 0 && by < TET_BH && bx >= 0 && bx < TET_BW) tmp[by][bx] = 1;
            }
    let lines = 0, holes = 0, bump = 0, maxH = 0;
    const heights = new Array(TET_BW).fill(0);
    for (let c = 0; c < TET_BW; c++) {
        let found = false;
        for (let r = 0; r < TET_BH; r++) {
            if (tmp[r][c]) {
                if (!found) { heights[c] = TET_BH - r; found = true; }
                if (TET_BH - r > maxH) maxH = TET_BH - r;
            } else if (found) holes++;
        }
    }
    for (let r = 0; r < TET_BH; r++) {
        let full = true;
        for (let c = 0; c < TET_BW; c++) if (!tmp[r][c]) { full = false; break; }
        if (full) lines++;
    }
    for (let c = 0; c < TET_BW - 1; c++) bump += Math.abs(heights[c] - heights[c + 1]);
    return { score: lines * 200 - holes * 150 - bump * 20 - maxH * 10, x: px, rot, y: dy };
}

class TetrisGhost extends GhostBase {
    constructor(ghostNum) {
        super({
            name: `TETRIS#${ghostNum}`,
            gameId: GAME_ID_TETRIS,
            maxPlayers: TETRIS_MAX,
            tickMs: TETRIS_TICK_MS,
            pingMs: 4000, // ping 5 ticks de 800ms ≈ 4s (igual que el original)
        });
        this.ghostNum = ghostNum;
        this.gameStarted = false;
        this.board = Array.from({ length: TET_BH }, () => new Array(TET_BW).fill(0));
        this.pi = Math.floor(Math.random() * TET_NUM_PIECES);
        this.nxt = Math.floor(Math.random() * TET_NUM_PIECES);
        this.rot = 0;
        this.px = 3;
        this.py = 0;
        this.dead = false;
        this.aiTargetX = 3;
        this.aiTargetRot = 0;
        this.aiComputed = false;
    }

    onAuthOk() {
        if (this.ghostNum === 0) {
            this.sendRaw(CMD.ROOM_CREATE, 0, 0,
                Buffer.from([this.gameId, this.maxPlayers, this.protoVer]));
        } else {
            const roomId = registry.get(TETRIS_GAME_KEY);
            if (roomId > 0) {
                this.sendRaw(CMD.ROOM_JOIN, 0, 0, Buffer.from([roomId]));
            } // si no hay sala, simplemente espera (el original tampoco crea sala fallback)
        }
    }

    onRoomInfo(payload, len) {
        // base guardo roomId y pid
        if (this.ghostNum === 0) {
            registry.claim(TETRIS_GAME_KEY, this.roomId, this.name);
        }
        this.log(`Sala ${this.roomId}, PID ${this.pid}`);
    }

    onPacket(cmd, payload, len) {
        if (cmd === CMD.PLAYER_JOINED) {
            const joinedPid = payload[0];
            this.log(`Player joined PID ${joinedPid}`);
            // Host: cuando llega el 4º, esperar 3s y mandar GAME_START
            if (this.ghostNum === 0 && joinedPid === 4 && !this.gameStarted) {
                this.log('Sala completa! Empezando en 3s...');
                this.setTimer(() => {
                    if (!this.gameStarted) {
                        this.send(CMD.GAME_START);
                        this._initGameOnStart();
                        this.log('GAME_START enviado');
                    }
                }, 3000);
            }
            return;
        }
        if (cmd === CMD.GAME_START) {
            this._initGameOnStart();
            this.log('Partida iniciada');
            return;
        }
        if (cmd === CMD.GAME_END) {
            this.gameStarted = false;
            this.dead = false;
            for (let r = 0; r < TET_BH; r++)
                for (let c = 0; c < TET_BW; c++) this.board[r][c] = 0;
            this.log('GAME_END recibido, sala libre');
            return;
        }
        if (cmd === CMD.STATE_UPDATE && len >= 4 && payload[0] === 0x03) {
            // PKT_GARBAGE
            const garbTarget = payload[1];
            const garbCount  = payload[2];
            const garbGap    = payload[3];
            if (garbTarget === this.pid - 1) {
                for (let g = 0; g < garbCount; g++) {
                    let topBlocked = false;
                    for (let c = 0; c < TET_BW; c++) if (this.board[0][c]) { topBlocked = true; break; }
                    if (topBlocked) { this.dead = true; break; }
                    for (let r = 0; r < TET_BH - 1; r++)
                        for (let c = 0; c < TET_BW; c++) this.board[r][c] = this.board[r + 1][c];
                    for (let c = 0; c < TET_BW; c++) this.board[TET_BH - 1][c] = (c === garbGap) ? 0 : 5;
                }
                this.aiComputed = false;
                this.log(`Recibido ${garbCount} filas de garbage`);
            }
        }
    }

    _initGameOnStart() {
        this.gameStarted = true;
        this.aiComputed = false;
        for (let r = 0; r < TET_BH; r++)
            for (let c = 0; c < TET_BW; c++) this.board[r][c] = 0;
        this.dead = false;
        this._spawn();
    }

    _spawn() {
        this.pi = this.nxt;
        this.nxt = Math.floor(Math.random() * TET_NUM_PIECES);
        this.rot = 0;
        this.px = 3;
        this.py = 0;
        if (!tetValid(this.board, this.pi, this.rot, this.px, this.py)) {
            this.dead = true;
            this._sendDead();
        }
    }

    _lockPiece() {
        const s = tetGetRot(this.pi, this.rot);
        for (let r = 0; r < s.h; r++)
            for (let c = 0; c < s.w; c++)
                if (tetBit(s.b, r, c)) {
                    const by = this.py + r, bx = this.px + c;
                    if (by >= 0 && by < TET_BH && bx >= 0 && bx < TET_BW) this.board[by][bx] = this.pid;
                }
        // Limpiar líneas
        let linesCleared = 0;
        for (let r = TET_BH - 1; r >= 0; r--) {
            let full = true;
            for (let c = 0; c < TET_BW; c++) if (!this.board[r][c]) { full = false; break; }
            if (full) {
                linesCleared++;
                for (let r2 = r; r2 > 0; r2--)
                    for (let c = 0; c < TET_BW; c++) this.board[r2][c] = this.board[r2 - 1][c];
                for (let c = 0; c < TET_BW; c++) this.board[0][c] = 0;
                r++;
            }
        }
        this._sendPieceLock(linesCleared);
        this._spawn();
    }

    _aiCompute() {
        let best = { score: -99999, x: this.px, rot: 0 };
        const numRots = TET_PIECES[this.pi].n;
        for (let ri = 0; ri < numRots; ri++) {
            for (let xi = -1; xi < TET_BW; xi++) {
                if (!tetValid(this.board, this.pi, ri, xi, this.py)) continue;
                const ev = tetEval(this.board, this.pi, ri, xi, this.py);
                if (ev.score > best.score) best = { score: ev.score, x: xi, rot: ri };
            }
        }
        this.aiTargetX = best.x;
        this.aiTargetRot = best.rot;
        this.aiComputed = true;
    }

    _sendPiecePos() {
        this.send(CMD.STATE_UPDATE,
            Buffer.from([0x01, this.pi, this.rot, this.px & 0xFF, this.py & 0xFF]));
    }

    _sendPieceLock(linesCleared) {
        this.send(CMD.STATE_UPDATE,
            Buffer.from([0x02, this.pi, this.rot, this.px & 0xFF, this.py & 0xFF, linesCleared]));
    }

    _sendFullSync() {
        const payload = Buffer.alloc(86);
        payload[0] = 0x05; // PKT_FULL_SYNC
        payload[1] = this.pi;
        payload[2] = this.rot;
        payload[3] = this.px & 0xFF;
        payload[4] = this.py & 0xFF;
        payload[5] = this.dead ? 1 : 0;
        let idx = 6;
        for (let r = 0; r < TET_BH; r++)
            for (let c = 0; c < TET_BW; c += 2) {
                payload[idx] = ((this.board[r][c] & 0x0F) << 4) | (this.board[r][c + 1] & 0x0F);
                idx++;
            }
        this.send(CMD.STATE_UPDATE, payload);
    }

    _sendDead() {
        this.send(CMD.STATE_UPDATE, Buffer.from([0x04]));
    }

    onTick() {
        if (this.dead || !this.gameStarted) return;

        if (!this.aiComputed) this._aiCompute();

        // Rotar + mover X
        if (this.rot !== this.aiTargetRot) {
            const nr = this.aiTargetRot;
            if (tetValid(this.board, this.pi, nr, this.px, this.py)) this.rot = nr;
            else this.aiTargetRot = this.rot;
        }
        while (this.px < this.aiTargetX && tetValid(this.board, this.pi, this.rot, this.px + 1, this.py)) this.px++;
        while (this.px > this.aiTargetX && tetValid(this.board, this.pi, this.rot, this.px - 1, this.py)) this.px--;

        // Caer 2 filas por tick
        let dropped = 0;
        while (dropped < 2 && tetValid(this.board, this.pi, this.rot, this.px, this.py + 1)) {
            this.py++;
            dropped++;
        }

        // Bottom: lock
        if (!tetValid(this.board, this.pi, this.rot, this.px, this.py + 1)) {
            this._lockPiece();
            this.aiComputed = false;
        }

        this._sendFullSync();
    }

    onShutdown() {
        if (this.ghostNum === 0) {
            registry.release(TETRIS_GAME_KEY, this.name);
        }
        this.gameStarted = false;
    }
}

module.exports = TetrisGhost;

if (require.main === module) {
    const N = parseInt(process.argv[2] || '3', 10);
    new TetrisGhost(0).start();
    for (let i = 1; i < N; i++) {
        setTimeout(() => new TetrisGhost(i).start(), 3000 + i * 1500);
    }
}
