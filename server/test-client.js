'use strict';

// =============================================================================
// test-client.js — Cliente de test que simula un MSX conectándose al servidor
//
// Valida el flujo completo:
//   1. Conexión TCP
//   2. AUTH con token correcto
//   3. AUTH con token incorrecto (debe fallar)
//   4. Crear sala
//   5. Segundo cliente se une a la sala
//   6. Intercambio de STATE_UPDATE
//   7. Desconexión limpia (ROOM_LEAVE)
//   8. Sala se elimina al quedar vacía
//
// Uso: node test-client.js [host] [port]
// =============================================================================

const net = require('net');

const HOST = process.argv[2] || '127.0.0.1';
const PORT = parseInt(process.argv[3]) || 9876;

// ── Protocolo (debe coincidir con protocol.h y msx-gameserver.js) ──
const MAGIC_0 = 0x46; // 'F'
const MAGIC_1 = 0x4D; // 'M'
const HEADER_SZ = 6;

const CMD = {
    PING:           0x01,
    PONG:           0x02,
    AUTH:           0x10,
    AUTH_OK:        0x11,
    AUTH_FAIL:      0x12,
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
    ERROR:          0xFF,
};

const CMD_NAME = {};
for (const [k, v] of Object.entries(CMD)) CMD_NAME[v] = k;

const AUTH_TOKEN_OK   = Buffer.from([0xDE, 0xAD, 0xBE, 0xEF]);
const AUTH_TOKEN_BAD  = Buffer.from([0x00, 0x00, 0x00, 0x00]);
const GAME_ID_BALL    = 0x01;

// ── Utilidades ──────────────────────────────────────────────────────

function buildPacket(cmd, roomId = 0, pid = 0, payload = Buffer.alloc(0)) {
    const pkt = Buffer.allocUnsafe(HEADER_SZ + payload.length);
    pkt[0] = MAGIC_0;
    pkt[1] = MAGIC_1;
    pkt[2] = cmd;
    pkt[3] = roomId;
    pkt[4] = pid;
    pkt[5] = payload.length;
    payload.copy(pkt, HEADER_SZ);
    return pkt;
}

function parsePackets(buf) {
    const packets = [];
    let offset = 0;
    while (offset + HEADER_SZ <= buf.length) {
        if (buf[offset] !== MAGIC_0 || buf[offset + 1] !== MAGIC_1) {
            offset++;
            continue;
        }
        const len = buf[offset + 5];
        if (offset + HEADER_SZ + len > buf.length) break;
        packets.push({
            cmd:     buf[offset + 2],
            roomId:  buf[offset + 3],
            pid:     buf[offset + 4],
            payload: buf.slice(offset + HEADER_SZ, offset + HEADER_SZ + len),
        });
        offset += HEADER_SZ + len;
    }
    return { packets, remaining: buf.slice(offset) };
}

// ── Cliente con promesas ────────────────────────────────────────────

function createClient(name) {
    return new Promise((resolve, reject) => {
        const sock = net.createConnection(PORT, HOST, () => {
            let buffer = Buffer.alloc(0);
            const waiters = [];
            const inbox = [];    // Cola de paquetes recibidos sin consumir

            sock.on('data', (chunk) => {
                buffer = Buffer.concat([buffer, chunk]);
                const { packets, remaining } = parsePackets(buffer);
                buffer = remaining;
                for (const pkt of packets) {
                    if (waiters.length > 0) {
                        waiters.shift()(pkt);
                    } else {
                        inbox.push(pkt);
                    }
                }
            });

            const client = {
                name,
                sock,
                send(cmd, roomId = 0, pid = 0, payload = Buffer.alloc(0)) {
                    sock.write(buildPacket(cmd, roomId, pid, payload));
                },
                waitPacket(timeoutMs = 3000) {
                    // Si ya hay paquetes en la cola, devolver inmediatamente
                    if (inbox.length > 0) {
                        return Promise.resolve(inbox.shift());
                    }
                    return new Promise((res, rej) => {
                        const timer = setTimeout(() => rej(new Error(`${name}: timeout esperando paquete`)), timeoutMs);
                        waiters.push((pkt) => {
                            clearTimeout(timer);
                            res(pkt);
                        });
                    });
                },
                close() {
                    return new Promise((res) => {
                        sock.once('close', res);
                        sock.destroy();
                    });
                },
            };
            resolve(client);
        });
        sock.on('error', reject);
    });
}

// ── Tests ───────────────────────────────────────────────────────────

let passed = 0;
let failed = 0;

function assert(condition, msg) {
    if (condition) {
        passed++;
        console.log(`  ✅ ${msg}`);
    } else {
        failed++;
        console.log(`  ❌ ${msg}`);
    }
}

