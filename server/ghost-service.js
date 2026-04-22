'use strict';

// =============================================================================
// ghost-service.js — MSXon Ghost Service
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

// Global error handlers — prevent a single ghost crash from killing the whole service
process.on('uncaughtException', (err) => {
    console.error('[FATAL] uncaughtException:', err && err.stack || err);
});
process.on('unhandledRejection', (err) => {
    console.error('[FATAL] unhandledRejection:', err && err.stack || err);
});

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
            const magicPos = recvBuf.indexOf(Buffer.from([0x46, 0x4D]));
            if (magicPos < 0) { recvBuf = Buffer.alloc(0); break; }
            if (magicPos > 0) { recvBuf = recvBuf.subarray(magicPos); continue; }
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

function startBurdynGhost(ghostNum) {
    const idx = (ghostNum !== undefined) ? ghostNum : 0;
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
            const magicPos = recvBuf.indexOf(Buffer.from([0x46, 0x4D]));
            if (magicPos < 0) { recvBuf = Buffer.alloc(0); break; }
            if (magicPos > 0) { recvBuf = recvBuf.subarray(magicPos); continue; }
            const len = recvBuf[5];
            if (recvBuf.length < 6 + len) break;
            const cmd = recvBuf[2];
            const payload = Buffer.from(recvBuf.subarray(6, 6 + len));
            recvBuf = recvBuf.subarray(6 + len);

            if (cmd === CMD.AUTH_OK) {
                log(`Auth OK (idx=${idx}, burdynRoomId=${burdynRoomId})`);
                if (idx === 0) {
                    sock.write(buildPacket(CMD.ROOM_CREATE, 0, 0, Buffer.from([0x03, 14, 0x01])));
                } else if (burdynRoomId > 0) {
                    log(`JOIN sala 0x${burdynRoomId.toString(16).padStart(2, '0')}`);
                    sock.write(buildPacket(0x21, 0, 0, Buffer.from([burdynRoomId])));
                } else {
                    log('ERROR: burdynRoomId=0, creando sala propia');
                    sock.write(buildPacket(CMD.ROOM_CREATE, 0, 0, Buffer.from([0x03, 14, 0x01])));
                }
            }
            else if (cmd === CMD.ROOM_INFO) {
                roomId = payload[0];
                pid = payload[3];
                if (idx === 0) burdynRoomId = roomId;
                log(`Sala 0x${roomId.toString(16).padStart(2, '0')} | PID=${pid} | burdynRoomId=${burdynRoomId}`);
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
        setTimeout(() => startBurdynGhost(ghostNum), 5000);
    });

    // Movimiento: rebote diagonal por el mapa 64x64
    interval = setInterval(() => {
        pingCounter++;
        if (pingCounter >= 38) { // ~5 seconds at 133ms
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
    }, 133); // ~7-8 moves/sec (matches MSX MOVE_JIFFIES=8)
}

// =============================================================================
// TETRIS GHOST — 3 bots jugando tetris en una sala
// =============================================================================

const GAME_ID_TETRIS = 0x06;
const TET_BW = 8, TET_BH = 20, TET_NUM_PIECES = 7;
const ACT_MOVE = 0, ACT_LOCK = 1, ACT_DEAD = 3;

// Piece data (same as client)
const TET_PIECES = [
    // I
    {n:2, r:[{w:4,h:1,b:0xF000},{w:1,h:4,b:0x8888}]},
    // O
    {n:1, r:[{w:2,h:2,b:0xCC00}]},
    // T
    {n:4, r:[{w:3,h:2,b:0x4E00},{w:2,h:3,b:0x8C80},{w:3,h:2,b:0xE400},{w:2,h:3,b:0x4C40}]},
    // S
    {n:2, r:[{w:3,h:2,b:0x6C00},{w:2,h:3,b:0x8C40}]},
    // Z
    {n:2, r:[{w:3,h:2,b:0xC600},{w:2,h:3,b:0x4C80}]},
    // L
    {n:4, r:[{w:2,h:3,b:0x88C0},{w:3,h:2,b:0xE800},{w:2,h:3,b:0xC440},{w:3,h:2,b:0x2E00}]},
    // J
    {n:4, r:[{w:2,h:3,b:0x44C0},{w:3,h:2,b:0x8E00},{w:2,h:3,b:0xC880},{w:3,h:2,b:0xE200}]},
];

