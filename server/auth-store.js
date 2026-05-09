'use strict';

// =============================================================================
// auth-store.js — Almacén de usuarios MSXon
//
// Gestiona:
//  - Registros pendientes en memoria (token + TTL 10min, NO persistido)
//  - Usuarios activados en users.json (con scrypt hash)
//  - Roles: superadmin / admin / user
//  - Bootstrap superadmin desde archivo .superadmin (no en git)
//
// Persistencia: JSON con escritura atómica (writeFile.tmp + rename) + debounce.
// Sin npm deps. Sólo crypto, fs, path builtin de Node.
// =============================================================================

const crypto = require('crypto');
const fs     = require('fs');
const path   = require('path');

// ── Configuración ─────────────────────────────────────────────
const DEFAULT_USERS_FILE   = path.join(__dirname, 'users.json');
const DEFAULT_SUPERADMIN_FILE = path.join(__dirname, '.superadmin');

const PENDING_TTL_MS       = 10 * 60 * 1000;   // 10 min
const TOKEN_BYTES          = 4;                // 8 chars hex
const SCRYPT_N             = 16384;
const SCRYPT_KEY_LEN       = 32;
const SALT_BYTES           = 16;

const FLUSH_DEBOUNCE_MS    = 500;

const VALID_ROLES = new Set(['superadmin', 'admin', 'user']);

// Validadores
const RE_USERNAME = /^[a-zA-Z0-9_]{3,16}$/;
const RE_PASSWORD = /^[\x20-\x7E]{4,16}$/;       // ASCII imprimible 4-16 chars
const RE_NICK     = /^[\x20-\x7E]{1,16}$/;       // ASCII imprimible 1-16 chars

// ── Utilidades ────────────────────────────────────────────────
function now() { return Date.now(); }

function generateToken() {
    return crypto.randomBytes(TOKEN_BYTES).toString('hex').toUpperCase();
}

function hashPassword(password) {
    const salt = crypto.randomBytes(SALT_BYTES);
    const hash = crypto.scryptSync(password, salt, SCRYPT_KEY_LEN, { N: SCRYPT_N });
    return `scrypt$${SCRYPT_N}$${salt.toString('hex')}$${hash.toString('hex')}`;
}

function verifyPassword(password, stored) {
    if (typeof stored !== 'string') return false;
    const parts = stored.split('$');
    if (parts.length !== 4 || parts[0] !== 'scrypt') return false;
    const N    = parseInt(parts[1], 10);
    const salt = Buffer.from(parts[2], 'hex');
    const hash = Buffer.from(parts[3], 'hex');
    if (!N || !salt.length || !hash.length) return false;
    let candidate;
    try {
        candidate = crypto.scryptSync(password, salt, hash.length, { N });
    } catch (_) { return false; }
    if (candidate.length !== hash.length) return false;
    return crypto.timingSafeEqual(candidate, hash);
}

// ── AuthStore ─────────────────────────────────────────────────
class AuthStore {
    constructor(opts = {}) {
        this.usersFile     = opts.usersFile     || DEFAULT_USERS_FILE;
        this.superadminFile= opts.superadminFile|| DEFAULT_SUPERADMIN_FILE;

        /** @type {Map<string, {username, nick, role, passwordHash, createdAt, lastLogin}>} key=username (case-sensitive) */
        this.users = new Map();
        /** @type {Map<string, {username, nick, expiresAt}>} key=token */
        this.pending = new Map();

        this.superadminUsername = null;
        this.flushTimer = null;
        this.writingFlag = false;
    }

    // ─── Lifecycle ───────────────────────────────────────────
    init() {
        this._loadSuperadminFile();
        this._loadUsersFile();
        // Aplicar role superadmin si el username del archivo ya existía con otro rol
        if (this.superadminUsername) {
            const u = this.users.get(this.superadminUsername);
            if (u && u.role !== 'superadmin') {
                u.role = 'superadmin';
                this._scheduleFlush();
                console.log(`[auth] Promoted ${this.superadminUsername} to superadmin (from .superadmin file)`);
            }
        }
    }

    _loadSuperadminFile() {
        try {
            const txt = fs.readFileSync(this.superadminFile, 'utf8').trim();
            if (txt && RE_USERNAME.test(txt)) {
                this.superadminUsername = txt;
                console.log(`[auth] Superadmin file loaded: ${txt}`);
            } else if (txt) {
                console.warn(`[auth] Invalid username in ${this.superadminFile}: ${txt}`);
            }
        } catch (e) {
            if (e.code !== 'ENOENT') console.warn(`[auth] Cannot read ${this.superadminFile}: ${e.message}`);
        }
    }

    _loadUsersFile() {
        try {
            const raw = fs.readFileSync(this.usersFile, 'utf8');
            const data = JSON.parse(raw);
            const list = Array.isArray(data.users) ? data.users : [];
            for (const u of list) {
                if (u && u.username) this.users.set(u.username, u);
            }
            console.log(`[auth] Loaded ${this.users.size} users from ${this.usersFile}`);
        } catch (e) {
            if (e.code === 'ENOENT') {
                console.log(`[auth] No users.json yet, starting empty`);
            } else {
                console.error(`[auth] Failed to load ${this.usersFile}: ${e.message}`);
                throw e;
            }
        }
    }

    _scheduleFlush() {
        if (this.flushTimer) return;
        this.flushTimer = setTimeout(() => {
            this.flushTimer = null;
            this._flushNow().catch(err => console.error(`[auth] flush error: ${err.message}`));
        }, FLUSH_DEBOUNCE_MS);
    }

