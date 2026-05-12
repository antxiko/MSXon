#!/usr/bin/env node
'use strict';

// =============================================================================
// admin.js — CLI de administración MSXon
//
// Opera directamente sobre users.json y games.json.
// Si msx-server está corriendo, REINICIA el servicio tras cambios para que
// recoja la nueva versión (los stores no auto-recargan en runtime).
//
// Uso:  node admin.js <comando> [args]
//
// Sin npm deps — usa auth-store.js y games-store.js del propio proyecto.
// =============================================================================

const path = require('path');
const { AuthStore } = require('./auth-store');
const { GamesStore, VALID_VISIBILITY } = require('./games-store');

const QR_BASE_URL = process.env.MSX_QR_BASE_URL || 'https://msxon.nosignalbbs.com/r';

// ── Utilidades ─────────────────────────────────────────────────
function die(msg, code = 1) {
    console.error('error: ' + msg);
    process.exit(code);
}

function pad(s, n) {
    s = String(s);
    return s.length >= n ? s : s + ' '.repeat(n - s.length);
}

function fmtDate(ts) {
    if (!ts) return 'never';
    const d = new Date(ts);
    return d.toISOString().replace('T', ' ').slice(0, 16);
}

function parseGameId(s) {
    if (typeof s !== 'string') return null;
    const n = s.startsWith('0x') ? parseInt(s, 16) : parseInt(s, 10);
    return Number.isInteger(n) && n >= 0 && n <= 255 ? n : null;
}

// ── Comandos ───────────────────────────────────────────────────

function cmdHelp() {
    console.log(`
MSXon admin CLI — uso: node admin.js <comando> [args]

Usuarios:
  list-users                          Lista usuarios activos
  list-pending                        Lista registros pendientes de activación
  promote <user> <role>               Cambia role (user/admin/superadmin)
  demote  <user>                      Atajo de "promote <user> user"
  reset-password <user>               Invalida password, genera token QR nuevo

Juegos:
  list-games                          Lista todos los juegos
  set-visibility <id> <vis>           public | private | disabled
  add-game <id> <name> <com> <max> <proto> <vis>
                                      Añade un juego nuevo
  del-game <id>                       Elimina juego del catálogo

Otros:
  help                                Muestra este texto

Ids hex aceptados (e.g. 0x69) o decimal (105).
`);
}

function cmdListUsers() {
    const auth = new AuthStore();
    auth.init();
    const users = auth.listUsers();
    if (users.length === 0) { console.log('(no users)'); return; }
    console.log(pad('USERNAME', 18) + pad('ROLE', 12) + pad('NICK', 18) + pad('LAST LOGIN', 18) + 'CREATED');
    console.log('-'.repeat(82));
    for (const u of users) {
        console.log(
            pad(u.username, 18) +
            pad(u.role, 12) +
            pad(u.nick || '', 18) +
            pad(fmtDate(u.lastLogin), 18) +
            fmtDate(u.createdAt)
        );
    }
    console.log(`\n(${users.length} users)`);
}

function cmdListPending() {
    const auth = new AuthStore();
    auth.init();
    const pending = auth.listPending();
    if (pending.length === 0) { console.log('(no pending registrations)'); return; }
    console.log(pad('TOKEN', 12) + pad('USER', 18) + pad('NICK', 18) + 'TTL (min)');
    console.log('-'.repeat(60));
    for (const p of pending) {
        const ttlMin = Math.max(0, Math.floor(p.ttlMs / 60000));
        console.log(pad(p.token, 12) + pad(p.username, 18) + pad(p.nick, 18) + ttlMin);
    }
    console.log(`\n(${pending.length} pending)`);
}

function cmdPromote(args) {
    const [username, role] = args;
    if (!username || !role) die('usage: promote <user> <role>');
    const auth = new AuthStore();
    auth.init();
    const r = auth.setRole(username, role);
    if (!r.ok) die(r.reason);
    auth.flushSync();
    console.log(`OK: ${username} → ${r.user.role}`);
}

function cmdDemote(args) {
    const [username] = args;
    if (!username) die('usage: demote <user>');
    cmdPromote([username, 'user']);
}

