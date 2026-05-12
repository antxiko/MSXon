'use strict';

// =============================================================================
// msx-web.js — Servidor HTTP minimal para MSXon
//
// Sirve el endpoint /r?u=X&t=Y donde el usuario, tras escanear el QR generado
// en el MSX, completa su registro definiendo password.
//
// Detrás de Caddy en el VPS (HTTPS termination + reverse_proxy a localhost:8080).
// En desarrollo local se accede directo a http://localhost:8080.
//
// Sin npm deps. Sólo http + url builtin de Node.
// =============================================================================

const http   = require('http');
const crypto = require('crypto');
const fs     = require('fs');
const path   = require('path');
const { spawn } = require('child_process');
const { URL } = require('url');
const { RE_USERNAME } = require('./auth-store');
const { VALID_VISIBILITY } = require('./games-store');

// ── Cookie secret (persistente para no invalidar sesiones al restart) ──
const COOKIE_SECRET_FILE = path.join(__dirname, '.cookie-secret');
const COOKIE_NAME        = 'msxon_admin';
const COOKIE_TTL_MS      = 24 * 3600 * 1000;
const QR_BASE_URL        = process.env.MSX_QR_BASE_URL || 'https://msxon.nosignalbbs.com/r';

function loadCookieSecret() {
    if (process.env.MSX_COOKIE_SECRET) return process.env.MSX_COOKIE_SECRET;
    try { return fs.readFileSync(COOKIE_SECRET_FILE, 'utf8').trim(); }
    catch (_) {
        const s = crypto.randomBytes(32).toString('hex');
        try { fs.writeFileSync(COOKIE_SECRET_FILE, s, { mode: 0o600 }); } catch (_) {}
        return s;
    }
}
const COOKIE_SECRET = loadCookieSecret();

function signSession(payload) {
    const json = Buffer.from(JSON.stringify(payload)).toString('base64url');
    const mac = crypto.createHmac('sha256', COOKIE_SECRET).update(json).digest('base64url');
    return `${json}.${mac}`;
}

function verifySession(cookieValue) {
    if (!cookieValue || typeof cookieValue !== 'string') return null;
    const dot = cookieValue.lastIndexOf('.');
    if (dot < 0) return null;
    const json = cookieValue.slice(0, dot);
    const mac  = cookieValue.slice(dot + 1);
    const expected = crypto.createHmac('sha256', COOKIE_SECRET).update(json).digest('base64url');
    try {
        if (!crypto.timingSafeEqual(Buffer.from(mac), Buffer.from(expected))) return null;
    } catch (_) { return null; }
    let payload;
    try { payload = JSON.parse(Buffer.from(json, 'base64url').toString('utf8')); }
    catch (_) { return null; }
    if (!payload || !payload.exp || payload.exp < Date.now()) return null;
    return payload;
}

function parseCookies(req) {
    const out = {};
    const h = req.headers.cookie;
    if (!h) return out;
    for (const part of h.split(';')) {
        const eq = part.indexOf('=');
        if (eq < 0) continue;
        const k = part.slice(0, eq).trim();
        const v = part.slice(eq + 1).trim();
        out[k] = v;
    }
    return out;
}

function getAdminSession(req) {
    const cookies = parseCookies(req);
    const session = verifySession(cookies[COOKIE_NAME]);
    if (!session) return null;
    if (session.role !== 'admin' && session.role !== 'superadmin') return null;
    return session;
}