    async _flushNow() {
        if (this.writingFlag) {
            this._scheduleFlush();
            return;
        }
        this.writingFlag = true;
        try {
            const data = {
                schemaVer: 1,
                users: Array.from(this.users.values()),
            };
            const tmp = this.usersFile + '.tmp';
            await fs.promises.writeFile(tmp, JSON.stringify(data, null, 2), 'utf8');
            await fs.promises.rename(tmp, this.usersFile);
        } finally {
            this.writingFlag = false;
        }
    }

    /** Forzar flush sincrónico inmediato (útil para tests y shutdown) */
    flushSync() {
        if (this.flushTimer) { clearTimeout(this.flushTimer); this.flushTimer = null; }
        const data = {
            schemaVer: 1,
            users: Array.from(this.users.values()),
        };
        const tmp = this.usersFile + '.tmp';
        fs.writeFileSync(tmp, JSON.stringify(data, null, 2), 'utf8');
        fs.renameSync(tmp, this.usersFile);
    }

    // ─── Pending registrations ───────────────────────────────
    /**
     * Crea un registro pendiente. Si username ya existe activado o pendiente, falla.
     * Devuelve el token de activación (8 hex chars).
     */
    createPending(username, nick) {
        if (!RE_USERNAME.test(username)) {
            return { ok: false, reason: 'invalid_username' };
        }
        if (!RE_NICK.test(nick)) {
            return { ok: false, reason: 'invalid_nick' };
        }
        if (this.users.has(username)) {
            return { ok: false, reason: 'user_exists' };
        }
        // Limpiar tokens expirados antes de comprobar duplicados
        this._purgeExpiredPending();
        for (const p of this.pending.values()) {
            if (p.username === username) return { ok: false, reason: 'pending_already' };
        }
        const token = generateToken();
        this.pending.set(token, {
            username,
            nick,
            expiresAt: now() + PENDING_TTL_MS,
        });
        return { ok: true, token };
    }

    _purgeExpiredPending() {
        const t = now();
        for (const [tok, info] of this.pending) {
            if (info.expiresAt < t) this.pending.delete(tok);
        }
    }

    /**
     * Activa un registro pendiente: valida token + username, hashea password con scrypt,
     * crea el user en users.json. Si el username coincide con .superadmin, role=superadmin.
     */
    activatePending(username, token, password) {
        this._purgeExpiredPending();
        const info = this.pending.get(token);
        if (!info)                       return { ok: false, reason: 'invalid_token' };
        if (info.username !== username)  return { ok: false, reason: 'token_user_mismatch' };
        if (!RE_PASSWORD.test(password)) return { ok: false, reason: 'invalid_password' };
        if (this.users.has(username))    return { ok: false, reason: 'user_exists' };

        const role = (this.superadminUsername && this.superadminUsername === username) ? 'superadmin' : 'user';
        const user = {
            username,
            nick: info.nick,
            role,
            passwordHash: hashPassword(password),
            createdAt: now(),
            lastLogin: 0,
        };
        this.users.set(username, user);
        this.pending.delete(token);
        this._scheduleFlush();
        return { ok: true, user: this._publicUser(user) };
    }

    /** Cancelar un pending por su username (útil para admin flows) */
    cancelPending(username) {
        for (const [tok, info] of this.pending) {
            if (info.username === username) { this.pending.delete(tok); return true; }
        }
        return false;
    }

    // ─── Login ───────────────────────────────────────────────
    /**
     * Verifica credenciales. Devuelve user público si OK.
     */
    verifyLogin(username, password) {
        if (!RE_USERNAME.test(username)) return { ok: false, reason: 'invalid_username' };
        if (!RE_PASSWORD.test(password)) return { ok: false, reason: 'invalid_password' };
        const u = this.users.get(username);
        if (!u) return { ok: false, reason: 'not_found' };
        if (!verifyPassword(password, u.passwordHash)) return { ok: false, reason: 'bad_credentials' };
        u.lastLogin = now();
        this._scheduleFlush();
        return { ok: true, user: this._publicUser(u) };
    }

    // ─── Admin operations ────────────────────────────────────
    getUser(username) {
        const u = this.users.get(username);
        return u ? this._publicUser(u) : null;
    }

    setRole(username, role) {
        if (!VALID_ROLES.has(role)) return { ok: false, reason: 'invalid_role' };
        const u = this.users.get(username);
        if (!u) return { ok: false, reason: 'not_found' };
        u.role = role;
        this._scheduleFlush();
        return { ok: true, user: this._publicUser(u) };
    }

    /** Resetea un usuario a estado pendiente (genera nuevo token, borra password). */
    resetPassword(username) {
        const u = this.users.get(username);
        if (!u) return { ok: false, reason: 'not_found' };
        const token = generateToken();
        this.pending.set(token, {
            username: u.username,
            nick: u.nick,
            expiresAt: now() + PENDING_TTL_MS,
        });
        // Mantener el user pero invalidar password — se sobrescribirá al activatePending
        this.users.delete(username);
        this._scheduleFlush();
        return { ok: true, token };
    }

    listUsers() {
        return Array.from(this.users.values()).map(u => this._publicUser(u));
    }

    listPending() {
        this._purgeExpiredPending();
        return Array.from(this.pending.entries()).map(([token, info]) => ({
            token,
            username: info.username,
            nick: info.nick,
            expiresAt: info.expiresAt,
            ttlMs: info.expiresAt - now(),
        }));
    }

    _publicUser(u) {
        return {
            username: u.username,
            nick: u.nick,
            role: u.role,
            createdAt: u.createdAt,
            lastLogin: u.lastLogin,
        };
    }
}

module.exports = { AuthStore, hashPassword, verifyPassword, RE_USERNAME, RE_PASSWORD, RE_NICK, PENDING_TTL_MS };