function tetBit(bits, row, col) {
    const mask = [
        [0x8000,0x4000,0x2000,0x1000],
        [0x0800,0x0400,0x0200,0x0100],
        [0x0080,0x0040,0x0020,0x0010],
        [0x0008,0x0004,0x0002,0x0001],
    ];
    return (bits & mask[row][col]) ? 1 : 0;
}

function tetGetRot(pi, rot) {
    return TET_PIECES[pi].r[rot % TET_PIECES[pi].n];
}

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
    // Drop piece to bottom
    let dy = py;
    while (tetValid(board, pi, rot, px, dy + 1)) dy++;
    // Place on tmp board
    const tmp = board.map(r => [...r]);
    const s = tetGetRot(pi, rot);
    for (let r = 0; r < s.h; r++)
        for (let c = 0; c < s.w; c++)
            if (tetBit(s.b, r, c)) {
                const by = dy + r, bx = px + c;
                if (by >= 0 && by < TET_BH && bx >= 0 && bx < TET_BW) tmp[by][bx] = 1;
            }
    // Score: lines*200 - holes*150 - bumpiness*20 - maxHeight*10
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

let tetrisRoomId = 0;

function startTetrisGhost(ghostNum) {
    const log = (msg) => console.log(`[TETRIS#${ghostNum}] ${msg}`);
    const sock = new net.Socket();
    let recvBuf = Buffer.alloc(0);
    let roomId = 0, pid = 0, gameStarted = false;
    let interval = null, pingCounter = 0;

    // Board + piece state
    const board = Array.from({length: TET_BH}, () => new Array(TET_BW).fill(0));
    let pi = Math.floor(Math.random() * TET_NUM_PIECES);
    let nxt = Math.floor(Math.random() * TET_NUM_PIECES);
    let rot = 0, px = 3, py = 0;
    let dead = false;

    function spawn() {
        pi = nxt;
        nxt = Math.floor(Math.random() * TET_NUM_PIECES);
        rot = 0; px = 3; py = 0;
        if (!tetValid(board, pi, rot, px, py)) {
            dead = true;
            sendDead();
        }
    }

    function lockPiece() {
        const s = tetGetRot(pi, rot);
        for (let r = 0; r < s.h; r++)
            for (let c = 0; c < s.w; c++)
                if (tetBit(s.b, r, c)) {
                    const by = py + r, bx = px + c;
                    if (by >= 0 && by < TET_BH && bx >= 0 && bx < TET_BW) board[by][bx] = pid;
                }
        // Find full lines
        let linesCleared = 0;
        for (let r = TET_BH - 1; r >= 0; r--) {
            let full = true;
            for (let c = 0; c < TET_BW; c++) if (!board[r][c]) { full = false; break; }
            if (full) {
                linesCleared++;
                for (let r2 = r; r2 > 0; r2--)
                    for (let c = 0; c < TET_BW; c++) board[r2][c] = board[r2-1][c];
                for (let c = 0; c < TET_BW; c++) board[0][c] = 0;
                r++; // recheck this row
            }
        }
        const garbT = [0, 0, 1, 2, 4];
        const garb = garbT[Math.min(linesCleared, 4)];

        sendPieceLock(linesCleared);
        spawn();
    }

    let aiTargetX = 3, aiTargetRot = 0, aiComputed = false;

    function aiCompute() {
        // Find best position
        let best = { score: -99999, x: px, rot: 0 };
        const numRots = TET_PIECES[pi].n;
        for (let ri = 0; ri < numRots; ri++) {
            for (let xi = -1; xi < TET_BW; xi++) {
                if (!tetValid(board, pi, ri, xi, py)) continue;
                const ev = tetEval(board, pi, ri, xi, py);
                if (ev.score > best.score) best = { score: ev.score, x: xi, rot: ri };
            }
        }
        aiTargetX = best.x;
        aiTargetRot = best.rot;
        aiComputed = true;
    }

    function aiTick() {
        if (dead || !gameStarted) return;

        if (!aiComputed) aiCompute();

        // Rotate + move X instantly (all in one tick)
        if (rot !== aiTargetRot) {
            const nr = aiTargetRot;
            if (tetValid(board, pi, nr, px, py)) rot = nr;
            else aiTargetRot = rot;
        }
        while (px < aiTargetX && tetValid(board, pi, rot, px + 1, py)) px++;
        while (px > aiTargetX && tetValid(board, pi, rot, px - 1, py)) px--;

        // Drop 2 rows per tick (faster play)
        let dropped = 0;
        while (dropped < 2 && tetValid(board, pi, rot, px, py + 1)) {
            py++;
            dropped++;
        }

        // At bottom: lock
        if (!tetValid(board, pi, rot, px, py + 1)) {
            lockPiece();
            aiComputed = false;
        }

        // Send full board state
        sendFullSync();
    }

    let syncCounter = 0;

    function sendPiecePos() {
        if (sock.destroyed) return;
        sock.write(buildPacket(CMD.STATE_UPDATE, roomId, pid,
            Buffer.from([0x01, pi, rot, px & 0xFF, py & 0xFF])));
    }

    function sendPieceLock(linesCleared) {
        if (sock.destroyed) return;
        sock.write(buildPacket(CMD.STATE_UPDATE, roomId, pid,
            Buffer.from([0x02, pi, rot, px & 0xFF, py & 0xFF, linesCleared])));
    }

    function sendFullSync() {
        if (sock.destroyed) return;
        const payload = Buffer.alloc(86);
        payload[0] = 0x05; // PKT_FULL_SYNC
        payload[1] = pi;
        payload[2] = rot;
        payload[3] = px & 0xFF;
        payload[4] = py & 0xFF;
        payload[5] = dead ? 1 : 0;
        let idx = 6;
        for (let r = 0; r < TET_BH; r++)
            for (let c = 0; c < TET_BW; c += 2) {
                payload[idx] = ((board[r][c] & 0x0F) << 4) | (board[r][c+1] & 0x0F);
                idx++;
            }
        sock.write(buildPacket(CMD.STATE_UPDATE, roomId, pid, payload));
    }

    function sendDead() {
        if (sock.destroyed) return;
        sock.write(buildPacket(CMD.STATE_UPDATE, roomId, pid, Buffer.from([0x04])));
    }

    sock.connect(SERVER_PORT, SERVER_IP, () => {
        log('Conectado');
        sock.write(buildPacket(CMD.AUTH, 0, 0, AUTH_TOKEN));
    });

    sock.on('data', (chunk) => {
        recvBuf = Buffer.concat([recvBuf, chunk]);

        while (recvBuf.length >= 6) {
            let magicPos = -1;
            for (let i = 0; i <= recvBuf.length - 2; i++) {
                if (recvBuf[i] === 0x46 && recvBuf[i+1] === 0x4D) { magicPos = i; break; }
            }
            if (magicPos < 0) { recvBuf = Buffer.alloc(0); break; }
            if (magicPos > 0) recvBuf = recvBuf.slice(magicPos);
            if (recvBuf.length < 6) break;

            const cmd = recvBuf[2];
            const payloadLen = recvBuf[5];
            const totalLen = 6 + payloadLen;
            if (recvBuf.length < totalLen) break;

            const payload = recvBuf.slice(6, totalLen);
            recvBuf = recvBuf.slice(totalLen);

            if (cmd === CMD.AUTH_OK) {
                log('Auth OK');
                if (ghostNum === 0) {
                    // Create room
                    sock.write(buildPacket(CMD.ROOM_CREATE, 0, 0,
                        Buffer.from([GAME_ID_TETRIS, 4, 0x01])));
                } else {
                    // Join existing room
                    if (tetrisRoomId > 0) {
                        sock.write(buildPacket(0x21, 0, 0, Buffer.from([tetrisRoomId])));
                    }
                }
            }
            else if (cmd === CMD.ROOM_INFO) {
                roomId = payload[0];
                pid = payload[3];
                if (ghostNum === 0) tetrisRoomId = roomId;
                log(`Sala ${roomId}, PID ${pid}`);
            }
            else if (cmd === CMD.PLAYER_JOINED) {
                const joinedPid = payload[0];
                log(`Player joined PID ${joinedPid}`);
                // Ghost #0: when 4th player joins (sala completa), wait 3s and start
                if (ghostNum === 0 && joinedPid === 4 && !gameStarted) {
                    log('Sala completa! Empezando en 3s...');
                    setTimeout(() => {
                        if (!sock.destroyed && !gameStarted) {
                            sock.write(buildPacket(0x32, roomId, pid));
                            // Align local state with what cmd===0x32 handler does
                            gameStarted = true;
                            aiComputed = false;
                            for (let r = 0; r < TET_BH; r++)
                                for (let c = 0; c < TET_BW; c++) board[r][c] = 0;
                            dead = false;
                            spawn();
                            log('GAME_START enviado');
                        }
                    }, 3000);
                }
            }
            else if (cmd === 0x32) { // GAME_START
                gameStarted = true;
                aiComputed = false;
                for (let r = 0; r < TET_BH; r++)
                    for (let c = 0; c < TET_BW; c++) board[r][c] = 0;
                dead = false;
                spawn();
                log('Partida iniciada');
            }
            else if (cmd === CMD.STATE_UPDATE && payloadLen >= 4 && payload[0] === 0x03) {
                // PKT_GARBAGE: targetSlot, count, gapCol
                const garbTarget = payload[1];
                const garbCount = payload[2];
                const garbGap = payload[3];
                if (garbTarget === pid - 1) { // only apply if targeted at me
                    // Apply garbage to MY board
                    for (let g = 0; g < garbCount; g++) {
                        // Check top row
                        let topBlocked = false;
                        for (let c = 0; c < TET_BW; c++) if (board[0][c]) { topBlocked = true; break; }
                        if (topBlocked) { dead = true; break; }
                        // Shift up
                        for (let r = 0; r < TET_BH - 1; r++)
                            for (let c = 0; c < TET_BW; c++) board[r][c] = board[r + 1][c];
                        // Fill bottom with garbage, gap from packet
                        for (let c = 0; c < TET_BW; c++) board[TET_BH - 1][c] = (c === garbGap) ? 0 : 5;
                    }
                    aiComputed = false; // recalculate after garbage
                    log(`Recibido ${garbCount} filas de garbage`);
                }
            }
        }
    });

    sock.on('error', (err) => {
        if (err.code !== 'ECONNRESET') log(`Error: ${err.message}`);
    });

    sock.on('close', () => {
        log('Desconectado. Reconectando en 5s...');
        if (interval) clearInterval(interval);
        setTimeout(() => startTetrisGhost(ghostNum), 5000);
    });

    // Game loop: AI makes a move every 1.5s, ping every ~15s
    interval = setInterval(() => {
        pingCounter++;
        if (pingCounter >= 5) {
            pingCounter = 0;
            if (!sock.destroyed) sock.write(buildPacket(CMD.PING, roomId, pid));
        }
        if (!gameStarted || dead) return;

        aiTick();
    }, 500); // 2 ticks per second
}

