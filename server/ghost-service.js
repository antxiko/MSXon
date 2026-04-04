'use strict';

// =============================================================================
// ghost-service.js — MSX Online Ghost Service
//
// Servicio persistente que mantiene ghosts activos en el servidor.
// Corre en el VPS junto a msx-gameserver.js como servicio systemd.
//
// Arranque:
//   node ghost-service.js
//
// Servicio systemd:
//   sudo systemctl start msx-ghost
// =============================================================================

const net = require('net');

const SERVER_IP   = '127.0.0.1'; // localhost — corre en el mismo VPS
const SERVER_PORT = 9876;
const AUTH_TOKEN  = Buffer.from(
    (process.env.MSX_AUTH_TOKEN || 'DEADBEEF'), 'hex'
);

const MAGIC = [0x46, 0x4D];

// ── Protocolo ─────────────────────────────────────────────────
const CMD = {
    PING:         0x01,
    AUTH:         0x10,
    AUTH_OK:      0x11,
    ROOM_CREATE:  0x20,
    ROOM_INFO:    0x23,
    ROOM_LEAVE:   0x22,
    STATE_UPDATE: 0x40,
    PLAYER_JOINED: 0x30,
    PLAYER_LEFT:  0x31,
};

const PIECE_NONE = 0, PIECE_WHITE = 1, PIECE_BLACK = 2;
const PIECE_WHITE_KING = 3, PIECE_BLACK_KING = 4;
const isWhite = p => p === PIECE_WHITE || p === PIECE_WHITE_KING;
const isBlack = p => p === PIECE_BLACK || p === PIECE_BLACK_KING;
const isKing = p => p === PIECE_WHITE_KING || p === PIECE_BLACK_KING;
const isEnemy = (a, b) => (isWhite(a) && isBlack(b)) || (isBlack(a) && isWhite(b));

function buildPacket(cmd, roomId, pid, payload) {
    payload = payload || Buffer.alloc(0);
    const pkt = Buffer.alloc(6 + payload.length);
    pkt[0] = 0x46; pkt[1] = 0x4D;
    pkt[2] = cmd; pkt[3] = roomId || 0; pkt[4] = pid || 0;
    pkt[5] = payload.length;
    payload.copy(pkt, 6);
    return pkt;
}

