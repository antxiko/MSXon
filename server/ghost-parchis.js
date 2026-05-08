'use strict';

// =============================================================================
// ghost-parchis.js — Ghost de Parchís migrado a GhostBase
//
// Reproduce fielmente el original:
//  - N instancias (default 3). Idx 0 crea sala
//  - gameId=0x04, maxPlayers=4, protoVer=0x01
//  - Tick irrelevante para game loop (turnos via setTimer encadenados);
//    pingMs delegado a GhostBase (15s)
//  - Sala completa (4 jugadores) → host envía GAME_START tras 3s
//  - Estado por jugador: 4 fichas, posición, estado (home/board/passage/goal)
//  - Si recibe ROOM_FULL/ROOM_NOT_FOUND, recrea sala propia
//
// Usa GhostRoomRegistry con gameKey='parchis'.
// =============================================================================

const { GhostBase, CMD } = require('./ghost-base');
const registry = require('./ghost-room-registry');

const GAME_ID_PARCHIS = 0x04;
const PARCHIS_MAX     = 4;
const PARCHIS_GAME_KEY = 'parchis';

// Salida en el recorrido por jugador (0-indexed: P1, P2, P3, P4)
const PARCHIS_EXIT = [5, 22, 39, 56];
// Casilla antes de entrar al pasillo por jugador
const PARCHIS_ENTER_PASS = [68, 17, 34, 51];
const PARCHIS_PATH_LENGTH = 68;
const PARCHIS_PASSAGE_LENGTH = 8;

const PARCHIS_STATE_HOME    = 0;
const PARCHIS_STATE_BOARD   = 1;
const PARCHIS_STATE_PASSAGE = 2;
const PARCHIS_STATE_GOAL    = 3;

class ParchisGhost extends GhostBase {
    constructor(ghostNum) {
        super({
            name: `PARCHIS#${ghostNum}`,
            gameId: GAME_ID_PARCHIS,
            maxPlayers: PARCHIS_MAX,
            // Sin tickMs: turnos via setTimer encadenados
        });
        this.ghostNum = ghostNum;
        this._initGameState();
    }

    _initGameState() {
        this.mySlot = 0;
        this.turn = 0;
        this.gameStarted = false;
        this.activePlayers = 0;
        this.pieces = [
            { state: PARCHIS_STATE_HOME, pos: 0 },
            { state: PARCHIS_STATE_HOME, pos: 0 },
            { state: PARCHIS_STATE_HOME, pos: 0 },
            { state: PARCHIS_STATE_HOME, pos: 0 },
        ];
    }

    onAuthOk() {
        this.log(`Auth OK (ghostNum=${this.ghostNum}, registryRoomId=${registry.get(PARCHIS_GAME_KEY)})`);
        if (this.ghostNum === 0) {
            this.log('Creando sala (ghost 0)');
            this.sendRaw(CMD.ROOM_CREATE, 0, 0,
                Buffer.from([this.gameId, this.maxPlayers, this.protoVer]));
        } else {
            const roomId = registry.get(PARCHIS_GAME_KEY);
            if (roomId > 0) {
                this.log(`JOIN sala ${roomId}`);
                this.sendRaw(CMD.ROOM_JOIN, 0, 0, Buffer.from([roomId]));
            } else {
                this.log('Sin sala host, creando sala propia');
                this.sendRaw(CMD.ROOM_CREATE, 0, 0,
                    Buffer.from([this.gameId, this.maxPlayers, this.protoVer]));
            }
        }
    }

    onRoomInfo(payload, len) {
        // base ya guardo roomId/pid
        this.mySlot = this.pid - 1;
        if (this.pid === 1) {
            registry.claim(PARCHIS_GAME_KEY, this.roomId, this.name);
        }
        // payload[2] = numero de jugadores activos
        const np = (len >= 3) ? payload[2] : 1;
        this.activePlayers = 0;
        for (let j = 0; j < np; j++) this.activePlayers |= (1 << j);
        this.log(`Sala ${this.roomId} PID=${this.pid} slot=${this.mySlot} activos=${np}`);
    }

    onPacket(cmd, payload, len) {
        if (cmd === CMD.PLAYER_JOINED) {
            const joinedPid = payload[0];
            this.activePlayers |= (1 << (joinedPid - 1));
            this.log(`Player joined PID=${joinedPid}`);
            if (this.pid === 1 && joinedPid === 4 && !this.gameStarted) {
                this.log('Sala completa (4/4), empezando en 3s...');
                this.setTimer(() => {
                    if (!this.gameStarted) {
                        this.send(CMD.GAME_START);
                        this.gameStarted = true;
                        this.turn = 0;
                        for (let i = 0; i < 4; i++) {
                            this.pieces[i].state = PARCHIS_STATE_HOME;
                            this.pieces[i].pos = 0;
                        }
                        this.log('GAME_START enviado');
                        if (this.turn === this.mySlot) this.setTimer(() => this._takeTurn(), 2000);
                    }
                }, 3000);
            }
            return;
        }
        if (cmd === CMD.PLAYER_LEFT) {
            const leftPid = payload[0];
            this.activePlayers &= ~(1 << (leftPid - 1));
            this.log(`Player left PID=${leftPid}`);
            return;
        }
        if (cmd === CMD.ROOM_FULL || cmd === CMD.ROOM_NOT_FOUND) {
            this.log(`Sala invalida (cmd=0x${cmd.toString(16)}), creando sala propia`);
            registry.release(PARCHIS_GAME_KEY, this.name);
            this.sendRaw(CMD.ROOM_CREATE, 0, 0,
                Buffer.from([this.gameId, this.maxPlayers, this.protoVer]));
            return;
        }
        if (cmd === CMD.GAME_START) {
            this.gameStarted = true;
            this.turn = 0;
            for (let i = 0; i < 4; i++) {
                this.pieces[i].state = PARCHIS_STATE_HOME;
                this.pieces[i].pos = 0;
            }
            this.log('Partida iniciada');
            if (this.turn === this.mySlot) this.setTimer(() => this._takeTurn(), 2000);
            return;
        }
        if (cmd === CMD.STATE_UPDATE && len >= 6) {
            const action = payload[0];
            const endTurn = payload[4];
            if (action === 1 && endTurn) {
                this._advanceTurn();
            }
        }
    }