// =============================================================================
// POKER GHOST — bot que se une a salas de poker y juega
// =============================================================================

const GAME_ID_POKER = 0x05;

function startPokerGhost() {
    const log = (msg) => console.log(`[POKER-BOT] ${msg}`);
    const sock = new net.Socket();
    let recvBuf = Buffer.alloc(0);
    let roomId = 0, pid = 0;
    let interval = null, pingCounter = 0;
    let myCards = [0, 0];
    let gameStarted = false;
    let myTurn = false;
    let currentBet = 0;

    // Simple AI: decide based on cards
    function cardStrength(c) {
        const v = c & 0x0F;
        return (v === 1) ? 14 : v; // Ace = 14
    }

    function decide() {
        const s1 = cardStrength(myCards[0]);
        const s2 = cardStrength(myCards[1]);
        const best = Math.max(s1, s2);
        const pair = (s1 === s2);
        const r = Math.random();

        if (pair && best >= 10) return { action: 3, amount: 40 }; // raise
        if (pair) return { action: (r < 0.5) ? 3 : 2, amount: 20 }; // raise or call
        if (best >= 12) return { action: (currentBet > 0) ? 2 : 1, amount: 0 }; // call or check
        if (best >= 8) {
            if (currentBet > 0) return (r < 0.6) ? { action: 2, amount: 0 } : { action: 0, amount: 0 };
            return { action: 1, amount: 0 }; // check
        }
        // Weak hand
        if (currentBet > 0) return (r < 0.3) ? { action: 2, amount: 0 } : { action: 0, amount: 0 };
        return { action: 1, amount: 0 }; // check
    }

    sock.connect(SERVER_PORT, SERVER_IP, () => {
        log('Conectado');
        sock.write(buildPacket(CMD.AUTH, 0, 0, AUTH_TOKEN));
    });

    sock.on('data', (chunk) => {
        recvBuf = Buffer.concat([recvBuf, chunk]);

        while (recvBuf.length >= 6) {
            let magicPos = -1;
            for (let i = 0; i <= recvBuf.length - 2; i++) {
                if (recvBuf[i] === 0x46 && recvBuf[i+1] === 0x4D) { magicPos = i; break; }
            }
            if (magicPos < 0) { recvBuf = Buffer.alloc(0); break; }
            if (magicPos > 0) recvBuf = recvBuf.slice(magicPos);
            if (recvBuf.length < 6) break;

            const cmd = recvBuf[2];
            const payloadLen = recvBuf[5];
            const totalLen = 6 + payloadLen;
            if (recvBuf.length < totalLen) break;

            const payload = recvBuf.slice(6, totalLen);
            recvBuf = recvBuf.slice(totalLen);

            if (cmd === CMD.AUTH_OK) {
                log('Auth OK');
                // Create poker room
                sock.write(buildPacket(CMD.ROOM_CREATE, 0, 0,
                    Buffer.from([GAME_ID_POKER, 6, 0x01])));
            }
            else if (cmd === 0x23) { // ROOM_INFO
                roomId = payload[0];
                pid = payload[3];
                log(`Sala ${roomId}, PID ${pid}`);
            }
            else if (cmd === 0x30) { // PLAYER_JOINED
                const joinedPid = payload[0];
                log(`Player joined PID ${joinedPid}`);
                // When a human joins (PID 2+), wait 3s and start
                if (pid === 1 && joinedPid >= 2) {
                    log('Humano conectado! Empezando en 3s...');
                    gameStarted = false;
                    setTimeout(() => {
                        if (!sock.destroyed) {
                            sock.write(buildPacket(0x32, roomId, pid));
                            gameStarted = true;
                            myCards = [0, 0];
                            log('GAME_START enviado');
                        }
                    }, 3000);
                }
            }
            else if (cmd === 0x32) { // GAME_START
                gameStarted = true;
                log('Partida iniciada');
            }
            else if (cmd === CMD.STATE_UPDATE && payloadLen >= 1) {
                const pkt = payload[0];

                if (pkt === 0x02 && payloadLen >= 3) { // DEAL_HOLE
                    myCards[0] = payload[1];
                    myCards[1] = payload[2];
                    log(`Cartas: ${myCards[0]} ${myCards[1]}`);
                }
                else if (pkt === 0x04 && payloadLen >= 8) { // ACTION_PROMPT
                    const actionSeat = payload[1];
                    currentBet = (payload[2] << 8) | payload[3];
                    if (actionSeat === pid - 1) {
                        myTurn = true;
                        // Respond after short delay
                        setTimeout(() => {
                            if (!sock.destroyed && myTurn) {
                                const d = decide();
                                const pl = Buffer.from([d.action, (d.amount >> 8) & 0xFF, d.amount & 0xFF]);
                                sock.write(buildPacket(CMD.STATE_UPDATE, roomId, pid, pl));
                                myTurn = false;
                                log(`Accion: ${['FOLD','CHECK','CALL','RAISE','ALLIN'][d.action]}`);
                            }
                        }, 1000 + Math.floor(Math.random() * 2000));
                    }
                }
            }
        }
    });

    sock.on('error', (err) => {
        if (err.code !== 'ECONNRESET') log(`Error: ${err.message}`);
    });

    sock.on('close', () => {
        log('Desconectado. Reconectando en 5s...');
        if (interval) clearInterval(interval);
        gameStarted = false;
        myTurn = false;
        setTimeout(() => startPokerGhost(), 5000);
    });

    // Ping
    interval = setInterval(() => {
        pingCounter++;
        if (pingCounter >= 30) {
            pingCounter = 0;
            if (!sock.destroyed) sock.write(buildPacket(CMD.PING, roomId, pid));
        }
    }, 500);
}

