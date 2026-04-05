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
    0x04: 'Parchis',
    0x05: 'Texas',
    0x06: 'Tetris',
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
        sock.setTimeout(10000); // Timeout solo para conexion+auth
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
                if (cmd === 0x11) {
                    sock.setTimeout(0); // Quitar timeout tras auth
                    sock.removeAllListeners('timeout');
                    resolve({ sock, buf });
                }
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

// ── Ghost Damas (IA basica) ──────────────────────────────────

let damasGhost = null; // { socket, interval, board, roomId, pid, myColor }

const PIECE_NONE = 0, PIECE_WHITE = 1, PIECE_BLACK = 2, PIECE_WHITE_KING = 3, PIECE_BLACK_KING = 4;
const isWhite = p => p === PIECE_WHITE || p === PIECE_WHITE_KING;
const isBlack = p => p === PIECE_BLACK || p === PIECE_BLACK_KING;
const isKing = p => p === PIECE_WHITE_KING || p === PIECE_BLACK_KING;
const isEnemy = (a, b) => (isWhite(a) && isBlack(b)) || (isBlack(a) && isWhite(b));

function damasInitBoard() {
    const b = Array.from({length: 8}, () => new Array(8).fill(0));
    for (let y = 0; y < 8; y++) {
        for (let x = 0; x < 8; x++) {
            if ((x + y) & 1) {
                if (y < 3) b[y][x] = PIECE_BLACK;
                else if (y > 4) b[y][x] = PIECE_WHITE;
            }
        }
    }
    return b;
}

