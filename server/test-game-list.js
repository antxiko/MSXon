'use strict';

// =============================================================================
// test-game-list.js — Test puntual del handler CMD.GAME_LIST
//
// Conecta, AUTH (legacy) -> rol "service" -> debería ver public+private,
// luego LOGIN como user normal -> rol "user" -> solo public.
//
// Uso:  node test-game-list.js [host] [port] [user] [pass]
//   Si no pasas user/pass, salta el test de LOGIN y solo prueba con AUTH legacy.
// =============================================================================

const net = require('net');

const HOST = process.argv[2] || '127.0.0.1';
const PORT = parseInt(process.argv[3] || '9876', 10);
const USER = process.argv[4] || null;
const PASS = process.argv[5] || null;

const MAGIC_0 = 0x46, MAGIC_1 = 0x4D, HEADER_SZ = 6;
const CMD = {
    PING: 0x01, PONG: 0x02,
    AUTH: 0x10, AUTH_OK: 0x11, AUTH_FAIL: 0x12,
    LOGIN: 0x13, LOGIN_OK: 0x14, LOGIN_FAIL: 0x15,
    GAME_LIST: 0x27,
};
const AUTH_TOKEN = Buffer.from([0xDE, 0xAD, 0xBE, 0xEF]);

function buildPacket(cmd, roomId = 0, pid = 0, payload = Buffer.alloc(0)) {
    const pkt = Buffer.alloc(HEADER_SZ + payload.length);
    pkt[0] = MAGIC_0; pkt[1] = MAGIC_1;
    pkt[2] = cmd; pkt[3] = roomId; pkt[4] = pid; pkt[5] = payload.length;
    payload.copy(pkt, HEADER_SZ);
    return pkt;
}

function parseGameList(payload) {
    const games = [];
    if (payload.length < 1) return games;
    const n = payload[0];
    let off = 1;
    for (let i = 0; i < n; i++) {
        if (off + 5 > payload.length) break;
        const id = payload[off++];
        const flags = payload[off++];
        const max = payload[off++];
        const proto = payload[off++];
        const comLen = payload[off++];
        if (off + comLen > payload.length) break;
        const com = payload.slice(off, off + comLen).toString('utf8'); off += comLen;
        if (off + 1 > payload.length) break;
        const nameLen = payload[off++];
        if (off + nameLen > payload.length) break;
        const name = payload.slice(off, off + nameLen).toString('utf8'); off += nameLen;
        games.push({ id, flags, max, proto, com, name, private: !!(flags & 0x01) });
    }
    return games;
}

function makeReader(socket) {
    let buffer = Buffer.alloc(0);
    const queue = [];
    let waiter = null;
    socket.on('data', chunk => {
        buffer = Buffer.concat([buffer, chunk]);
        while (buffer.length >= HEADER_SZ) {
            if (buffer[0] !== MAGIC_0 || buffer[1] !== MAGIC_1) {
                buffer = buffer.slice(1);
                continue;
            }
            const len = buffer[5];
            if (buffer.length < HEADER_SZ + len) break;
            const pkt = {
                cmd: buffer[2], roomId: buffer[3], pid: buffer[4],
                payload: buffer.slice(HEADER_SZ, HEADER_SZ + len),
            };
            buffer = buffer.slice(HEADER_SZ + len);
            if (waiter) { const w = waiter; waiter = null; w(pkt); }
            else queue.push(pkt);
        }
    });
    return {
        next() {
            return new Promise((resolve, reject) => {
                if (queue.length) return resolve(queue.shift());
                waiter = resolve;
                setTimeout(() => { if (waiter) { waiter = null; reject(new Error('timeout')); } }, 5000);
            });
        }
    };
}

function connect() {
    return new Promise((resolve, reject) => {
        const s = net.connect(PORT, HOST);
        s.once('connect', () => resolve(s));
        s.once('error', reject);
    });
}

async function runScenario(label, doLogin) {
    console.log(`\n══ ${label} ══`);
    const sock = await connect();
    const reader = makeReader(sock);

    // AUTH legacy
    sock.write(buildPacket(CMD.AUTH, 0, 0, AUTH_TOKEN));
    let pkt = await reader.next();
    if (pkt.cmd !== CMD.AUTH_OK) { console.log('AUTH failed:', pkt.cmd.toString(16)); sock.end(); return; }
    console.log('AUTH ok (legacy → service role)');

    if (doLogin) {
        const u = Buffer.from(USER, 'utf8'), p = Buffer.from(PASS, 'utf8');
        const pl = Buffer.concat([Buffer.from([u.length]), u, Buffer.from([p.length]), p]);
        sock.write(buildPacket(CMD.LOGIN, 0, 0, pl));
        pkt = await reader.next();
        if (pkt.cmd !== CMD.LOGIN_OK) {
            console.log(`LOGIN failed (cmd=0x${pkt.cmd.toString(16)} reason=${pkt.payload[0]})`);
            sock.end(); return;
        }
        const role = pkt.payload[0];
        const nickLen = pkt.payload[1];
        const nick = pkt.payload.slice(2, 2 + nickLen).toString('utf8');
        console.log(`LOGIN ok role=${role} nick="${nick}"`);
    }

    // GAME_LIST
    sock.write(buildPacket(CMD.GAME_LIST));
    pkt = await reader.next();
    if (pkt.cmd !== CMD.GAME_LIST) {
        console.log('Expected GAME_LIST, got 0x' + pkt.cmd.toString(16));
        sock.end(); return;
    }
    const games = parseGameList(pkt.payload);
    console.log(`Received ${games.length} games (payload ${pkt.payload.length} bytes):`);
    for (const g of games) {
        const tag = g.private ? '🔒' : '  ';
        console.log(`  ${tag} 0x${g.id.toString(16).padStart(2,'0')}  ${g.name.padEnd(14)}  com=${g.com.padEnd(8)} max=${g.max} proto=${g.proto}`);
    }

    sock.end();
}

(async () => {
    try {
        await runScenario('AUTH legacy (rol service)', false);
        if (USER && PASS) {
            await runScenario(`LOGIN as ${USER} (rol del usuario)`, true);
        } else {
            console.log('\n(no user/pass arg, saltando LOGIN test)');
        }
    } catch (e) {
        console.error('FAIL:', e.message);
        process.exit(1);
    }
})();
