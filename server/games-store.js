'use strict';

// =============================================================================
// games-store.js — Catálogo de juegos MSXon
//
// Gestiona:
//  - Lista de juegos en games.json (id, name, com, max, proto, visibility)
//  - Visibility: public | private | disabled
//  - Filtrado por rol del usuario (user / admin / superadmin)
//
// Persistencia: JSON con escritura atómica (writeFile.tmp + rename) + debounce.
// Sin npm deps.
// =============================================================================

const fs   = require('fs');
const path = require('path');

const DEFAULT_GAMES_FILE = path.join(__dirname, 'games.json');
const FLUSH_DEBOUNCE_MS  = 500;

const VALID_VISIBILITY = new Set(['public', 'private', 'disabled']);
const VALID_ROLES      = new Set(['user', 'admin', 'superadmin', 'service']);

// Validaciones de campo
function isValidId(id)   { return Number.isInteger(id) && id >= 0 && id <= 255; }
function isValidName(n)  { return typeof n === 'string' && n.length >= 1 && n.length <= 32; }
function isValidCom(c)   { return typeof c === 'string' && /^[A-Z0-9_]{1,8}$/i.test(c); }
function isValidMax(m)   { return Number.isInteger(m) && m >= 1 && m <= 16; }
function isValidProto(p) { return Number.isInteger(p) && p >= 0 && p <= 255; }
function isValidVis(v)   { return VALID_VISIBILITY.has(v); }

class GamesStore {
    constructor(opts = {}) {
        this.gamesFile = opts.gamesFile || DEFAULT_GAMES_FILE;
        /** @type {Map<number, {id, name, com, max, proto, visibility}>} key=id (number) */
        this.games = new Map();
        this.flushTimer = null;
        this.writingFlag = false;
    }

    // ─── Lifecycle ───────────────────────────────────────────
    init() { this._loadFile(); }

    _loadFile() {
        try {
            const raw  = fs.readFileSync(this.gamesFile, 'utf8');
            const data = JSON.parse(raw);
            const list = Array.isArray(data.games) ? data.games : [];
            for (const g of list) {
                if (this._validate(g)) this.games.set(g.id, { ...g });
                else console.warn(`[games] Ignoring invalid entry: ${JSON.stringify(g)}`);
            }
            console.log(`[games] Loaded ${this.games.size} games from ${this.gamesFile}`);
        } catch (e) {
            if (e.code === 'ENOENT') {
                console.log(`[games] No games.json yet, starting empty`);
            } else {
                console.error(`[games] Failed to load ${this.gamesFile}: ${e.message}`);
                throw e;
            }
        }
    }

    _validate(g) {
        return g
            && isValidId(g.id)
            && isValidName(g.name)
            && isValidCom(g.com)
            && isValidMax(g.max)
            && isValidProto(g.proto)
            && isValidVis(g.visibility);
    }

    _scheduleFlush() {
        if (this.flushTimer) return;
        this.flushTimer = setTimeout(() => {
            this.flushTimer = null;
            this._flushNow().catch(err => console.error(`[games] flush error: ${err.message}`));
        }, FLUSH_DEBOUNCE_MS);
    }

    async _flushNow() {
        if (this.writingFlag) { this._scheduleFlush(); return; }
        this.writingFlag = true;
        try {
            const data = {
                schemaVer: 1,
                games: Array.from(this.games.values()).sort((a, b) => a.id - b.id),
            };
            const tmp = this.gamesFile + '.tmp';
            await fs.promises.writeFile(tmp, JSON.stringify(data, null, 2), 'utf8');
            await fs.promises.rename(tmp, this.gamesFile);
        } finally {
            this.writingFlag = false;
        }
    }

    flushSync() {
        if (this.flushTimer) { clearTimeout(this.flushTimer); this.flushTimer = null; }
        const data = {
            schemaVer: 1,
            games: Array.from(this.games.values()).sort((a, b) => a.id - b.id),
        };
        const tmp = this.gamesFile + '.tmp';
        fs.writeFileSync(tmp, JSON.stringify(data, null, 2), 'utf8');
        fs.renameSync(tmp, this.gamesFile);
    }

    // ─── Read ────────────────────────────────────────────────
    list() {
        return Array.from(this.games.values()).sort((a, b) => a.id - b.id);
    }

    /**
     * Devuelve la lista filtrada para un rol concreto:
     *   - user            → solo public
     *   - admin/superadmin/service → public + private
     *   - (nadie ve disabled)
     */
    listVisibleFor(role) {
        if (!VALID_ROLES.has(role)) role = 'user';
        const isPriv = role === 'admin' || role === 'superadmin' || role === 'service';
        return this.list().filter(g => {
            if (g.visibility === 'disabled') return false;
            if (g.visibility === 'public')   return true;
            if (g.visibility === 'private')  return isPriv;
            return false;
        });
    }

    get(id) {
        return this.games.get(id) || null;
    }

    /** Útil para validar peticiones del cliente (ROOM_CREATE/JOIN si añadimos enforcement) */
    canSee(role, id) {
        const g = this.games.get(id);
        if (!g) return false;
        if (g.visibility === 'disabled') return false;
        if (g.visibility === 'public')   return true;
        if (g.visibility === 'private')  return role === 'admin' || role === 'superadmin' || role === 'service';
        return false;
    }

    // ─── Write ───────────────────────────────────────────────
    /** Crea o sustituye un juego. Devuelve {ok, reason?} */
    addOrUpdate(game) {
        if (!this._validate(game)) return { ok: false, reason: 'invalid' };
        this.games.set(game.id, { ...game });
        this._scheduleFlush();
        return { ok: true };
    }

    setVisibility(id, visibility) {
        if (!isValidVis(visibility)) return { ok: false, reason: 'invalid_visibility' };
        const g = this.games.get(id);
        if (!g) return { ok: false, reason: 'not_found' };
        g.visibility = visibility;
        this._scheduleFlush();
        return { ok: true, game: { ...g } };
    }

    remove(id) {
        if (!this.games.has(id)) return { ok: false, reason: 'not_found' };
        this.games.delete(id);
        this._scheduleFlush();
        return { ok: true };
    }
}

module.exports = { GamesStore, VALID_VISIBILITY };