// ── Parchis Ghost ─────────────────────────────────────────────

let parchisRoomId = 0;
const GAME_ID_PARCHIS = 0x04;

// Salida en el recorrido por jugador (0-indexed: P1, P2, P3, P4)
const PARCHIS_EXIT = [5, 22, 39, 56];
// Casilla antes de entrar al pasillo por jugador
const PARCHIS_ENTER_PASS = [68, 17, 34, 51];
const PARCHIS_PATH_LENGTH = 68;
const PARCHIS_PASSAGE_LENGTH = 8;

const PARCHIS_STATE_HOME = 0;
const PARCHIS_STATE_BOARD = 1;
const PARCHIS_STATE_PASSAGE = 2;
const PARCHIS_STATE_GOAL = 3;

function startParchisGhost(ghostNum) {
    const sock = new net.Socket();
    let roomId = 0, pid = 0;
    let mySlot = 0;
    let turn = 0;
    let gameStarted = false;
    let recvBuf = Buffer.alloc(0);
    let interval = null;
    let pingCounter = 0;
    let activePlayers = 0;
    // Estado de mis 4 fichas
    const pieces = [
        { state: PARCHIS_STATE_HOME, pos: 0 },
        { state: PARCHIS_STATE_HOME, pos: 0 },
        { state: PARCHIS_STATE_HOME, pos: 0 },
        { state: PARCHIS_STATE_HOME, pos: 0 },
    ];
    // Pending move (after dice)
    let pendingMove = null;

    function log(msg) {
        const d = new Date();
        const ts = d.toTimeString().substring(0, 8);
        console.log(`[${ts}] [PARCHIS#${ghostNum}] ${msg}`);
    }

    // Decide qué ficha mover con el dado. Devuelve {pieceIdx, newPos, newState} o null.
    function decideMove(dice) {
        // 1. Si saco 5 y tengo ficha en casa: sacar
        if (dice === 5) {
            for (let i = 0; i < 4; i++) {
                if (pieces[i].state === PARCHIS_STATE_HOME) {
                    return {
                        pieceIdx: i,
                        newPos: PARCHIS_EXIT[mySlot] - 1, // 0-indexed
                        newState: PARCHIS_STATE_BOARD,
                    };
                }
            }
        }
        // 2. Buscar ficha en tablero que pueda mover
        for (let i = 0; i < 4; i++) {
            if (pieces[i].state === PARCHIS_STATE_BOARD) {
                let newPos = pieces[i].pos + dice;
                const enterPass = PARCHIS_ENTER_PASS[mySlot] - 1;
                // Normalizar recorrido circular (68 casillas)
                const distToEnter = (enterPass - pieces[i].pos + PARCHIS_PATH_LENGTH) % PARCHIS_PATH_LENGTH;
                if (dice > distToEnter) {
                    // Entra al pasillo
                    const passPos = dice - distToEnter - 1;
                    if (passPos < PARCHIS_PASSAGE_LENGTH) {
                        if (passPos === PARCHIS_PASSAGE_LENGTH - 1) {
                            return { pieceIdx: i, newPos: passPos, newState: PARCHIS_STATE_GOAL };
                        }
                        return { pieceIdx: i, newPos: passPos, newState: PARCHIS_STATE_PASSAGE };
                    }
                    // Se pasaria, no puede mover esta ficha
                    continue;
                }
                newPos = newPos % PARCHIS_PATH_LENGTH;
                return { pieceIdx: i, newPos, newState: PARCHIS_STATE_BOARD };
            }
        }
        // 3. Ficha en pasillo
        for (let i = 0; i < 4; i++) {
            if (pieces[i].state === PARCHIS_STATE_PASSAGE) {
                const newPos = pieces[i].pos + dice;
                if (newPos < PARCHIS_PASSAGE_LENGTH) {
                    if (newPos === PARCHIS_PASSAGE_LENGTH - 1) {
                        return { pieceIdx: i, newPos, newState: PARCHIS_STATE_GOAL };
                    }
                    return { pieceIdx: i, newPos, newState: PARCHIS_STATE_PASSAGE };
                }
            }
        }
        return null; // No puedo mover
    }

    function takeTurn() {
        if (!gameStarted || sock.destroyed) return;
        if (turn !== mySlot) return;

        // Tirar dado
        const dice = 1 + Math.floor(Math.random() * 6);
        const endTurnDice = 0;
        const pl = Buffer.from([0, dice, 0, 0, endTurnDice, 0, 0, 0]);
        sock.write(buildPacket(CMD.STATE_UPDATE, roomId, pid, pl));

        setTimeout(() => {
            if (sock.destroyed) return;
            const mv = decideMove(dice);
            const endTurn = (dice === 6) ? 0 : 1;
            if (mv) {
                // Aplicar en mi estado local
                pieces[mv.pieceIdx].state = mv.newState;
                pieces[mv.pieceIdx].pos = mv.newPos;
                const movePl = Buffer.from([1, dice, mv.pieceIdx, mv.newPos, endTurn, mv.newState, 0, 0]);
                sock.write(buildPacket(CMD.STATE_UPDATE, roomId, pid, movePl));
                log(`Dado=${dice} pieza=${mv.pieceIdx} pos=${mv.newPos} estado=${mv.newState} endTurn=${endTurn}`);
            } else {
                // No puede mover: enviar move nulo con endTurn=1 (si no saco 6)
                const piece0 = pieces[0];
                const movePl = Buffer.from([1, dice, 0, piece0.pos, endTurn, piece0.state, 0, 0]);
                sock.write(buildPacket(CMD.STATE_UPDATE, roomId, pid, movePl));
                log(`Dado=${dice} sin movimientos`);
            }
            // Avanzar turno local si endTurn=1
            if (endTurn) {
                advanceTurn();
            } else {
                // Jugar otra vez (saque 6)
                setTimeout(() => takeTurn(), 2000);
            }
        }, 1500 + Math.floor(Math.random() * 1500));
    }

    function advanceTurn() {
        let next = (turn + 1) % 4;
        let tries = 0;
        while (!(activePlayers & (1 << next)) && tries < 4) {
            next = (next + 1) % 4;
            tries++;
        }
        turn = next;
        if (turn === mySlot) {
            setTimeout(() => takeTurn(), 1500);
        }
    }

    sock.connect(SERVER_PORT, SERVER_IP, () => {
        log('Conectado');
        sock.write(buildPacket(CMD.AUTH, 0, 0, AUTH_TOKEN));
    });

    sock.on('data', (chunk) => {
        recvBuf = Buffer.concat([recvBuf, chunk]);
        while (recvBuf.length >= 6) {
            const magicPos = recvBuf.indexOf(Buffer.from([0x46, 0x4D]));
            if (magicPos < 0) { recvBuf = Buffer.alloc(0); break; }
            if (magicPos > 0) { recvBuf = recvBuf.subarray(magicPos); continue; }
            const payloadLen = recvBuf[5];
            if (recvBuf.length < 6 + payloadLen) break;
            const cmd = recvBuf[2];
            const payload = Buffer.from(recvBuf.subarray(6, 6 + payloadLen));
            recvBuf = recvBuf.subarray(6 + payloadLen);

            if (cmd === CMD.AUTH_OK) {
                log('Auth OK');
                if (ghostNum === 0) {
                    // P1 crea sala
                    sock.write(buildPacket(CMD.ROOM_CREATE, 0, 0,
                        Buffer.from([GAME_ID_PARCHIS, 4, 0x01])));
                } else if (parchisRoomId > 0) {
                    sock.write(buildPacket(CMD.ROOM_JOIN, 0, 0,
                        Buffer.from([parchisRoomId])));
                } else {
                    // Fallback: no hay sala, crear una propia
                    log('parchisRoomId=0, creando sala propia');
                    sock.write(buildPacket(CMD.ROOM_CREATE, 0, 0,
                        Buffer.from([GAME_ID_PARCHIS, 4, 0x01])));
                }
            }
            else if (cmd === 0x23) { // ROOM_INFO
                roomId = payload[0];
                pid = payload[3];
                mySlot = pid - 1;
                // Update parchisRoomId if we're the host (ghost 0 or fallback host)
                if (pid === 1) parchisRoomId = roomId;
                // Set active players from payload[2]
                const np = payload[2];
                activePlayers = 0;
                for (let j = 0; j < np; j++) activePlayers |= (1 << j);
                log(`Sala ${roomId} PID=${pid} slot=${mySlot} activos=${np}`);
            }
            else if (cmd === 0x30) { // PLAYER_JOINED
                const joinedPid = payload[0];
                activePlayers |= (1 << (joinedPid - 1));
                log(`Player joined PID=${joinedPid}`);
                // Any ghost that is P1 (host) triggers game start when someone joins
                if (pid === 1 && joinedPid >= 2 && !gameStarted) {
                    log('Empezando en 3s...');
                    setTimeout(() => {
                        if (!sock.destroyed && !gameStarted) {
                            sock.write(buildPacket(0x32, roomId, pid));
                            // Server doesn't echo GAME_START back — set local state manually
                            gameStarted = true;
                            turn = 0;
                            for (let i = 0; i < 4; i++) {
                                pieces[i].state = PARCHIS_STATE_HOME;
                                pieces[i].pos = 0;
                            }
                            log('GAME_START enviado');
                            if (turn === mySlot) setTimeout(() => takeTurn(), 2000);
                        }
                    }, 3000);
                }
            }
            else if (cmd === 0x31) { // PLAYER_LEFT
                const leftPid = payload[0];
                activePlayers &= ~(1 << (leftPid - 1));
                log(`Player left PID=${leftPid}`);
            }
            else if (cmd === 0x24 || cmd === 0x25) {
                // ROOM_FULL or ROOM_NOT_FOUND — stale parchisRoomId, create own room
                log(`Sala invalida (cmd=0x${cmd.toString(16)}), creando sala propia`);
                parchisRoomId = 0;
                sock.write(buildPacket(CMD.ROOM_CREATE, 0, 0,
                    Buffer.from([GAME_ID_PARCHIS, 4, 0x01])));
            }
            else if (cmd === 0x32) { // GAME_START
                gameStarted = true;
                turn = 0;
                // Reset pieces
                for (let i = 0; i < 4; i++) {
                    pieces[i].state = PARCHIS_STATE_HOME;
                    pieces[i].pos = 0;
                }
                log('Partida iniciada');
                if (turn === mySlot) {
                    setTimeout(() => takeTurn(), 2000);
                }
            }
            else if (cmd === CMD.STATE_UPDATE && payloadLen >= 6) {
                const action = payload[0];
                const endTurn = payload[4];
                if (action === 1 && endTurn) {
                    advanceTurn();
                }
            }
        }
    });

    sock.on('error', (err) => {
        if (err.code !== 'ECONNRESET') log(`Error: ${err.message}`);
    });

    sock.on('close', () => {
        log('Desconectado. Reconectando en 5s...');
        // If I was the host, the room is likely gone — reset shared ID so others fallback
        if (pid === 1 && roomId === parchisRoomId) {
            parchisRoomId = 0;
            log('Era host, parchisRoomId reseteado a 0');
        }
        if (interval) clearInterval(interval);
        setTimeout(() => startParchisGhost(ghostNum), 5000);
    });

    interval = setInterval(() => {
        pingCounter++;
        if (pingCounter >= 30) {
            pingCounter = 0;
            if (!sock.destroyed && roomId) sock.write(buildPacket(CMD.PING, roomId, pid));
        }
    }, 500);
}

