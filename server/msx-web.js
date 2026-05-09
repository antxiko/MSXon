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

const http = require('http');
const { URL } = require('url');
const { RE_USERNAME } = require('./auth-store');

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
  body { background:#000; color:#0f0; font-family:'Courier New',monospace; font-size:16px; padding:20px; max-width:520px; margin:0 auto; }
  h1   { color:#0f0; border-bottom:2px solid #0f0; padding-bottom:8px; letter-spacing:2px; }
  h2   { color:#0f0; margin-top:0; }
  p    { line-height:1.5; }
  .err { color:#f00; border:1px solid #f00; padding:10px; margin:10px 0; }
  .ok  { color:#0f0; border:1px solid #0f0; padding:10px; margin:10px 0; }
  label{ display:block; margin:12px 0 4px; font-weight:bold; }
  input[type=password]{ background:#000; color:#0f0; border:1px solid #0f0; padding:8px; width:100%; box-sizing:border-box; font-family:'Courier New',monospace; font-size:16px; }
  button{ background:#0f0; color:#000; border:none; padding:12px 20px; font-family:'Courier New',monospace; font-weight:bold; font-size:16px; margin-top:16px; cursor:pointer; letter-spacing:1px; }
  button:hover{ background:#fff; }
  small{ color:#080; }
  a    { color:#0f0; }
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
    `);
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
 * @param {AuthStore} opts.authStore — instancia ya inicializada
 * @param {number}   [opts.port=8080]
 * @param {string}   [opts.host='127.0.0.1'] — bind interface (Caddy proxea desde localhost)
 * @returns {http.Server}
 */
function mountWebServer({ authStore, port = 8080, host = '127.0.0.1' }) {
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