    _decideMove(dice) {
        // 1. Si saco 5 y tengo ficha en casa: sacar
        if (dice === 5) {
            for (let i = 0; i < 4; i++) {
                if (this.pieces[i].state === PARCHIS_STATE_HOME) {
                    return {
                        pieceIdx: i,
                        newPos: PARCHIS_EXIT[this.mySlot] - 1,
                        newState: PARCHIS_STATE_BOARD,
                    };
                }
            }
        }
        // 2. Buscar ficha en tablero
        for (let i = 0; i < 4; i++) {
            if (this.pieces[i].state === PARCHIS_STATE_BOARD) {
                let newPos = this.pieces[i].pos + dice;
                const enterPass = PARCHIS_ENTER_PASS[this.mySlot] - 1;
                const distToEnter = (enterPass - this.pieces[i].pos + PARCHIS_PATH_LENGTH) % PARCHIS_PATH_LENGTH;
                if (dice > distToEnter) {
                    const passPos = dice - distToEnter - 1;
                    if (passPos < PARCHIS_PASSAGE_LENGTH) {
                        if (passPos === PARCHIS_PASSAGE_LENGTH - 1) {
                            return { pieceIdx: i, newPos: passPos, newState: PARCHIS_STATE_GOAL };
                        }
                        return { pieceIdx: i, newPos: passPos, newState: PARCHIS_STATE_PASSAGE };
                    }
                    continue;
                }
                newPos = newPos % PARCHIS_PATH_LENGTH;
                return { pieceIdx: i, newPos, newState: PARCHIS_STATE_BOARD };
            }
        }
        // 3. Ficha en pasillo
        for (let i = 0; i < 4; i++) {
            if (this.pieces[i].state === PARCHIS_STATE_PASSAGE) {
                const newPos = this.pieces[i].pos + dice;
                if (newPos < PARCHIS_PASSAGE_LENGTH) {
                    if (newPos === PARCHIS_PASSAGE_LENGTH - 1) {
                        return { pieceIdx: i, newPos, newState: PARCHIS_STATE_GOAL };
                    }
                    return { pieceIdx: i, newPos, newState: PARCHIS_STATE_PASSAGE };
                }
            }
        }
        return null;
    }

    _takeTurn() {
        if (!this.gameStarted || !this.sock || this.sock.destroyed) return;
        if (this.turn !== this.mySlot) return;

        const dice = 1 + Math.floor(Math.random() * 6);
        const endTurnDice = 0;
        const dicePl = Buffer.from([0, dice, 0, 0, endTurnDice, 0, 0, 0]);
        this.send(CMD.STATE_UPDATE, dicePl);

        const delay = 1500 + Math.floor(Math.random() * 1500);
        this.setTimer(() => {
            if (!this.sock || this.sock.destroyed) return;
            const mv = this._decideMove(dice);
            const endTurn = (dice === 6) ? 0 : 1;
            if (mv) {
                this.pieces[mv.pieceIdx].state = mv.newState;
                this.pieces[mv.pieceIdx].pos = mv.newPos;
                const movePl = Buffer.from([1, dice, mv.pieceIdx, mv.newPos, endTurn, mv.newState, 0, 0]);
                this.send(CMD.STATE_UPDATE, movePl);
                this.log(`Dado=${dice} pieza=${mv.pieceIdx} pos=${mv.newPos} estado=${mv.newState} endTurn=${endTurn}`);
            } else {
                const piece0 = this.pieces[0];
                const movePl = Buffer.from([1, dice, 0, piece0.pos, endTurn, piece0.state, 0, 0]);
                this.send(CMD.STATE_UPDATE, movePl);
                this.log(`Dado=${dice} sin movimientos`);
            }
            if (endTurn) {
                this._advanceTurn();
            } else {
                this.setTimer(() => this._takeTurn(), 2000);
            }
        }, delay);
    }

    _advanceTurn() {
        let next = (this.turn + 1) % 4;
        let tries = 0;
        while (!(this.activePlayers & (1 << next)) && tries < 4) {
            next = (next + 1) % 4;
            tries++;
        }
        this.turn = next;
        if (this.turn === this.mySlot) {
            this.setTimer(() => this._takeTurn(), 1500);
        }
    }

    onShutdown() {
        if (this.pid === 1) {
            registry.release(PARCHIS_GAME_KEY, this.name);
            this.log('Era host, registry liberado');
        }
        this._initGameState();
    }
}

module.exports = ParchisGhost;

if (require.main === module) {
    const N = parseInt(process.argv[2] || '3', 10);
    new ParchisGhost(0).start();
    for (let i = 1; i < N; i++) {
        setTimeout(() => new ParchisGhost(i).start(), 3000 + i * 1500);
    }
}