async function run() {
    console.log(`\n🔌 Conectando a ${HOST}:${PORT}...\n`);

    // ── Test 1: Auth con token incorrecto ────────────────────────
    console.log('── Test 1: Auth con token INCORRECTO');
    {
        const c = await createClient('BadAuth');
        c.send(CMD.AUTH, 0, 0, AUTH_TOKEN_BAD);
        const pkt = await c.waitPacket();
        assert(pkt.cmd === CMD.AUTH_FAIL, 'Servidor rechaza token incorrecto (AUTH_FAIL)');
        await c.close();
    }

    // ── Test 2: Auth correcta ────────────────────────────────────
    console.log('── Test 2: Auth correcta');
    const p1 = await createClient('P1');
    {
        p1.send(CMD.AUTH, 0, 0, AUTH_TOKEN_OK);
        const pkt = await p1.waitPacket();
        assert(pkt.cmd === CMD.AUTH_OK, 'Servidor acepta token correcto (AUTH_OK)');
    }

    // ── Test 3: PING / PONG ──────────────────────────────────────
    console.log('── Test 3: PING / PONG');
    {
        p1.send(CMD.PING);
        const pkt = await p1.waitPacket();
        assert(pkt.cmd === CMD.PONG, 'Servidor responde PONG a PING');
    }

    // ── Test 4: Crear sala ───────────────────────────────────────
    console.log('── Test 4: Crear sala');
    let roomId;
    {
        p1.send(CMD.ROOM_CREATE, 0, 0, Buffer.from([GAME_ID_BALL]));
        const pkt = await p1.waitPacket();
        assert(pkt.cmd === CMD.ROOM_INFO, 'Servidor responde ROOM_INFO');
        assert(pkt.payload.length >= 4, 'Payload tiene al menos 4 bytes');
        roomId = pkt.payload[0];
        const gameId = pkt.payload[1];
        const nPlayers = pkt.payload[2];
        const myPid = pkt.payload[3];
        assert(gameId === GAME_ID_BALL, `Game ID correcto (0x${gameId.toString(16)})`);
        assert(nPlayers === 1, `1 jugador en sala`);
        assert(myPid === 1, `P1 es el host (PID=1)`);
        console.log(`     Sala creada: ROOM=${roomId}`);
    }

    // ── Test 5: Segundo jugador se une ───────────────────────────
    console.log('── Test 5: Segundo jugador se une');
    const p2 = await createClient('P2');
    {
        p2.send(CMD.AUTH, 0, 0, AUTH_TOKEN_OK);
        const authPkt = await p2.waitPacket();
        assert(authPkt.cmd === CMD.AUTH_OK, 'P2 autenticado');

        p2.send(CMD.ROOM_JOIN, 0, 0, Buffer.from([roomId]));

        // P2 recibe ROOM_INFO
        const infoPkt = await p2.waitPacket();
        assert(infoPkt.cmd === CMD.ROOM_INFO, 'P2 recibe ROOM_INFO');
        assert(infoPkt.payload[3] === 2, 'P2 tiene PID=2');

        // P1 recibe PLAYER_JOINED
        const joinPkt = await p1.waitPacket();
        assert(joinPkt.cmd === CMD.PLAYER_JOINED, 'P1 recibe PLAYER_JOINED');
        assert(joinPkt.payload[0] === 2, 'P1 ve que PID=2 se unió');
    }

    // ── Test 6: Unirse a sala inexistente ────────────────────────
    console.log('── Test 6: Sala inexistente');
    {
        const c = await createClient('Ghost');
        c.send(CMD.AUTH, 0, 0, AUTH_TOKEN_OK);
        await c.waitPacket(); // AUTH_OK
        c.send(CMD.ROOM_JOIN, 0, 0, Buffer.from([0xFE]));
        const pkt = await c.waitPacket();
        assert(pkt.cmd === CMD.ROOM_NOT_FOUND, 'ROOM_NOT_FOUND para sala 0xFE');
        await c.close();
    }

    // ── Test 7: STATE_UPDATE relay ───────────────────────────────
    console.log('── Test 7: STATE_UPDATE relay');
    {
        // P1 envía su posición
        const statePayload = Buffer.from([0x00, 0x40, 0x00, 0x50, 0x01, 0x00, 0x00, 0x00]);
        p1.send(CMD.STATE_UPDATE, roomId, 1, statePayload);

        // P2 debe recibir el STATE_UPDATE de P1
        const pkt = await p2.waitPacket();
        assert(pkt.cmd === CMD.STATE_UPDATE, 'P2 recibe STATE_UPDATE');
        assert(pkt.pid === 1, 'STATE_UPDATE viene de PID=1');
        assert(pkt.payload[1] === 0x40, 'X_LO = 0x40 correcto');
        assert(pkt.payload[3] === 0x50, 'Y_LO = 0x50 correcto');

        // P2 envía su posición
        const statePayload2 = Buffer.from([0x00, 0x80, 0x00, 0x90, 0x02, 0x01, 0x00, 0x00]);
        p2.send(CMD.STATE_UPDATE, roomId, 2, statePayload2);

        // P1 debe recibir el STATE_UPDATE de P2
        const pkt2 = await p1.waitPacket();
        assert(pkt2.cmd === CMD.STATE_UPDATE, 'P1 recibe STATE_UPDATE');
        assert(pkt2.pid === 2, 'STATE_UPDATE viene de PID=2');
    }

    // ── Test 8: GAME_START (solo host) ───────────────────────────
    console.log('── Test 8: GAME_START broadcast');
    {
        p1.send(CMD.GAME_START, roomId, 1);
        const pkt = await p2.waitPacket();
        assert(pkt.cmd === CMD.GAME_START, 'P2 recibe GAME_START del host');
    }

    // ── Test 9: P2 se va, P1 recibe PLAYER_LEFT ─────────────────
    console.log('── Test 9: P2 abandona la sala');
    {
        p2.send(CMD.ROOM_LEAVE, roomId, 2);
        const pkt = await p1.waitPacket();
        assert(pkt.cmd === CMD.PLAYER_LEFT, 'P1 recibe PLAYER_LEFT');
        assert(pkt.payload[0] === 2, 'PID=2 abandonó');
    }

    // ── Test 10: P1 se va, sala se elimina ───────────────────────
    console.log('── Test 10: P1 abandona, sala eliminada');
    {
        p1.send(CMD.ROOM_LEAVE, roomId, 1);
        // No hay respuesta directa, pero la sala debe desaparecer del servidor
        // Verificamos intentando unirnos
        const c = await createClient('Check');
        c.send(CMD.AUTH, 0, 0, AUTH_TOKEN_OK);
        await c.waitPacket(); // AUTH_OK
        c.send(CMD.ROOM_JOIN, 0, 0, Buffer.from([roomId]));
        const pkt = await c.waitPacket();
        assert(pkt.cmd === CMD.ROOM_NOT_FOUND, 'Sala eliminada tras quedar vacía');
        await c.close();
    }

    // ── Test 11: Sala llena (4 jugadores max) ────────────────────
    console.log('── Test 11: Sala llena (4 jugadores max)');
    {
        // Crear sala con p1
        p1.send(CMD.ROOM_CREATE, 0, 0, Buffer.from([GAME_ID_BALL]));
        const info = await p1.waitPacket();
        const fullRoomId = info.payload[0];

        // Unir P2, P3, P4
        const clients = [];
        for (let i = 2; i <= 4; i++) {
            const c = await createClient(`Fill-P${i}`);
            c.send(CMD.AUTH, 0, 0, AUTH_TOKEN_OK);
            await c.waitPacket(); // AUTH_OK
            c.send(CMD.ROOM_JOIN, 0, 0, Buffer.from([fullRoomId]));
            await c.waitPacket(); // ROOM_INFO
            clients.push(c);
            // Consumir PLAYER_JOINED en los demás
            // P1 siempre recibe PLAYER_JOINED
            await p1.waitPacket();
            // Los clientes anteriores también reciben PLAYER_JOINED
            for (let j = 0; j < clients.length - 1; j++) {
                await clients[j].waitPacket();
            }
        }

        // P5 intenta unirse → ROOM_FULL
        const p5 = await createClient('P5');
        p5.send(CMD.AUTH, 0, 0, AUTH_TOKEN_OK);
        await p5.waitPacket(); // AUTH_OK
        p5.send(CMD.ROOM_JOIN, 0, 0, Buffer.from([fullRoomId]));
        const fullPkt = await p5.waitPacket();
        assert(fullPkt.cmd === CMD.ROOM_FULL, 'Quinto jugador recibe ROOM_FULL');

        // Limpiar
        await p5.close();
        for (const c of clients) await c.close();
    }

    // ── Limpiar ──────────────────────────────────────────────────
    await p1.close();
    await p2.close();

    // ── Resumen ──────────────────────────────────────────────────
    console.log(`\n${'═'.repeat(50)}`);
    console.log(`  RESULTADO: ${passed} passed, ${failed} failed`);
    console.log(`${'═'.repeat(50)}\n`);

    process.exit(failed > 0 ? 1 : 0);
}

run().catch((err) => {
    console.error('Error fatal:', err.message);
    process.exit(1);
});