// ── HTML escape ───────────────────────────────────────────────
function htmlEscape(s) {
    return String(s)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

// ── HTML templates (estética BBS retro: verde sobre negro) ────
const CSS = `
  body { background:#000; color:#0f0; font-family:'Courier New',monospace; font-size:16px; padding:20px; max-width:720px; margin:0 auto; }
  h1   { color:#0f0; border-bottom:2px solid #0f0; padding-bottom:8px; letter-spacing:2px; }
  h2   { color:#0f0; margin-top:24px; }
  p    { line-height:1.5; }
  .err { color:#f00; border:1px solid #f00; padding:10px; margin:10px 0; }
  .ok  { color:#0f0; border:1px solid #0f0; padding:10px; margin:10px 0; }
  label{ display:block; margin:12px 0 4px; font-weight:bold; }
  input, select { background:#000; color:#0f0; border:1px solid #0f0; padding:6px; box-sizing:border-box; font-family:'Courier New',monospace; font-size:14px; }
  input[type=password], input[type=text]{ width:100%; padding:8px; font-size:16px; }
  button{ background:#0f0; color:#000; border:none; padding:8px 14px; font-family:'Courier New',monospace; font-weight:bold; font-size:14px; margin-top:6px; cursor:pointer; letter-spacing:1px; }
  button:hover{ background:#fff; }
  button.big { padding:12px 20px; font-size:16px; margin-top:16px; }
  button.danger { background:#f00; color:#000; }
  button.danger:hover { background:#fff; color:#f00; }
  small{ color:#080; }
  a    { color:#0f0; }
  table{ border-collapse:collapse; width:100%; margin:8px 0 16px; }
  th,td{ border:1px solid #0f0; padding:6px 8px; text-align:left; font-size:14px; }
  th   { background:#020; }
  .tag-private { color:#ff0; }
  .tag-disabled{ color:#666; }
  .nav { margin-bottom:18px; }
  .nav a { margin-right:14px; }
  form.inline { display:inline; }
`;

function pageWrap(title, body) {
    return '<!doctype html>\n<html lang="es"><head>'
        + '<meta charset="utf-8">'
        + '<meta name="viewport" content="width=device-width,initial-scale=1">'
        + `<title>${htmlEscape(title)}</title>`
        + `<style>${CSS}</style>`
        + '</head><body>'
        + body
        + '</body></html>';
}

function pageRegisterForm(username, token, errMsg) {
    const err = errMsg ? `<div class="err">${htmlEscape(errMsg)}</div>` : '';
    return pageWrap('MSXon — Activar cuenta', `
        <h1>MSXon</h1>
        <h2>Activar cuenta</h2>
        <p>Hola <strong>${htmlEscape(username)}</strong>. Define una contraseña para tu cuenta.</p>
        ${err}
        <form method="POST" action="/r?u=${encodeURIComponent(username)}&amp;t=${encodeURIComponent(token)}">
            <label for="p1">Contraseña (4-16 caracteres)</label>
            <input type="password" id="p1" name="password" minlength="4" maxlength="16" required autofocus>
            <label for="p2">Repite la contraseña</label>
            <input type="password" id="p2" name="confirm" minlength="4" maxlength="16" required>
            <button type="submit">ACTIVAR CUENTA</button>
        </form>
        <p><small>Tras activar podrás loggearte en el MSXon de tu MSX.</small></p>
    `);
}

function pageActivated(username, role) {
    return pageWrap('MSXon — Cuenta activada', `
        <h1>MSXon</h1>
        <div class="ok">
            <h2>OK</h2>
            <p>La cuenta <strong>${htmlEscape(username)}</strong> ha sido activada.</p>
            <p>Rol asignado: <strong>${htmlEscape(role)}</strong></p>
        </div>
        <p>Vuelve al MSX y haz LOGIN con tu username y la contraseña que acabas de elegir.</p>
    `);
}

function pageError(title, msg) {
    return pageWrap(title, `
        <h1>MSXon</h1>
        <div class="err">
            <h2>${htmlEscape(title)}</h2>
            <p>${htmlEscape(msg)}</p>
        </div>
        <p>Vuelve al MSX y reintenta el registro para obtener un nuevo QR.</p>
    `);
}

function pageLanding() {
    return pageWrap('MSXon', `
        <h1>MSXon</h1>
        <p>Plataforma de juegos online para MSX.</p>
        <p>Esta página recibe los registros desde el lobby MSXon de tu MSX.</p>
        <p><small>Para crear cuenta: arranca MSXon en tu MSX, elige "Registrarse", y escanea el QR que aparece en pantalla.</small></p>
        <p><a href="/admin">[ Admin ]</a></p>
    `);
}

// ── Admin pages ───────────────────────────────────────────────
function pageAdminLogin(errMsg) {
    const err = errMsg ? `<div class="err">${htmlEscape(errMsg)}</div>` : '';
    return pageWrap('MSXon — Admin Login', `
        <h1>MSXon Admin</h1>
        <h2>Login</h2>
        ${err}
        <form method="POST" action="/admin/login">
            <label for="u">Usuario</label>
            <input type="text" id="u" name="username" required autofocus>
            <label for="p">Contraseña</label>
            <input type="password" id="p" name="password" required>
            <button type="submit" class="big">ENTRAR</button>
        </form>
        <p><small>Sólo cuentas con rol <b>admin</b> o <b>superadmin</b>.</small></p>
    `);
}

function pageAdminDashboard(session, users, games, pending, msg, msgType) {
    const banner = msg
        ? `<div class="${msgType === 'err' ? 'err' : 'ok'}">${htmlEscape(msg)}</div>`
        : '';

    const usersRows = users.map(u => `
        <tr>
            <td>${htmlEscape(u.username)}</td>
            <td>${htmlEscape(u.nick || '')}</td>
            <td><b>${htmlEscape(u.role)}</b></td>
            <td><small>${u.lastLogin ? new Date(u.lastLogin).toISOString().slice(0,16).replace('T',' ') : 'never'}</small></td>
            <td>
                <form class="inline" method="POST" action="/admin/promote">
                    <input type="hidden" name="username" value="${htmlEscape(u.username)}">
                    <select name="role">
                        <option value="user"${u.role==='user'?' selected':''}>user</option>
                        <option value="admin"${u.role==='admin'?' selected':''}>admin</option>
                        <option value="superadmin"${u.role==='superadmin'?' selected':''}>superadmin</option>
                    </select>
                    <button type="submit">set role</button>
                </form>
                <form class="inline" method="POST" action="/admin/reset-password" onsubmit="return confirm('Reset password de ${htmlEscape(u.username)}?');">
                    <input type="hidden" name="username" value="${htmlEscape(u.username)}">
                    <button type="submit" class="danger">reset pw</button>
                </form>
            </td>
        </tr>`).join('');

    const pendingRows = pending.length === 0 ? '<tr><td colspan="3"><small>(ninguno)</small></td></tr>' : pending.map(p => `
        <tr>
            <td>${htmlEscape(p.username)}</td>
            <td>${htmlEscape(p.nick)}</td>
            <td><a href="${QR_BASE_URL}?u=${encodeURIComponent(p.username)}&t=${encodeURIComponent(p.token)}">${htmlEscape(p.token)}</a> · ${Math.max(0, Math.floor(p.ttlMs/60000))} min</td>
        </tr>`).join('');

    const VIS_OPTIONS = ['public', 'private', 'disabled'];
    const gamesRows = games.map(g => {
        const idHex = '0x' + g.id.toString(16).padStart(2, '0');
        const cls = g.visibility === 'private' ? 'tag-private' : (g.visibility === 'disabled' ? 'tag-disabled' : '');
        const opts = VIS_OPTIONS.map(v => `<option value="${v}"${v===g.visibility?' selected':''}>${v}</option>`).join('');
        return `
            <tr>
                <td>${idHex}</td>
                <td>${htmlEscape(g.name)}</td>
                <td><small>${htmlEscape(g.com)}</small></td>
                <td><small>${g.max}p · proto ${g.proto}</small></td>
                <td class="${cls}"><b>${g.visibility}</b></td>
                <td>
                    <form class="inline" method="POST" action="/admin/set-visibility">
                        <input type="hidden" name="id" value="${g.id}">
                        <select name="visibility">${opts}</select>
                        <button type="submit">set</button>
                    </form>
                </td>
            </tr>`;
    }).join('');

    return pageWrap('MSXon — Admin', `
        <h1>MSXon Admin</h1>
        <div class="nav">
            <small>Sesión: <b>${htmlEscape(session.username)}</b> (${htmlEscape(session.role)})</small>
            &nbsp;·&nbsp;
            <a href="/admin/logout">Logout</a>
        </div>
        ${banner}

        <h2>Usuarios (${users.length})</h2>
        <table>
            <thead><tr><th>user</th><th>nick</th><th>role</th><th>last login</th><th>acciones</th></tr></thead>
            <tbody>${usersRows}</tbody>
        </table>

        <h2>Pendientes de activación (${pending.length})</h2>
        <table>
            <thead><tr><th>user</th><th>nick</th><th>token (URL)</th></tr></thead>
            <tbody>${pendingRows}</tbody>
        </table>

        <h2>Catálogo de juegos (${games.length})</h2>
        <table>
            <thead><tr><th>id</th><th>nombre</th><th>com</th><th>info</th><th>visibility</th><th>acciones</th></tr></thead>
            <tbody>${gamesRows}</tbody>
        </table>

        <h2>Sistema</h2>
        <form method="POST" action="/admin/restart-server" onsubmit="return confirm('Reiniciar el servidor msx-server? (corta sesiones activas)');">
            <button type="submit" class="danger big">RESTART msx-server</button>
        </form>
        <p><small>El restart desconecta todos los clientes (incluido este). Se reconecta solo en ~5s.</small></p>
    `);
}

function pageAdminRestarting() {
    return pageWrap('MSXon — Restarting', `
        <h1>MSXon Admin</h1>
        <div class="ok">
            <p>Reiniciando msx-server…</p>
            <p>Espera ~5 segundos y refresca.</p>
        </div>
        <script>setTimeout(function(){location.href='/admin';}, 6000);</script>
        <p><a href="/admin">Volver al admin</a></p>
    `);
}

function setSessionCookie(res, payload) {
    const value = signSession(payload);
    res.setHeader('Set-Cookie',
        `${COOKIE_NAME}=${value}; HttpOnly; SameSite=Lax; Path=/; Max-Age=${Math.floor(COOKIE_TTL_MS/1000)}`);
}

function clearSessionCookie(res) {
    res.setHeader('Set-Cookie', `${COOKIE_NAME}=; HttpOnly; SameSite=Lax; Path=/; Max-Age=0`);
}

function redirect(res, location) {
    res.writeHead(303, { Location: location, 'Cache-Control': 'no-store' });
    res.end();
}

// ── Handlers ──────────────────────────────────────────────────
function readBody(req, maxBytes) {
    return new Promise((resolve, reject) => {
        const chunks = [];
        let total = 0;
        req.on('data', (c) => {
            total += c.length;
            if (total > maxBytes) {
                req.destroy();
                reject(new Error('body_too_large'));
                return;
            }
            chunks.push(c);
        });
        req.on('end', () => resolve(Buffer.concat(chunks)));
        req.on('error', reject);
    });
}

function parseFormBody(buf) {
    // application/x-www-form-urlencoded
    const text = buf.toString('utf8');
    const out = {};
    if (!text) return out;
    for (const part of text.split('&')) {
        const eq = part.indexOf('=');
        if (eq < 0) continue;
        const k = decodeURIComponent(part.slice(0, eq).replace(/\+/g, ' '));
        const v = decodeURIComponent(part.slice(eq + 1).replace(/\+/g, ' '));
        out[k] = v;
    }
    return out;
}

function send(res, status, contentType, body) {
    res.writeHead(status, {
        'Content-Type': contentType,
        'Content-Length': Buffer.byteLength(body),
        'Cache-Control': 'no-store',
        'X-Content-Type-Options': 'nosniff',
    });
    res.end(body);
}

function sendHtml(res, status, html) {
    send(res, status, 'text/html; charset=utf-8', html);
}

// ── Mount ─────────────────────────────────────────────────────
/**
 * @param {object} opts
 * @param {AuthStore}  opts.authStore  — instancia ya inicializada
 * @param {GamesStore} opts.gamesStore — instancia ya inicializada (para /admin)
 * @param {number}     [opts.port=8080]
 * @param {string}     [opts.host='127.0.0.1'] — bind interface (Caddy proxea desde localhost)
 * @returns {http.Server}
 */
function mountWebServer({ authStore, gamesStore, port = 8080, host = '127.0.0.1' }) {
    if (!authStore) throw new Error('mountWebServer: authStore is required');

    const server = http.createServer(async (req, res) => {
        try {
            // URL parser necesita una base, da igual cuál
            const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
            const pathname = url.pathname;

            // ── GET / (landing) ────────────────────────────
            if (req.method === 'GET' && pathname === '/') {
                return sendHtml(res, 200, pageLanding());
            }

            // ── GET /r?u=X&t=Y (form de activación) ──────
            if (req.method === 'GET' && pathname === '/r') {
                const username = url.searchParams.get('u') || '';
                const token    = url.searchParams.get('t') || '';
                if (!RE_USERNAME.test(username) || !/^[A-F0-9]{8}$/.test(token)) {
                    return sendHtml(res, 400, pageError('Enlace inválido', 'El enlace que has usado no es correcto.'));
                }
                // Comprobar que existe pending para ese token+username
                const valid = Array.from(authStore.pending.entries()).some(
                    ([t, info]) => t === token && info.username === username && info.expiresAt > Date.now()
                );
                if (!valid) {
                    return sendHtml(res, 410, pageError('Enlace caducado', 'El token ha expirado o ya fue usado. Vuelve al MSX y reintenta el registro.'));
                }
                return sendHtml(res, 200, pageRegisterForm(username, token, null));
            }

            // ── POST /r?u=X&t=Y (procesa form) ────────────
            if (req.method === 'POST' && pathname === '/r') {
                const username = url.searchParams.get('u') || '';
                const token    = url.searchParams.get('t') || '';
                if (!RE_USERNAME.test(username) || !/^[A-F0-9]{8}$/.test(token)) {
                    return sendHtml(res, 400, pageError('Enlace inválido', 'El enlace que has usado no es correcto.'));
                }
                let body;
                try { body = await readBody(req, 4096); }
                catch (e)   { return sendHtml(res, 413, pageError('Demasiado grande', 'La petición es demasiado grande.')); }
                const form = parseFormBody(body);
                const password = form.password || '';
                const confirm  = form.confirm  || '';
                if (password !== confirm) {
                    return sendHtml(res, 400, pageRegisterForm(username, token, 'Las contraseñas no coinciden.'));
                }
                const r = authStore.activatePending(username, token, password);
                if (!r.ok) {
                    const reasons = {
                        invalid_token:       'Token inválido o caducado.',
                        token_user_mismatch: 'El enlace no corresponde al usuario.',
                        invalid_password:    'La contraseña no cumple los requisitos (4-16 caracteres ASCII imprimibles).',
                        user_exists:         'Esa cuenta ya estaba activada.',
                    };
                    const msg = reasons[r.reason] || 'No se pudo activar la cuenta.';
                    return sendHtml(res, 400, pageRegisterForm(username, token, msg));
                }
                return sendHtml(res, 200, pageActivated(r.user.username, r.user.role));
            }

            // ── /admin/login ─────────────────────────────
            if (req.method === 'GET' && pathname === '/admin/login') {
                return sendHtml(res, 200, pageAdminLogin(null));
            }
            if (req.method === 'POST' && pathname === '/admin/login') {
                let body;
                try { body = await readBody(req, 4096); }
                catch (_) { return sendHtml(res, 413, pageError('Demasiado grande', 'La petición es demasiado grande.')); }
                const form = parseFormBody(body);
                const username = form.username || '';
                const password = form.password || '';
                const r = authStore.verifyLogin(username, password);
                if (!r.ok) return sendHtml(res, 401, pageAdminLogin('Credenciales no válidas.'));
                if (r.user.role !== 'admin' && r.user.role !== 'superadmin') {
                    return sendHtml(res, 403, pageAdminLogin('Esta cuenta no tiene rol admin/superadmin.'));
                }
                setSessionCookie(res, { username: r.user.username, role: r.user.role, exp: Date.now() + COOKIE_TTL_MS });
                return redirect(res, '/admin');
            }

            // ── /admin/logout ────────────────────────────
            if (pathname === '/admin/logout') {
                clearSessionCookie(res);
                return redirect(res, '/admin/login');
            }

            // ── A partir de aquí, requiere admin session ─
            if (pathname === '/admin' || pathname.startsWith('/admin/')) {
                const session = getAdminSession(req);
                if (!session) return redirect(res, '/admin/login');

                // GET /admin (dashboard)
                if (req.method === 'GET' && pathname === '/admin') {
                    if (!gamesStore) {
                        return sendHtml(res, 500, pageError('Sin gamesStore', 'El servidor no tiene gamesStore montado.'));
                    }
                    const url2 = url;
                    const flash = url2.searchParams.get('msg');
                    const flashType = url2.searchParams.get('t');
                    return sendHtml(res, 200, pageAdminDashboard(
                        session,
                        authStore.listUsers(),
                        gamesStore.list(),
                        authStore.listPending(),
                        flash,
                        flashType,
                    ));
                }

                // POST /admin/promote
                if (req.method === 'POST' && pathname === '/admin/promote') {
                    let body; try { body = await readBody(req, 4096); } catch (_) { return redirect(res, '/admin?msg=body_too_large&t=err'); }
                    const f = parseFormBody(body);
                    const r = authStore.setRole(f.username, f.role);
                    if (!r.ok) return redirect(res, `/admin?msg=${encodeURIComponent('promote: ' + r.reason)}&t=err`);
                    authStore.flushSync();
                    return redirect(res, `/admin?msg=${encodeURIComponent(f.username + ' → ' + r.user.role)}&t=ok`);
                }

                // POST /admin/reset-password
                if (req.method === 'POST' && pathname === '/admin/reset-password') {
                    let body; try { body = await readBody(req, 4096); } catch (_) { return redirect(res, '/admin?msg=body_too_large&t=err'); }
                    const f = parseFormBody(body);
                    const r = authStore.resetPassword(f.username);
                    if (!r.ok) return redirect(res, `/admin?msg=${encodeURIComponent('reset: ' + r.reason)}&t=err`);
                    authStore.flushSync();
                    const msg = `${f.username} reseteado. Token: ${r.token} (10 min). URL: ${QR_BASE_URL}?u=${encodeURIComponent(f.username)}&t=${r.token}`;
                    return redirect(res, `/admin?msg=${encodeURIComponent(msg)}&t=ok`);
                }

                // POST /admin/set-visibility
                if (req.method === 'POST' && pathname === '/admin/set-visibility') {
                    if (!gamesStore) return redirect(res, '/admin?msg=no_gamesstore&t=err');
                    let body; try { body = await readBody(req, 4096); } catch (_) { return redirect(res, '/admin?msg=body_too_large&t=err'); }
                    const f = parseFormBody(body);
                    const id = parseInt(f.id, 10);
                    if (!Number.isInteger(id)) return redirect(res, '/admin?msg=invalid_id&t=err');
                    if (!VALID_VISIBILITY.has(f.visibility)) return redirect(res, '/admin?msg=invalid_visibility&t=err');
                    const r = gamesStore.setVisibility(id, f.visibility);
                    if (!r.ok) return redirect(res, `/admin?msg=${encodeURIComponent('vis: ' + r.reason)}&t=err`);
                    gamesStore.flushSync();
                    return redirect(res, `/admin?msg=${encodeURIComponent(r.game.name + ' → ' + f.visibility)}&t=ok`);
                }

                // POST /admin/restart-server
                if (req.method === 'POST' && pathname === '/admin/restart-server') {
                    sendHtml(res, 200, pageAdminRestarting());
                    setTimeout(() => {
                        try {
                            const child = spawn('systemctl', ['restart', 'msx-server'], { detached: true, stdio: 'ignore' });
                            child.unref();
                        } catch (e) { console.error('[admin] restart failed:', e.message); }
                    }, 200);
                    return;
                }
            }

            // ── 404 ──────────────────────────────────────
            return sendHtml(res, 404, pageError('No encontrado', 'La ruta solicitada no existe.'));

        } catch (err) {
            console.error('[web] handler error:', err && err.stack || err);
            try { sendHtml(res, 500, pageError('Error interno', 'Algo ha fallado en el servidor.')); }
            catch (_) { /* res ya cerrada */ }
        }
    });

    server.listen(port, host, () => {
        console.log(`MSX Web Server escuchando en ${host}:${port}`);
    });

    return server;
}

module.exports = { mountWebServer };
