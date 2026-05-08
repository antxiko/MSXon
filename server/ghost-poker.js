'use strict';

// =============================================================================
// ghost-poker.js — Ghost de Poker (Texas Hold'em) migrado a GhostBase
//
// Reproduce fielmente el original:
//  - 1 sola instancia (host=self)
//  - gameId=0x05, maxPlayers=6, protoVer=0x01
//  - SIN game tick: solo responde a eventos (DEAL_HOLE, ACTION_PROMPT)
//  - Ping cada 15s (delegado a GhostBase, default)
//  - Cuando entra humano (PID>=2), espera 3s y manda GAME_START
//  - Decide acción simple según cartas: fold/check/call/raise
// =============================================================================

const { GhostBase, CMD } = require('./ghost-base');

const GAME_ID_POKER = 0x05;
const POKER_MAX     = 6;

// Acciones (codificadas en byte 0 del payload de respuesta)
const POKER_FOLD = 0, POKER_CHECK = 1, POKER_CALL = 2, POKER_RAISE = 3, POKER_ALLIN = 4;

class PokerGhost extends GhostBase {
    constructor() {
        super({
            name: 'POKER-BOT',
            gameId: GAME_ID_POKER,
            maxPlayers: POKER_MAX,
            // Sin tickMs: este ghost no tiene game loop
        });
        this.gameStarted = false;
        this.myCards = [0, 0];
        this.myTurn = false;
        this.currentBet = 0;
    }

    onAuthOk() {
        this.sendRaw(CMD.ROOM_CREATE, 0, 0,
            Buffer.from([this.gameId, this.maxPlayers, this.protoVer]));
    }

    onRoomInfo(payload, len) {
        // base ya guardo roomId/pid
        this.log(`Sala ${this.roomId}, PID ${this.pid}`);
    }

    onPacket(cmd, payload, len) {
        if (cmd === CMD.PLAYER_JOINED) {
            const joinedPid = payload[0];
            this.log(`Player joined PID ${joinedPid}`);
            // Si yo soy host (pid==1) y entra un humano (pid>=2), arrancar partida en 3s
            if (this.pid === 1 && joinedPid >= 2) {
                this.log('Humano conectado! Empezando en 3s...');
                this.gameStarted = false;
                this.setTimer(() => {
                    this.send(CMD.GAME_START);
                    this.gameStarted = true;
                    this.myCards = [0, 0];
                    this.log('GAME_START enviado');
                }, 3000);
            }
            return;
        }
        if (cmd === CMD.GAME_START) {
            this.gameStarted = true;
            this.log('Partida iniciada');
            return;
        }
        if (cmd === CMD.STATE_UPDATE && len >= 1) {
            const pkt = payload[0];

            if (pkt === 0x02 && len >= 3) {
                // DEAL_HOLE: 2 cartas
                this.myCards[0] = payload[1];
                this.myCards[1] = payload[2];
                this.log(`Cartas: ${this.myCards[0]} ${this.myCards[1]}`);
                return;
            }
            if (pkt === 0x04 && len >= 8) {
                // ACTION_PROMPT
                const actionSeat = payload[1];
                this.currentBet = (payload[2] << 8) | payload[3];
                if (actionSeat === this.pid - 1) {
                    this.myTurn = true;
                    // Responder tras delay 1-3s (humano-like)
                    const delay = 1000 + Math.floor(Math.random() * 2000);
                    this.setTimer(() => {
                        if (this.myTurn) {
                            const d = this._decide();
                            const pl = Buffer.from([d.action, (d.amount >> 8) & 0xFF, d.amount & 0xFF]);
                            if (this.send(CMD.STATE_UPDATE, pl)) {
                                this.myTurn = false;
                                this.log(`Accion: ${['FOLD', 'CHECK', 'CALL', 'RAISE', 'ALLIN'][d.action]}`);
                            }
                        }
                    }, delay);
                }
            }
        }
    }

    _cardStrength(c) {
        const v = c & 0x0F;
        return (v === 1) ? 14 : v; // Ace alto
    }

    _decide() {
        const s1 = this._cardStrength(this.myCards[0]);
        const s2 = this._cardStrength(this.myCards[1]);
        const best = Math.max(s1, s2);
        const pair = (s1 === s2);
        const r = Math.random();

        if (pair && best >= 10) return { action: POKER_RAISE, amount: 40 };
        if (pair) return { action: (r < 0.5) ? POKER_RAISE : POKER_CALL, amount: 20 };
        if (best >= 12) return { action: (this.currentBet > 0) ? POKER_CALL : POKER_CHECK, amount: 0 };
        if (best >= 8) {
            if (this.currentBet > 0) return (r < 0.6) ? { action: POKER_CALL, amount: 0 } : { action: POKER_FOLD, amount: 0 };
            return { action: POKER_CHECK, amount: 0 };
        }
        // Mano débil
        if (this.currentBet > 0) return (r < 0.3) ? { action: POKER_CALL, amount: 0 } : { action: POKER_FOLD, amount: 0 };
        return { action: POKER_CHECK, amount: 0 };
    }

    onShutdown() {
        this.gameStarted = false;
        this.myTurn = false;
    }
}

module.exports = PokerGhost;

if (require.main === module) {
    new PokerGhost().start();
}