function damasFindMoves(board, color) {
    const captures = [];
    const moves = [];

    for (let y = 0; y < 8; y++) {
        for (let x = 0; x < 8; x++) {
            const p = board[y][x];
            if (p === PIECE_NONE) continue;
            const mine = (color === PIECE_WHITE) ? isWhite(p) : isBlack(p);
            if (!mine) continue;

            if (isKing(p)) {
                // Dama: 4 diagonales, cualquier distancia
                for (const sy of [-1, 1]) {
                    for (const sx of [-1, 1]) {
                        let foundEnemy = false;
                        for (let i = 1; i < 8; i++) {
                            const cx = x + sx * i, cy = y + sy * i;
                            if (cx < 0 || cx >= 8 || cy < 0 || cy >= 8) break;
                            const cp = board[cy][cx];
                            if (cp === PIECE_NONE) {
                                if (foundEnemy) captures.push({fx: x, fy: y, tx: cx, ty: cy, cap: true});
                                else moves.push({fx: x, fy: y, tx: cx, ty: cy, cap: false});
                            } else if (isEnemy(p, cp) && !foundEnemy) {
                                foundEnemy = true;
                            } else break;
                        }
                    }
                }
            } else {
                // Ficha normal
                const fwd = (color === PIECE_WHITE) ? -1 : 1;
                // Movimientos simples
                for (const dx of [-1, 1]) {
                    const tx = x + dx, ty = y + fwd;
                    if (tx >= 0 && tx < 8 && ty >= 0 && ty < 8 && board[ty][tx] === PIECE_NONE)
                        moves.push({fx: x, fy: y, tx, ty, cap: false});
                }
                // Capturas (4 direcciones)
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
    // Capturas son obligatorias
    return captures.length > 0 ? captures : moves;
}

function damasExecuteMove(board, fx, fy, tx, ty) {
    const p = board[fy][fx];
    board[ty][tx] = p;
    board[fy][fx] = PIECE_NONE;

    const dx = tx - fx, dy = ty - fy;
    const sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    const dist = Math.abs(dx);

    // Quitar pieza capturada
    for (let i = 1; i < dist; i++) {
        const cx = fx + sx * i, cy = fy + sy * i;
        if (board[cy][cx] !== PIECE_NONE) {
            board[cy][cx] = PIECE_NONE;
            break;
        }
    }

    // Promocion
    if (p === PIECE_WHITE && ty === 0) board[ty][tx] = PIECE_WHITE_KING;
    if (p === PIECE_BLACK && ty === 7) board[ty][tx] = PIECE_BLACK_KING;
}

async function startDamasGhost() {
    if (damasGhost) {
        console.log('\nGhost de damas ya activo. Escribe "stopd" para pararlo.\n');
        return;
    }

    try {
        // Conexion dedicada para el ghost de damas
        const damasSock = new net.Socket();

        await new Promise((resolve, reject) => {
            let authTimer = setTimeout(() => { damasSock.destroy(); reject(new Error('Auth timeout')); }, 10000);
            damasSock.connect(SERVER_PORT, SERVER_IP, () => {
                damasSock.write(buildPacket(CMD.AUTH, 0, 0, AUTH_TOKEN));
            });
            let authBuf = Buffer.alloc(0);
            const authHandler = (chunk) => {
                authBuf = Buffer.concat([authBuf, chunk]);
                if (authBuf.length >= 6 && authBuf[2] === 0x11) {
                    clearTimeout(authTimer);
                    damasSock.removeListener('data', authHandler);
                    resolve();
                }
            };
            damasSock.on('data', authHandler);
            damasSock.on('error', (e) => { clearTimeout(authTimer); reject(e); });
        });

        console.log('  Ghost damas: auth OK');

        // Crear sala
        const roomInfo = await new Promise((resolve, reject) => {
            let roomTimer = setTimeout(() => reject(new Error('Room timeout')), 5000);
            damasSock.write(buildPacket(CMD.ROOM_CREATE, 0, 0, Buffer.from([0x02, 2, 0x01])));
            let roomBuf = Buffer.alloc(0);
            const roomHandler = (chunk) => {
                roomBuf = Buffer.concat([roomBuf, chunk]);
                while (roomBuf.length >= 6) {
                    const idx = roomBuf.indexOf(Buffer.from([0x46, 0x4D]));
                    if (idx < 0) { roomBuf = Buffer.alloc(0); break; }
                    if (idx > 0) { roomBuf = roomBuf.subarray(idx); continue; }
                    const len = roomBuf[5];
                    if (roomBuf.length < 6 + len) break;
                    if (roomBuf[2] === CMD.ROOM_INFO) {
                        clearTimeout(roomTimer);
                        const payload = Buffer.from(roomBuf.subarray(6, 6 + len));
                        damasSock.removeListener('data', roomHandler);
                        resolve(payload);
                        return;
                    }
                    roomBuf = roomBuf.subarray(6 + len);
                }
            };
            damasSock.on('data', roomHandler);
        });

        const roomId = roomInfo[0];
        const pid = roomInfo[3];
        const sock = damasSock;

        console.log(`\nGhost damas: sala creada 0x${roomId.toString(16).padStart(2, '0')} como P${pid}`);
        console.log('Esperando rival humano...');

        const myColor = (pid === 1) ? PIECE_WHITE : PIECE_BLACK;
        const board = damasInitBoard();
        let turn = PIECE_WHITE;
        let moveCount = 0;
        let gameStarted = (pid === 2); // Si somos P2, el P1 ya estaba — juego empieza

        console.log(`Ghost juega con ${myColor === PIECE_WHITE ? 'BLANCAS' : 'NEGRAS'}. ${gameStarted ? 'Partida en curso!' : 'Esperando rival...'} "stopd" para parar.\n`);

        // Escuchar movimientos del oponente
        let recvBuf = Buffer.alloc(0);
        sock.removeAllListeners('data'); // Limpiar handlers previos de ghostSendAndWait
        sock.on('data', (chunk) => {
            try {
                recvBuf = Buffer.concat([recvBuf, chunk]);
                while (recvBuf.length >= 6) {
                    // Buscar magic
                    const idx = recvBuf.indexOf(Buffer.from([0x46, 0x4D]));
                    if (idx < 0) { recvBuf = Buffer.alloc(0); break; }
                    if (idx > 0) { recvBuf = recvBuf.subarray(idx); continue; }

                    const len = recvBuf[5];
                    if (recvBuf.length < 6 + len) break;

                    const cmd = recvBuf[2];
                    const payload = Buffer.from(recvBuf.subarray(6, 6 + len));
                    recvBuf = recvBuf.subarray(6 + len);

                    if (cmd === 0x40 && len >= 4) {
                        const fx = payload[0], fy = payload[1], tx = payload[2], ty = payload[3];
                        if (fx === tx && fy === ty) {
                            console.log(`  Oponente: (${fx},${fy})->(${tx},${ty}) IGNORADO`);
                        } else if (fx < 8 && fy < 8 && tx < 8 && ty < 8) {
                            const endTurn = len >= 5 ? payload[4] : 1;
                            damasExecuteMove(board, fx, fy, tx, ty);
                            if (endTurn) {
                                turn = (turn === PIECE_WHITE) ? PIECE_BLACK : PIECE_WHITE;
                                console.log(`  Oponente: (${fx},${fy})->(${tx},${ty}) | Turno: ${turn === PIECE_WHITE ? 'B' : 'N'}`);
                            } else {
                                console.log(`  Oponente: (${fx},${fy})->(${tx},${ty}) MULTI (esperando siguiente...)`);
                            }
                        }
                    }
                    else if (cmd === 0x30) {
                        gameStarted = true;
                        console.log('  Oponente conectado! Partida empieza.');
                    }
                    // Ignorar PONG (0x02) y otros paquetes silenciosamente
                }
            } catch(e) {
                console.log(`  Ghost data error: ${e.message}`);
            }
        });
        sock.on('error', (err) => {
            if (err.code !== 'ECONNRESET') console.log(`  Ghost socket error: ${err.message}`);
        });
        sock.on('close', () => {
            console.log('  Ghost damas: socket cerrado');
            if (damasGhost) { clearInterval(damasGhost.interval); damasGhost = null; }
        });

        // Keepalive + jugadas
        let pingCounter = 0;
        let multiCapX = -1, multiCapY = -1; // Multi-captura en curso
        let multiCapCount = 0; // Seguridad: max 12 capturas seguidas

        const interval = setInterval(() => {
            // Ping cada 10 ticks (~20s a 2s/tick)
            pingCounter++;
            if (pingCounter >= 10) {
                pingCounter = 0;
                if (!sock.destroyed) {
                    sock.write(buildPacket(0x01, roomId, pid));
                    console.log('  [PING]');
                }
            }

            if (!gameStarted) return;
            if (turn !== myColor) return;

            // Buscar movimientos validos
            let validMoves;
            if (multiCapX >= 0) {
                // Multi-captura: solo capturas de la ficha en curso
                validMoves = damasFindMoves(board, myColor).filter(
                    m => m.fx === multiCapX && m.fy === multiCapY && Math.abs(m.tx - m.fx) >= 2
                );
                if (validMoves.length === 0) {
                    // No mas capturas: turno pasa al oponente
                    multiCapX = -1;
                    multiCapY = -1;
                    turn = (turn === PIECE_WHITE) ? PIECE_BLACK : PIECE_WHITE;
                    console.log(`  Multi-captura fin. Turno: ${turn === PIECE_WHITE ? 'BLANCAS' : 'NEGRAS'}`);
                    return;
                }
            } else {
                validMoves = damasFindMoves(board, myColor);
            }

            if (validMoves.length === 0) {
                // Comprobar si tiene fichas
                let myPieces = 0;
                for (let r = 0; r < 8; r++)
                    for (let c = 0; c < 8; c++)
                        if ((myColor === PIECE_WHITE && isWhite(board[r][c])) ||
                            (myColor === PIECE_BLACK && isBlack(board[r][c]))) myPieces++;

                if (myPieces === 0)
                    console.log('\n  *** GHOST HA PERDIDO — sin fichas ***');
                else
                    console.log('\n  *** GHOST HA PERDIDO — sin movimientos ***');

                // Reiniciar tablero y esperar nuevo oponente
                console.log('  Reiniciando tablero. Esperando nuevo rival...\n');
                for (let r = 0; r < 8; r++)
                    for (let c = 0; c < 8; c++)
                        board[r][c] = PIECE_NONE;
                for (let r = 0; r < 8; r++)
                    for (let c = 0; c < 8; c++)
                        if ((c + r) & 1) {
                            if (r < 3) board[r][c] = PIECE_BLACK;
                            else if (r > 4) board[r][c] = PIECE_WHITE;
                        }
                turn = PIECE_WHITE;
                multiCapX = -1;
                multiCapY = -1;
                multiCapCount = 0;
                gameStarted = false;
                return;
            }

            // Elegir y ejecutar movimiento
            const t0 = Date.now();
            const move = validMoves[Math.floor(Math.random() * validMoves.length)];
            damasExecuteMove(board, move.fx, move.fy, move.tx, move.ty);

            let endTurnFlag = 1; // por defecto fin de turno
            moveCount++;
            const dt = Date.now() - t0;
            console.log(`  Ghost: (${move.fx},${move.fy})->(${move.tx},${move.ty})${move.cap ? ' CAPTURA' : ''} | #${moveCount} (${dt}ms)`);

            if (move.cap) {
                multiCapCount++;
                if (multiCapCount <= 12) {
                    const moreCaps = damasFindMoves(board, myColor).filter(
                        m => m.fx === move.tx && m.fy === move.ty && m.cap === true
                    );
                    if (moreCaps.length > 0) {
                        multiCapX = move.tx;
                        multiCapY = move.ty;
                        endTurnFlag = 0; // Multi-captura: no fin de turno
                        console.log('  Multi-captura posible, sigue...');
                    }
                }
            }

            // Enviar movimiento con flag endTurn
            const sendPayload = Buffer.from([move.fx, move.fy, move.tx, move.ty, endTurnFlag, 0, 0, 0]);
            if (!sock.destroyed) sock.write(buildPacket(0x40, roomId, pid, sendPayload));

            if (endTurnFlag === 0) {
                // Multi-captura: no cambiar turno, seguir en proximo tick
                return;
            }

            // Turno pasa al oponente
            multiCapX = -1;
            multiCapY = -1;
            multiCapCount = 0;
            turn = (turn === PIECE_WHITE) ? PIECE_BLACK : PIECE_WHITE;
            console.log(`  Turno: ${turn === PIECE_WHITE ? 'BLANCAS' : 'NEGRAS'}`);

        }, 1500);

        damasGhost = { socket: sock, interval, board, roomId, pid, myColor };

    } catch (err) {
        console.log(`Error: ${err.message}`);
    }
}

function stopDamasGhost() {
    if (!damasGhost) {
        console.log('\nNo hay ghost de damas activo.\n');
        return;
    }
    clearInterval(damasGhost.interval);
    if (!damasGhost.socket.destroyed) damasGhost.socket.destroy();
    damasGhost = null;
    console.log('\nGhost de damas detenido.\n');
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
    console.log('  6. Ghost Burdyn (rebota por mapa)');
    console.log('  7. Parar ghosts Burdyn');
    console.log('  8. Ghost Damas (IA basica)');
    console.log('  9. Parar ghost Damas');
    console.log('  x. Reiniciar servidor (limpia salas)');
    console.log('');
    console.log('  GHOST SERVICE (VPS)');
    console.log('  gs. Estado ghost-service');
    console.log('  gr. Reiniciar ghost-service');
    console.log('  gl. Logs ghost-service');
    console.log('');
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
                case '8': case 'damas':
                    await startDamasGhost();
                    break;
                case '9': case 'stopd':
                    stopDamasGhost();
                    break;
                case 'x': case 'restart':
                    console.log('\nReiniciando servidor...');
                    try {
                        require('child_process').execSync('ssh root@217.154.107.144 "systemctl restart msx-server"', { timeout: 15000 });
                        console.log('Servidor reiniciado.\n');
                        disconnect();
                    } catch(e) { console.log('Error: ' + e.message); }
                    break;
                case 'gs':
                    console.log('\nEstado ghost-service:');
                    try {
                        const out = require('child_process').execSync('ssh root@217.154.107.144 "systemctl status msx-ghost 2>&1 | head -10"', { timeout: 15000 });
                        console.log(out.toString());
                    } catch(e) { console.log('No instalado o error: ' + e.message); }
                    break;
                case 'gr':
                    console.log('\nReiniciando ghost-service...');
                    try {
                        require('child_process').execSync('ssh root@217.154.107.144 "systemctl restart msx-ghost"', { timeout: 15000 });
                        console.log('Ghost-service reiniciado.\n');
                    } catch(e) { console.log('Error: ' + e.message); }
                    break;
                case 'gl':
                    console.log('\nLogs ghost-service (ultimas 20 lineas):');
                    try {
                        const out = require('child_process').execSync('ssh root@217.154.107.144 "journalctl -u msx-ghost --no-pager -n 20"', { timeout: 15000 });
                        console.log(out.toString());
                    } catch(e) { console.log('Error: ' + e.message); }
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