// ── Arranque ──────────────────────────────────────────────────

const NUM_BURDYN = parseInt(process.argv[2] || '3', 10);
const NUM_TETRIS = parseInt(process.argv[3] || '3', 10);
const NUM_PARCHIS = parseInt(process.argv[4] || '3', 10);

console.log('MSXon Ghost Service v1.3');
console.log(`Iniciando: 1 damas + ${NUM_BURDYN} burdyn + ${NUM_TETRIS} tetris + ${NUM_PARCHIS} parchis + 1 poker\n`);
startDamasGhost();

// Burdyn ghosts
startBurdynGhost(0);
for (let i = 1; i < NUM_BURDYN; i++) {
    setTimeout(() => {
        if (burdynRoomId > 0) {
            startBurdynGhost(i);
        } else {
            console.log(`[BURDYN#${i}] Esperando sala... reintentando en 3s`);
            setTimeout(() => startBurdynGhost(i), 3000);
        }
    }, 5000 + i * 1500);
}

// Tetris ghosts
setTimeout(() => {
    startTetrisGhost(0);
    for (let i = 1; i < NUM_TETRIS; i++) {
        setTimeout(() => {
            if (tetrisRoomId > 0) {
                startTetrisGhost(i);
            } else {
                console.log(`[TETRIS#${i}] Esperando sala... reintentando en 3s`);
                setTimeout(() => startTetrisGhost(i), 3000);
            }
        }, 3000 + i * 1500);
    }
}, 8000);

// Poker ghost
setTimeout(() => {
    startPokerGhost();
}, 12000);

// Parchis ghosts
setTimeout(() => {
    startParchisGhost(0);
    for (let i = 1; i < NUM_PARCHIS; i++) {
        setTimeout(() => {
            if (parchisRoomId > 0) {
                startParchisGhost(i);
            } else {
                console.log(`[PARCHIS#${i}] Esperando sala... reintentando en 3s`);
                setTimeout(() => startParchisGhost(i), 3000);
            }
        }, 3000 + i * 1500);
    }
}, 15000);
