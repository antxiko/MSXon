'use strict';

// =============================================================================
// ghost-damas.js — Ghost de Damas migrado a GhostBase
//
// Reproduce fielmente el comportamiento del ghost original:
//  - 1 sola instancia, host=self
//  - Crea sala damas (gameId=0x02, max=2, proto=0x01)
//  - Color según pid (1=blancas, otro=negras)
//  - IA aleatoria con captura forzada y multi-captura
//  - Tick cada 1500ms
// =============================================================================

const { GhostBase, CMD } = require('./ghost-base');

// ── Constantes del juego ──────────────────────────────────────
const PIECE_NONE = 0;
const PIECE_WHITE = 1, PIECE_BLACK = 2;
const PIECE_WHITE_KING = 3, PIECE_BLACK_KING = 4;

const isWhite = p => p === PIECE_WHITE || p === PIECE_WHITE_KING;
const isBlack = p => p === PIECE_BLACK || p === PIECE_BLACK_KING;
const isKing  = p => p === PIECE_WHITE_KING || p === PIECE_BLACK_KING;
const isEnemy = (a, b) => (isWhite(a) && isBlack(b)) || (isBlack(a) && isWhite(b));

const GAME_ID_DAMAS = 0x02;
const DAMAS_MAX = 2;
const DAMAS_TICK_MS = 1500;

// ── Helpers de tablero ────────────────────────────────────────
function initBoard() {
    const b = Array.from({ length: 8 }, () => new Array(8).fill(0));
    for (let y = 0; y < 8; y++)
        for (let x = 0; x < 8; x++)
            if ((x + y) & 1) {
                if (y < 3) b[y][x] = PIECE_BLACK;
                else if (y > 4) b[y][x] = PIECE_WHITE;
            }
    return b;
}

function findMoves(board, color) {
    const captures = [];
    const moves = [];
    for (let y = 0; y < 8; y++) {
        for (let x = 0; x < 8; x++) {
            const p = board[y][x];
            if (p === PIECE_NONE) continue;
            const mine = (color === PIECE_WHITE) ? isWhite(p) : isBlack(p);
            if (!mine) continue;
            if (isKing(p)) {
                for (const sy of [-1, 1]) {
                    for (const sx of [-1, 1]) {
                        let fe = false;
                        for (let i = 1; i < 8; i++) {
                            const cx = x + sx * i, cy = y + sy * i;
                            if (cx < 0 || cx >= 8 || cy < 0 || cy >= 8) break;
                            const cp = board[cy][cx];
                            if (cp === PIECE_NONE) {
                                if (fe) captures.push({ fx: x, fy: y, tx: cx, ty: cy, cap: true });
                                else moves.push({ fx: x, fy: y, tx: cx, ty: cy, cap: false });
                            } else if (isEnemy(p, cp) && !fe) { fe = true; }
                            else break;
                        }
                    }
                }
            } else {
                const fwd = (color === PIECE_WHITE) ? -1 : 1;
                for (const dx of [-1, 1]) {
                    const tx = x + dx, ty = y + fwd;
                    if (tx >= 0 && tx < 8 && ty >= 0 && ty < 8 && board[ty][tx] === PIECE_NONE)
                        moves.push({ fx: x, fy: y, tx, ty, cap: false });
                }
                for (const dy of [-1, 1]) {
                    for (const dx of [-1, 1]) {
                        const mx = x + dx, my = y + dy;
                        const tx = x + dx * 2, ty = y + dy * 2;
                        if (tx >= 0 && tx < 8 && ty >= 0 && ty < 8 &&
                            board[ty][tx] === PIECE_NONE && isEnemy(p, board[my][mx]))
                            captures.push({ fx: x, fy: y, tx, ty, cap: true });
                    }
                }
            }
        }
    }
    return captures.length > 0 ? captures : moves;
}

function executeMove(board, fx, fy, tx, ty) {
    const p = board[fy][fx];
    board[ty][tx] = p;
    board[fy][fx] = PIECE_NONE;
    const dx = tx - fx, dy = ty - fy;
    const sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    const dist = Math.abs(dx);
    for (let i = 1; i < dist; i++) {
        const cx = fx + sx * i, cy = fy + sy * i;
        if (board[cy][cx] !== PIECE_NONE) { board[cy][cx] = PIECE_NONE; break; }
    }
    if (p === PIECE_WHITE && ty === 0) board[ty][tx] = PIECE_WHITE_KING;
    if (p === PIECE_BLACK && ty === 7) board[ty][tx] = PIECE_BLACK_KING;
}

// ── Ghost Damas ───────────────────────────────────────────────
class DamasGhost extends GhostBase {
    constructor() {
        super({
            name: 'DAMAS',
            gameId: GAME_ID_DAMAS,
            maxPlayers: DAMAS_MAX,
            tickMs: DAMAS_TICK_MS,
        });
        this._initGameState();
    }

    _initGameState() {
        this.board = initBoard();
        this.turn = PIECE_WHITE;
        this.myColor = PIECE_WHITE;
        this.gameStarted = false;
        this.moveCount = 0;
        this.multiCapX = -1;
        this.multiCapY = -1;
        this.multiCapCount = 0;
    }

    onAuthOk() {
        // Crear sala damas
        this.sendRaw(CMD.ROOM_CREATE, 0, 0,
            Buffer.from([this.gameId, this.maxPlayers, this.protoVer]));
        this.log('Sala damas creada. Esperando rival...');
    }

