'use strict';

// =============================================================================
// test-register.js — Test E2E del flujo de registro QR
//
// Uso:
//   MSX_SERVER=tu.vps.ip node tools/test-register.js [username] [nick]
//   defaults: server=127.0.0.1, username=myuser, nick=MyUser
//
// Conecta al server, manda REGISTER, recibe token, imprime URL para abrir en
// el móvil. Cuando pulses Enter (tras activar en móvil), pide password e
// intenta LOGIN para verificar.
// =============================================================================

const net      = require('net');
const readline = require('readline');

const SERVER = process.env.MSX_SERVER || '127.0.0.1';
const PORT   = parseInt(process.env.MSX_PORT || '9876', 10);
const WEB_HOST = process.env.MSX_WEB_HOST || 'msxon.nosignalbbs.com';
const username = process.argv[2] || 'myuser';
const nick     = process.argv[3] || 'MyUser';

const CMD = {
    AUTH:           0x10,
    AUTH_OK:        0x11,
    LOGIN:          0x13,
    LOGIN_OK:       0x14,
    LOGIN_FAIL:     0x15,
    REGISTER:       0x16,
    REG_PENDING:    0x17,
    REG_FAIL:       0x18,
};

const ROLE_NAMES = { 1: 'user', 2: 'admin', 3: 'superadmin' };

function buildPacket(cmd, payload) {
    payload = payload || Buffer.alloc(0);
    const pkt = Buffer.alloc(6 + payload.length);
    pkt[0] = 0x46; pkt[1] = 0x4D;
    pkt[2] = cmd; pkt[3] = 0; pkt[4] = 0; pkt[5] = payload.length;
    payload.copy(pkt, 6);
    return pkt;
}

function makeClient() {
    const sock = new net.Socket();
    let buf = Buffer.alloc(0);
    const queue = [];
    const pending = [];
    sock.on('data', (chunk) => {
        buf = Buffer.concat([buf, chunk]);
        while (buf.length >= 6) {
            if (buf[0] !== 0x46 || buf[1] !== 0x4D) {
                buf = buf.slice(1); continue;
            }
            const len = buf[5];
            if (buf.length < 6 + len) break;
            const cmd = buf[2];
            const payload = Buffer.from(buf.slice(6, 6 + len));
            buf = buf.slice(6 + len);
            const r = pending.shift();
            if (r) r({ cmd, payload });
            else   queue.push({ cmd, payload });
        }
    });
    return {
        connect: () => new Promise((resolve, reject) => {
            sock.on('error', reject);
            sock.connect(PORT, SERVER, resolve);
        }),
        send: (cmd, payload) => sock.write(buildPacket(cmd, payload)),
        recv: () => new Promise((resolve) => {
            if (queue.length) resolve(queue.shift());
            else pending.push(resolve);
        }),
        close: () => sock.destroy(),
    };
}

function prompt(rl, q) {
    return new Promise((resolve) => rl.question(q, resolve));
}

(async () => {
    console.log(`Test register E2E con ${SERVER}:${PORT}`);
    console.log(`Username: ${username} | Nick: ${nick}`);
    console.log('');

    // ── Paso 1: REGISTER ─────────────────────────────────
    const c1 = makeClient();
    await c1.connect();
    console.log(`> REGISTER username=${username} nick=${nick}`);
    const userBuf = Buffer.from(username);
    const nickBuf = Buffer.from(nick);
    const regPayload = Buffer.concat([
        Buffer.from([userBuf.length]), userBuf,
        Buffer.from([nickBuf.length]), nickBuf,
    ]);
    c1.send(CMD.REGISTER, regPayload);
    let r = await c1.recv();

    if (r.cmd === CMD.REG_FAIL) {
        const reasons = { 1: 'user_exists', 2: 'invalid_chars', 3: 'disabled', 4: 'pending_already' };
        console.error(`< REG_FAIL reason=${reasons[r.payload[0]] || r.payload[0]}`);
        c1.close();
        process.exit(1);
    }
    if (r.cmd !== CMD.REG_PENDING) {
        console.error(`< Respuesta inesperada cmd=0x${r.cmd.toString(16)}`);
        c1.close();
        process.exit(1);
    }

    const tokLen = r.payload[0];
    const token  = r.payload.slice(1, 1 + tokLen).toString();
    console.log(`< REG_PENDING token=${token}`);
    c1.close();

    const url = `https://${WEB_HOST}/r?u=${encodeURIComponent(username)}&t=${token}`;
    console.log('');
    console.log('═══════════════════════════════════════════════════════════');
    console.log('  ABRE ESTA URL EN TU MÓVIL Y DEFINE TU PASSWORD:');
    console.log('  ' + url);
    console.log('═══════════════════════════════════════════════════════════');
    console.log('');

    const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
    await prompt(rl, 'Pulsa ENTER cuando hayas activado la cuenta en el móvil...');
    const password = await prompt(rl, 'Ahora introduce el password que has elegido: ');
    rl.close();

    // ── Paso 2: LOGIN ────────────────────────────────────
    const c2 = makeClient();
    await c2.connect();
    console.log('');
    console.log(`> LOGIN username=${username}`);
    const passBuf = Buffer.from(password);
    const loginPayload = Buffer.concat([
        Buffer.from([userBuf.length]), userBuf,
        Buffer.from([passBuf.length]), passBuf,
    ]);
    c2.send(CMD.LOGIN, loginPayload);
    r = await c2.recv();

    if (r.cmd === CMD.LOGIN_FAIL) {
        const reasons = { 1: 'bad_credentials', 2: 'not_found', 3: 'banned', 4: 'rate', 5: 'pending_setup' };
        console.error(`< LOGIN_FAIL reason=${reasons[r.payload[0]] || r.payload[0]}`);
        c2.close();
        process.exit(1);
    }
    if (r.cmd !== CMD.LOGIN_OK) {
        console.error(`< Respuesta inesperada cmd=0x${r.cmd.toString(16)}`);
        c2.close();
        process.exit(1);
    }

    const role = r.payload[0];
    const nLen = r.payload[1];
    const nickBack = r.payload.slice(2, 2 + nLen).toString();
    const sid = r.payload.slice(2 + nLen, 2 + nLen + 4).toString('hex');
    console.log(`< LOGIN_OK role=${ROLE_NAMES[role] || ('0x' + role.toString(16))}  nick=${nickBack}  session=${sid}`);
    c2.close();

    if (role === 0x03) {
        console.log('');
        console.log('🎉 ÉXITO: cuenta superadmin creada y verificada.');
    } else {
        console.log('');
        console.log(`OK pero role=${ROLE_NAMES[role]} (no superadmin). Verifica el archivo .superadmin del VPS.`);
    }
    process.exit(0);
})().catch(e => { console.error('Error fatal:', e); process.exit(2); });