function initBoard() {
    const b = Array.from({length: 8}, () => new Array(8).fill(0));
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
                                if (fe) captures.push({fx: x, fy: y, tx: cx, ty: cy, cap: true});
                                else moves.push({fx: x, fy: y, tx: cx, ty: cy, cap: false});
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
                        moves.push({fx: x, fy: y, tx, ty, cap: false});
                }
                for (const dy of [-1, 1]) {
                    for (const dx of [-1, 1]) {
                        const mx = x + dx, my = y + dy;
                        const tx = x + dx * 2, ty = y + dy * 2;
                        if (tx >= 0 && tx < 8 && ty >= 0 && ty < 8 &&
                            board[ty][tx] === PIECE_NONE && isEnemy(p, board[my][mx]))
                            captures.push({fx: x, fy: y, tx, ty, cap: true});
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

// ── Ghost de Damas ────────────────────────────────────────────

function startDamasGhost() {
    const sock = new net.Socket();
    let board = initBoard();
    let turn = PIECE_WHITE;
    let myColor = PIECE_WHITE;
    let roomId = 0, pid = 0;
    let gameStarted = false;
    let recvBuf = Buffer.alloc(0);
    let moveCount = 0;
    let multiCapX = -1, multiCapY = -1, multiCapCount = 0;
    let interval = null;
    let pingCounter = 0;

    function log(msg) {
        const d = new Date();
        const ts = d.toTimeString().substring(0, 8);
        console.log(`[${ts}] ${msg}`);
    }

    function resetBoard() {
        board = initBoard();
        turn = PIECE_WHITE;
        gameStarted = false;
        moveCount = 0;
        multiCapX = -1;
        multiCapY = -1;
        multiCapCount = 0;
        log('Tablero reiniciado. Esperando rival...');
    }

    function processPacket(cmd, payload, len) {
        if (cmd === CMD.ROOM_INFO) {
            roomId = payload[0];
            pid = payload[3];
            myColor = (pid === 1) ? PIECE_WHITE : PIECE_BLACK;
            log(`Sala 0x${roomId.toString(16).padStart(2, '0')} | PID=${pid} | ${myColor === PIECE_WHITE ? 'BLANCAS' : 'NEGRAS'}`);
        }
        else if (cmd === CMD.PLAYER_JOINED) {
            gameStarted = true;
            log('Rival conectado! Partida empieza.');
        }
        else if (cmd === CMD.PLAYER_LEFT) {
            log('Rival desconectado.');
            resetBoard();
        }
        else if (cmd === CMD.STATE_UPDATE && len >= 5) {
            const fx = payload[0], fy = payload[1], tx = payload[2], ty = payload[3];
            const endTurn = payload[4];
            if (fx === tx && fy === ty) return; // Ignorar nulos
            if (fx >= 8 || fy >= 8 || tx >= 8 || ty >= 8) return;
            executeMove(board, fx, fy, tx, ty);
            if (endTurn) {
                turn = (turn === PIECE_WHITE) ? PIECE_BLACK : PIECE_WHITE;
                log(`Oponente: (${fx},${fy})->(${tx},${ty}) | Turno: ${turn === PIECE_WHITE ? 'B' : 'N'}`);
            } else {
                log(`Oponente: (${fx},${fy})->(${tx},${ty}) MULTI...`);
            }
        }
    }

    // Conectar
    sock.connect(SERVER_PORT, SERVER_IP, () => {
        log('Conectado al servidor');
        sock.write(buildPacket(CMD.AUTH, 0, 0, AUTH_TOKEN));
    });

    sock.on('data', (chunk) => {
        recvBuf = Buffer.concat([recvBuf, chunk]);
        while (recvBuf.length >= 6) {
            const idx = recvBuf.indexOf(Buffer.from([0x46, 0x4D]));
            if (idx < 0) { recvBuf = Buffer.alloc(0); break; }
            if (idx > 0) { recvBuf = recvBuf.subarray(idx); continue; }
            const len = recvBuf[5];
            if (recvBuf.length < 6 + len) break;
            const cmd = recvBuf[2];
            const payload = Buffer.from(recvBuf.subarray(6, 6 + len));
            recvBuf = recvBuf.subarray(6 + len);

            if (cmd === CMD.AUTH_OK) {
                log('Auth OK');
                // Crear sala damas
                sock.write(buildPacket(CMD.ROOM_CREATE, 0, 0, Buffer.from([0x02, 2, 0x01])));
                log('Sala damas creada. Esperando rival...');
            }
            else {
                processPacket(cmd, payload, len);
            }
        }
    });

    sock.on('error', (err) => {
        if (err.code !== 'ECONNRESET')
            log(`Error: ${err.message}`);
    });

    sock.on('close', () => {
        log('Desconectado. Reconectando en 5s...');
        if (interval) clearInterval(interval);
        setTimeout(startDamasGhost, 5000);
    });

    // Game loop
    interval = setInterval(() => {
        // Ping
        pingCounter++;
        if (pingCounter >= 10) {
            pingCounter = 0;
            if (!sock.destroyed) sock.write(buildPacket(CMD.PING, roomId, pid));
        }

        if (!gameStarted) return;
        if (turn !== myColor) return;

        // Buscar movimientos
        let validMoves;
        if (multiCapX >= 0) {
            validMoves = findMoves(board, myColor).filter(
                m => m.fx === multiCapX && m.fy === multiCapY && m.cap === true
            );
            if (validMoves.length === 0) {
                // Fin multi-captura
                multiCapX = -1; multiCapY = -1; multiCapCount = 0;
                const sendPl = Buffer.from([multiCapX, multiCapY, multiCapX, multiCapY, 1, 0, 0, 0]);
                // No enviar nada, solo cambiar turno
                turn = (turn === PIECE_WHITE) ? PIECE_BLACK : PIECE_WHITE;
                log(`Fin multi-captura | Turno: ${turn === PIECE_WHITE ? 'B' : 'N'}`);
                return;
            }
        } else {
            validMoves = findMoves(board, myColor);
        }

        if (validMoves.length === 0) {
            // Sin movimientos — perdido
            let myPieces = 0;
            for (let r = 0; r < 8; r++)
                for (let c = 0; c < 8; c++)
                    if ((myColor === PIECE_WHITE && isWhite(board[r][c])) ||
                        (myColor === PIECE_BLACK && isBlack(board[r][c]))) myPieces++;
            if (myPieces === 0) log('Ghost ha perdido — sin fichas');
            else log('Ghost ha perdido — sin movimientos');
            resetBoard();
            return;
        }

        // Elegir movimiento
        const move = validMoves[Math.floor(Math.random() * validMoves.length)];
        executeMove(board, move.fx, move.fy, move.tx, move.ty);
        moveCount++;

        let endTurnFlag = 1;
        if (move.cap) {
            multiCapCount++;
            if (multiCapCount <= 12) {
                const moreCaps = findMoves(board, myColor).filter(
                    m => m.fx === move.tx && m.fy === move.ty && m.cap === true
                );
                if (moreCaps.length > 0) {
                    multiCapX = move.tx;
                    multiCapY = move.ty;
                    endTurnFlag = 0;
                }
            }
        }

        const pl = Buffer.from([move.fx, move.fy, move.tx, move.ty, endTurnFlag, 0, 0, 0]);
        if (!sock.destroyed) sock.write(buildPacket(CMD.STATE_UPDATE, roomId, pid, pl));
        log(`Ghost: (${move.fx},${move.fy})->(${move.tx},${move.ty})${move.cap ? ' CAP' : ''} #${moveCount}${endTurnFlag ? '' : ' MULTI...'}`);

        if (endTurnFlag) {
            multiCapX = -1; multiCapY = -1; multiCapCount = 0;
            turn = (turn === PIECE_WHITE) ? PIECE_BLACK : PIECE_WHITE;
            log(`Turno: ${turn === PIECE_WHITE ? 'BLANCAS' : 'NEGRAS'}`);
        }
    }, 1500);
}

// ── Ghost de Burdyn ───────────────────────────────────────────

let burdynRoomId = 0; // Compartido entre todos los ghosts Burdyn

function startBurdynGhost(idx) {
    idx = idx || 0;
    const sock = new net.Socket();
    let roomId = 0, pid = 0;
    let recvBuf = Buffer.alloc(0);
    let x = 3 + idx * 5, y = 3 + idx * 4;
    let dx = (idx % 2 === 0) ? 1 : -1;
    let dy = (idx % 3 === 0) ? -1 : 1;
    let interval = null;
    let pingCounter = 0;
    let gameStarted = false;

    function log(msg) {
        const d = new Date();
        const ts = d.toTimeString().substring(0, 8);
        console.log(`[${ts}] [BURDYN#${idx}] ${msg}`);
    }

    sock.connect(SERVER_PORT, SERVER_IP, () => {
        log('Conectado al servidor');
        sock.write(buildPacket(CMD.AUTH, 0, 0, AUTH_TOKEN));
    });

    sock.on('data', (chunk) => {
        recvBuf = Buffer.concat([recvBuf, chunk]);
        while (recvBuf.length >= 6) {
            const idx = recvBuf.indexOf(Buffer.from([0x46, 0x4D]));
            if (idx < 0) { recvBuf = Buffer.alloc(0); break; }
            if (idx > 0) { recvBuf = recvBuf.subarray(idx); continue; }
            const len = recvBuf[5];
            if (recvBuf.length < 6 + len) break;
            const cmd = recvBuf[2];
            const payload = Buffer.from(recvBuf.subarray(6, 6 + len));
            recvBuf = recvBuf.subarray(6 + len);

            if (cmd === CMD.AUTH_OK) {
                log('Auth OK');
                if (idx === 0) {
                    // Primer ghost: crear sala
                    sock.write(buildPacket(CMD.ROOM_CREATE, 0, 0, Buffer.from([0x03, 14, 0x02])));
                } else if (burdynRoomId > 0) {
                    // Unirse a la sala del primer ghost
                    log(`Uniendose a sala 0x${burdynRoomId.toString(16).padStart(2, '0')}`);
                    sock.write(buildPacket(0x21, 0, 0, Buffer.from([burdynRoomId])));
                } else {
                    // Sala aun no creada, esperar y reintentar
                    log('Sala aun no creada, reintentando en 3s...');
                    setTimeout(() => {
                        if (burdynRoomId > 0) {
                            sock.write(buildPacket(0x21, 0, 0, Buffer.from([burdynRoomId])));
                        }
                    }, 3000);
                }
            }
            else if (cmd === CMD.ROOM_INFO) {
                roomId = payload[0];
                pid = payload[3];
                burdynRoomId = roomId; // Compartir con otros ghosts
                log(`Sala 0x${roomId.toString(16).padStart(2, '0')} | PID=${pid}`);
                // Enviar GAME_START para activar tick AGGREGATE
                if (pid === 1) {
                    sock.write(buildPacket(0x32, roomId, pid));
                    log('GAME_START enviado');
                }
                gameStarted = true;
            }
            else if (cmd === CMD.PLAYER_JOINED) {
                log('Jugador conectado!');
            }
            else if (cmd === CMD.PLAYER_LEFT) {
                log('Jugador desconectado.');
            }
        }
    });

    sock.on('error', (err) => {
        if (err.code !== 'ECONNRESET') log(`Error: ${err.message}`);
    });

    sock.on('close', () => {
        log('Desconectado. Reconectando en 5s...');
        if (interval) clearInterval(interval);
        setTimeout(() => startBurdynGhost(idx), 5000);
    });

    // Movimiento: rebote diagonal por el mapa 64x64
    interval = setInterval(() => {
        pingCounter++;
        if (pingCounter >= 10) {
            pingCounter = 0;
            if (!sock.destroyed) sock.write(buildPacket(CMD.PING, roomId, pid));
        }

        if (!gameStarted) return;

        x += dx; y += dy;
        if (x <= 1 || x >= 62) dx = -dx;
        if (y <= 1 || y >= 62) dy = -dy;
        if (x < 1) x = 1; if (x > 62) x = 62;
        if (y < 1) y = 1; if (y > 62) y = 62;

        const pl = Buffer.from([x, y, 0, 100, 0, 0, 1, 0]);
        if (!sock.destroyed) sock.write(buildPacket(CMD.STATE_UPDATE, roomId, pid, pl));
    }, 250); // 4 FPS
}

// ── Arranque ──────────────────────────────────────────────────

// Parametros: node ghost-service.js [num_burdyn_ghosts]
const NUM_BURDYN = parseInt(process.argv[2] || '3', 10);

console.log('MSX Online Ghost Service v1.0');
console.log(`Iniciando: 1 ghost damas + ${NUM_BURDYN} ghost(s) burdyn\n`);
startDamasGhost();
for (let i = 0; i < NUM_BURDYN; i++) {
    // Delay escalonado para que no se conecten todos a la vez
    setTimeout(() => startBurdynGhost(i), i * 2000);
}