function cmdResetPassword(args) {
    const [username] = args;
    if (!username) die('usage: reset-password <user>');
    const auth = new AuthStore();
    auth.init();
    const r = auth.resetPassword(username);
    if (!r.ok) die(r.reason);
    auth.flushSync();
    const url = `${QR_BASE_URL}?u=${encodeURIComponent(username)}&t=${r.token}`;
    console.log(`OK: ${username} reset.`);
    console.log(`Token: ${r.token}`);
    console.log(`URL:   ${url}`);
    console.log(`\n(El usuario tiene 10 min para escanear el QR / abrir la URL y poner password nueva)`);
}

function cmdListGames() {
    const games = new GamesStore();
    games.init();
    const list = games.list();
    if (list.length === 0) { console.log('(no games)'); return; }
    console.log(pad('ID', 6) + pad('NAME', 16) + pad('COM', 12) + pad('MAX', 5) + pad('PROTO', 7) + 'VISIBILITY');
    console.log('-'.repeat(60));
    for (const g of list) {
        const id = '0x' + g.id.toString(16).padStart(2, '0');
        console.log(
            pad(id, 6) +
            pad(g.name, 16) +
            pad(g.com, 12) +
            pad(g.max, 5) +
            pad(g.proto, 7) +
            g.visibility
        );
    }
    console.log(`\n(${list.length} games)`);
}

function cmdSetVisibility(args) {
    const [idStr, vis] = args;
    const id = parseGameId(idStr);
    if (id === null) die('invalid game id');
    if (!VALID_VISIBILITY.has(vis)) die(`invalid visibility: ${vis} (use ${[...VALID_VISIBILITY].join('|')})`);
    const games = new GamesStore();
    games.init();
    const r = games.setVisibility(id, vis);
    if (!r.ok) die(r.reason);
    games.flushSync();
    console.log(`OK: ${r.game.name} (0x${id.toString(16).padStart(2,'0')}) → ${vis}`);
}

function cmdAddGame(args) {
    const [idStr, name, com, maxStr, protoStr, vis] = args;
    if (args.length < 6) die('usage: add-game <id> <name> <com> <max> <proto> <vis>');
    const id = parseGameId(idStr);
    const max = parseInt(maxStr, 10);
    const proto = parseGameId(protoStr);
    if (id === null) die('invalid id');
    if (!Number.isInteger(max) || max < 1 || max > 16) die('invalid max');
    if (proto === null) die('invalid proto');
    if (!VALID_VISIBILITY.has(vis)) die(`invalid visibility: ${vis}`);
    const games = new GamesStore();
    games.init();
    const r = games.addOrUpdate({ id, name, com, max, proto, visibility: vis });
    if (!r.ok) die(r.reason);
    games.flushSync();
    console.log(`OK: ${name} (0x${id.toString(16).padStart(2,'0')}) added/updated.`);
}

function cmdDelGame(args) {
    const id = parseGameId(args[0]);
    if (id === null) die('usage: del-game <id>');
    const games = new GamesStore();
    games.init();
    const r = games.remove(id);
    if (!r.ok) die(r.reason);
    games.flushSync();
    console.log(`OK: 0x${id.toString(16).padStart(2,'0')} deleted.`);
}

// ── Dispatch ───────────────────────────────────────────────────
const COMMANDS = {
    help:           cmdHelp,
    'list-users':    cmdListUsers,
    'list-pending':  cmdListPending,
    promote:         cmdPromote,
    demote:          cmdDemote,
    'reset-password': cmdResetPassword,
    'list-games':    cmdListGames,
    'set-visibility': cmdSetVisibility,
    'add-game':      cmdAddGame,
    'del-game':      cmdDelGame,
};

const [, , cmd, ...args] = process.argv;
if (!cmd || cmd === '-h' || cmd === '--help') { cmdHelp(); process.exit(0); }
const fn = COMMANDS[cmd];
if (!fn) { console.error(`unknown command: ${cmd}`); cmdHelp(); process.exit(1); }
try {
    fn(args);
} catch (e) {
    console.error('FAIL: ' + e.message);
    process.exit(1);
}