    onRoomInfo(payload, len) {
        // base ya guardó roomId y pid
        this.myColor = (this.pid === 1) ? PIECE_WHITE : PIECE_BLACK;
        this.log(`Sala 0x${this.roomId.toString(16).padStart(2, '0')} | PID=${this.pid} | ${this.myColor === PIECE_WHITE ? 'BLANCAS' : 'NEGRAS'}`);
    }

    onPacket(cmd, payload, len) {
        if (cmd === CMD.PLAYER_JOINED) {
            this.gameStarted = true;
            this.log('Rival conectado! Partida empieza.');
        }
        else if (cmd === CMD.PLAYER_LEFT) {
            this.log('Rival desconectado.');
            this._initGameState();
        }
        else if (cmd === CMD.GAME_END) {
            this.log('GAME_END recibido — preparando nueva partida si hay rival');
            this._initGameState();
            // En damas la partida arranca con 2 jugadores en sala. Si el
            // rival sigue conectado (humano elige "ENTER nueva"), volvemos a
            // gameStarted=true. Si el rival se ha ido, PLAYER_LEFT vendra
            // detras y resetea otra vez a false.
            if (this.roomId !== 0) this.gameStarted = true;
        }
        else if (cmd === CMD.STATE_UPDATE && len >= 5) {
            const fx = payload[0], fy = payload[1], tx = payload[2], ty = payload[3];
            const endTurn = payload[4];
            if (fx === tx && fy === ty) return;          // nulos
            if (fx >= 8 || fy >= 8 || tx >= 8 || ty >= 8) return;
            executeMove(this.board, fx, fy, tx, ty);
            if (endTurn) {
                this.turn = (this.turn === PIECE_WHITE) ? PIECE_BLACK : PIECE_WHITE;
                this.log(`Oponente: (${fx},${fy})->(${tx},${ty}) | Turno: ${this.turn === PIECE_WHITE ? 'B' : 'N'}`);
            } else {
                this.log(`Oponente: (${fx},${fy})->(${tx},${ty}) MULTI...`);
            }
        }
    }

    onTick() {
        if (!this.gameStarted) return;
        if (this.turn !== this.myColor) return;

        // Buscar movimientos validos (con o sin multi-captura en curso)
        let validMoves;
        if (this.multiCapX >= 0) {
            validMoves = findMoves(this.board, this.myColor).filter(
                m => m.fx === this.multiCapX && m.fy === this.multiCapY && m.cap === true
            );
            if (validMoves.length === 0) {
                // Fin multi-captura
                this.multiCapX = -1;
                this.multiCapY = -1;
                this.multiCapCount = 0;
                this.turn = (this.turn === PIECE_WHITE) ? PIECE_BLACK : PIECE_WHITE;
                this.log(`Fin multi-captura | Turno: ${this.turn === PIECE_WHITE ? 'B' : 'N'}`);
                return;
            }
        } else {
            validMoves = findMoves(this.board, this.myColor);
        }

        if (validMoves.length === 0) {
            let myPieces = 0;
            for (let r = 0; r < 8; r++)
                for (let c = 0; c < 8; c++)
                    if ((this.myColor === PIECE_WHITE && isWhite(this.board[r][c])) ||
                        (this.myColor === PIECE_BLACK && isBlack(this.board[r][c]))) myPieces++;
            if (myPieces === 0) this.log('Ghost ha perdido — sin fichas');
            else                 this.log('Ghost ha perdido — sin movimientos');
            this._initGameState();
            return;
        }

        // Elegir movimiento al azar
        const move = validMoves[Math.floor(Math.random() * validMoves.length)];
        executeMove(this.board, move.fx, move.fy, move.tx, move.ty);
        this.moveCount++;

        let endTurnFlag = 1;
        if (move.cap) {
            this.multiCapCount++;
            if (this.multiCapCount <= 12) {
                const moreCaps = findMoves(this.board, this.myColor).filter(
                    m => m.fx === move.tx && m.fy === move.ty && m.cap === true
                );
                if (moreCaps.length > 0) {
                    this.multiCapX = move.tx;
                    this.multiCapY = move.ty;
                    endTurnFlag = 0;
                }
            }
        }

        const pl = Buffer.from([move.fx, move.fy, move.tx, move.ty, endTurnFlag, 0, 0, 0]);
        this.send(CMD.STATE_UPDATE, pl);
        this.log(`Ghost: (${move.fx},${move.fy})->(${move.tx},${move.ty})${move.cap ? ' CAP' : ''} #${this.moveCount}${endTurnFlag ? '' : ' MULTI...'}`);

        if (endTurnFlag) {
            this.multiCapX = -1;
            this.multiCapY = -1;
            this.multiCapCount = 0;
            this.turn = (this.turn === PIECE_WHITE) ? PIECE_BLACK : PIECE_WHITE;
            this.log(`Turno: ${this.turn === PIECE_WHITE ? 'BLANCAS' : 'NEGRAS'}`);
        }
    }

    onShutdown() {
        // Reset estado por si se reconecta
        this._initGameState();
    }
}

module.exports = DamasGhost;

// Permitir ejecutar este archivo standalone para tests: `node ghost-damas.js`
if (require.main === module) {
    const g = new DamasGhost();
    g.start();
}
