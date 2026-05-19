'use strict';

// =============================================================================
// msx-gameserver.js — MSXon Game Server v2.0
// Stack: Node.js 20 LTS · TCP raw · Sin dependencias externas
//
// Protocolo: header binario 6 bytes + payload 0..255 bytes
//   [0x46][0x4D][CMD][ROOM][PID][LEN][...payload...]
//    'F'   'M'
//
// Arranque:
//   node msx-gameserver.js
//
// Servicio systemd:
//   sudo systemctl start msx-server
// =============================================================================

const net    = require('net');
const crypto = require('crypto');
const { AuthStore }      = require('./auth-store');
const { GamesStore }     = require('./games-store');
const { mountWebServer } = require('./msx-web');

// ── Configuración ─────────────────────────────────────────────
const PORT               = 9876;
const WEB_PORT           = parseInt(process.env.MSX_WEB_PORT || '8080', 10);
const WEB_HOST           = process.env.MSX_WEB_HOST || '127.0.0.1';
const AUTH_TOKEN         = Buffer.from(
  (process.env.MSX_AUTH_TOKEN || 'DEADBEEF'), 'hex'
);
const MAX_PLAYERS_LIMIT  = 16;       // Techo absoluto por sala
const TIMEOUT_MS         = 90_000;   // 90s — generoso para el Z80

// ── Auth store (shared con el HTTP server) ────────────────────
const authStore = new AuthStore();
authStore.init();

// ── Games store (catálogo) ────────────────────────────────────
const gamesStore = new GamesStore();
gamesStore.init();

// ── Protocolo ─────────────────────────────────────────────────
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
  ROOM_LIST:      0x26,
  PLAYER_JOINED:  0x30,
  PLAYER_LEFT:    0x31,
  GAME_START:     0x32,
  GAME_END:       0x33,
  STATE_UPDATE:   0x40,
  WORLD_STATE:    0x41,
  // ── Auth de usuario (sesion-aware, distinto del AUTH legacy 0x10) ─
  LOGIN:          0x13,
  LOGIN_OK:       0x14,
  LOGIN_FAIL:     0x15,
  REGISTER:       0x16,
  REG_PENDING:    0x17,
  REG_FAIL:       0x18,
  LOGOUT:         0x19,
  SESSION_RESUME: 0x1A,
  GAME_LIST:      0x27,
  PLAYER_LIST:    0x28,  // C->S sin payload; S->C [N][PID,NLEN,nick] x N
  // ── Chat global (solo usuarios loggeados, no ghosts) ─
  CHAT_SEND:      0x50,  // C->S [LEN][texto utf8]
  CHAT_RECV:      0x51,  // S->C [NLEN][nick][MLEN][texto]
  CHAT_HISTORY:   0x52,  // C->S sin payload; S->C envia varios CHAT_RECV
  ERROR:          0xFF,
};

// ── Roles ─────────────────────────────────────────────────────
const ROLE_BYTE = { user: 0x01, admin: 0x02, superadmin: 0x03 };

// ── Chat global ───────────────────────────────────────────────
const CHAT_BUFFER_SIZE = 50;
const CHAT_MAX_MSG_LEN = 80;
const chatBuffer = []; // { nick, msg } circular
const chatSockets = new Set(); // sockets de usuarios humanos loggeados

function buildChatRecvPacket(nick, msg) {
  const nickB = Buffer.from(String(nick).slice(0, 16), 'utf8');
  const msgB  = Buffer.from(String(msg).slice(0, CHAT_MAX_MSG_LEN), 'utf8');
  const payload = Buffer.alloc(1 + nickB.length + 1 + msgB.length);
  let off = 0;
  payload[off++] = nickB.length;
  nickB.copy(payload, off); off += nickB.length;
  payload[off++] = msgB.length;
  msgB.copy(payload, off);
  return buildPacket(CMD.CHAT_RECV, 0, 0, payload);
}

function pushChat(nick, msg) {
  chatBuffer.push({ nick, msg });
  if (chatBuffer.length > CHAT_BUFFER_SIZE) chatBuffer.shift();
}

function broadcastChat(nick, msg) {
  pushChat(nick, msg);
  const pkt = buildChatRecvPacket(nick, msg);
  for (const s of chatSockets) {
    try { s.write(pkt); } catch (_) {}
  }
}

// ── Sesiones de usuario (4 byte sessionId, TTL 5min) ──────────
const SESSION_TTL_MS = 5 * 60 * 1000;
const sessions = new Map(); // sessionId hex string -> { username, role, expiresAt }

function generateSessionId() {
  return crypto.randomBytes(4);
}

function purgeExpiredSessions() {
  const now = Date.now();
  for (const [id, info] of sessions) {
    if (info.expiresAt < now) sessions.delete(id);
  }
}

// Reasons mapeados al wire para LOGIN_FAIL / REG_FAIL
const LOGIN_FAIL_BAD_CREDS    = 1;
const LOGIN_FAIL_NOT_FOUND    = 2;
const LOGIN_FAIL_PENDING_SET  = 5;
const REG_FAIL_USER_EXISTS    = 1;
const REG_FAIL_INVALID_CHARS  = 2;
const REG_FAIL_PENDING_ALREADY= 4;

function mapLoginFailReason(reason) {
  switch (reason) {
    case 'bad_credentials':
    case 'invalid_password': // no leak: tratar como bad creds
    case 'invalid_username':
      return LOGIN_FAIL_BAD_CREDS;
    case 'not_found':         return LOGIN_FAIL_NOT_FOUND;
    case 'pending_setup':     return LOGIN_FAIL_PENDING_SET;
    default:                  return LOGIN_FAIL_BAD_CREDS;
  }
}

function mapRegFailReason(reason) {
  switch (reason) {
    case 'user_exists':       return REG_FAIL_USER_EXISTS;
    case 'invalid_username':
    case 'invalid_nick':      return REG_FAIL_INVALID_CHARS;
    case 'pending_already':   return REG_FAIL_PENDING_ALREADY;
    default:                  return REG_FAIL_INVALID_CHARS;
  }
}

const PROTO_HEADER_SZ = 6;
const MAGIC_0 = 0x46; // 'F'
const MAGIC_1 = 0x4D; // 'M'

// ── Constantes de modo de sala ────────────────────────────────
const ROOM_MODE_RELAY     = 0;  // Relay puro (legacy, Ball Demo actual)
const ROOM_MODE_AGGREGATE = 1;  // Servidor agrega estado (WORLD_STATE)
const DEFAULT_TICK_MS     = 100; // 10Hz por defecto

// ── Game Handlers (lógica server-side por GAME_ID) ────────────
let gameHandlers;
try {
  gameHandlers = require('./game-handlers');
} catch(e) {
  gameHandlers = new Map(); // Sin handlers: relay puro para todos
}

// ── Estado global ──────────────────────────────────────────────
// rooms: Map<roomId, { gameId, maxPlayers, mode, tickMs, tickInterval, tickSeq, players }>
// players: Map<pid, { socket, state: Buffer|null, lastUpdate: number }>
const rooms   = new Map();

// ── Utilidades de protocolo ────────────────────────────────────

function buildPacket(cmd, roomId = 0, pid = 0, payload = Buffer.alloc(0)) {
  const pkt = Buffer.allocUnsafe(PROTO_HEADER_SZ + payload.length);
  pkt[0] = MAGIC_0;
  pkt[1] = MAGIC_1;
  pkt[2] = cmd;
  pkt[3] = roomId;
  pkt[4] = pid;
  pkt[5] = payload.length;
  payload.copy(pkt, PROTO_HEADER_SZ);
  return pkt;
}

function broadcastToRoom(room, excludePid, packet) {
  for (const [pid, pdata] of room.players) {
    const sock = pdata.socket || pdata; // Soportar ambos formatos
    if (pid !== excludePid && !sock.destroyed) {
      sock.write(packet);
    }
  }
}

function sendToPlayer(room, pid, packet) {
  const pdata = room.players.get(pid);
  if (pdata && pdata.socket && !pdata.socket.destroyed)
    pdata.socket.write(packet);
}

function serverBroadcast(room, cmd, payload) {
  const packet = buildPacket(cmd, room.id || 0, 0, payload);
  broadcastToRoom(room, null, packet);
}

// API object passed to game handlers
const handlerApi = { sendToPlayer, serverBroadcast, buildPacket, broadcastToRoom };

function createRoom(gameId, maxPlayers, mode) {
  // Reusar el primer ID libre (1..255)
  let id = null;
  for (let i = 1; i <= 255; i++) {
    if (!rooms.has(i)) { id = i; break; }
  }
  if (id === null) return null; // 255 salas llenas
  const handler = gameHandlers.get ? (gameHandlers.get(gameId) || null) : null;
  const room = {
    id,
    gameId,
    maxPlayers,
    mode:         mode || ROOM_MODE_RELAY,
    tickMs:       DEFAULT_TICK_MS,
    tickInterval: null,
    tickSeq:      0,
    gameStarted:  false,
    players:      new Map(),
    handler,
    gameState:    {},
  };
  rooms.set(id, room);
  if (handler && handler.onRoomCreated)
    handler.onRoomCreated(room, handlerApi);
  return id;
}

function getNextPid(room) {
  for (let pid = 1; pid <= room.maxPlayers; pid++) {
    if (!room.players.has(pid)) return pid;
  }
  return null; // Sala llena
}

// ── WORLD_STATE: agregación de estado ─────────────────────────

function broadcastWorldState(roomId) {
  const room = rooms.get(roomId);
  if (!room) return;

  room.tickSeq = (room.tickSeq + 1) & 0xFF;
  const now = Date.now();

  // Construir entradas de jugadores
  const entries = [];
  for (const [pid, pdata] of room.players) {
    const isStale = (now - pdata.lastUpdate > 2000) ? 0x02 : 0x00;
    const pflags = 0x01 | isStale; // bit0=CONNECTED, bit1=STALE

    const entry = Buffer.alloc(10);
    entry[0] = pid;
    entry[1] = pflags;
    if (pdata.state) {
      pdata.state.copy(entry, 2, 0, 8);
    }
    entries.push(entry);
  }

  // Payload: [FLAGS, N_PLAYERS, TICK_SEQ, ...entries]
  const payloadSize = 3 + entries.length * 10;
  const payload = Buffer.alloc(payloadSize);
  payload[0] = 0x00;             // FLAGS (sin turnos por ahora)
  payload[1] = entries.length;   // N_PLAYERS
  payload[2] = room.tickSeq;     // TICK_SEQ

  let off = 3;
  for (const entry of entries) {
    entry.copy(payload, off);
    off += 10;
  }

  // Enviar a cada jugador (header PID = el PID del receptor)
  for (const [pid, pdata] of room.players) {
    if (!pdata.socket.destroyed) {
      pdata.socket.write(buildPacket(CMD.WORLD_STATE, roomId, pid, payload));
    }
  }
}

function startRoomTick(roomId) {
  const room = rooms.get(roomId);
  if (!room || room.mode !== ROOM_MODE_AGGREGATE) return;
  if (room.tickInterval) return; // Ya corriendo
  room.tickInterval = setInterval(() => {
    broadcastWorldState(roomId);
  }, room.tickMs);
  console.log(`Sala ${roomId}: tick iniciado (${room.tickMs}ms)`);
}

function stopRoomTick(roomId) {
  const room = rooms.get(roomId);
  if (!room || !room.tickInterval) return;
  clearInterval(room.tickInterval);
  room.tickInterval = null;
  console.log(`Sala ${roomId}: tick detenido`);
}

// ── Parser de paquetes (buffer acumulativo) ────────────────────
// Crítico: ObsoNET puede entregar un paquete en múltiples chunks TCP

function parsePackets(state) {
  const packets = [];
  while (state.buffer.length >= PROTO_HEADER_SZ) {
    // Validar magic bytes
    if (state.buffer[0] !== MAGIC_0 || state.buffer[1] !== MAGIC_1) {
      const next = state.buffer.indexOf(MAGIC_0, 1);
      state.buffer = next === -1 ? Buffer.alloc(0) : state.buffer.slice(next);
      continue;
    }
    const len      = state.buffer[5];
    const totalLen = PROTO_HEADER_SZ + len;
    if (state.buffer.length < totalLen) break; // Paquete incompleto
    packets.push({
      cmd:     state.buffer[2],
      roomId:  state.buffer[3],
      pid:     state.buffer[4],
      payload: state.buffer.slice(PROTO_HEADER_SZ, totalLen),
    });
    state.buffer = state.buffer.slice(totalLen);
  }
  return packets;
}

// ── Gestión de sala ────────────────────────────────────────────

function leaveRoom(socket, state) {
  if (!state.roomId) return;
  const room = rooms.get(state.roomId);
  if (!room) return;

  if (room.handler && room.handler.onPlayerLeft)
    room.handler.onPlayerLeft(room, state.pid);
  room.players.delete(state.pid);
  console.log(`[${socket.remoteAddress}] Abandona sala ${state.roomId} (P${state.pid})`);

  if (room.players.size === 0) {
    stopRoomTick(state.roomId);
    console.log(`Sala ${state.roomId} vacia (persiste)`);
  } else {
    broadcastToRoom(room, null,
      buildPacket(CMD.PLAYER_LEFT, state.roomId, 0, Buffer.from([state.pid]))
    );
  }
  state.roomId = null;
  state.pid    = null;
}

// ── Lógica de comandos ─────────────────────────────────────────

// Comandos permitidos antes de autenticar (AUTH legacy o LOGIN/REGISTER nuevos)
const PRE_AUTH_CMDS = new Set([
  CMD.AUTH, CMD.PING, CMD.LOGIN, CMD.REGISTER, CMD.SESSION_RESUME,
]);

function handlePacket(socket, state, { cmd, payload }) {
  if (!state.auth && !PRE_AUTH_CMDS.has(cmd)) {
    socket.write(buildPacket(CMD.AUTH_FAIL));
    socket.destroy();
    return;
  }

  switch (cmd) {

    case CMD.PING:
      socket.write(buildPacket(CMD.PONG));
      break;

    case CMD.AUTH:
      if (payload.slice(0, 4).equals(AUTH_TOKEN)) {
        state.auth = true;
        console.log(`[${socket.remoteAddress}] Auth OK (legacy token)`);
        socket.write(buildPacket(CMD.AUTH_OK));
      } else {
        console.warn(`[${socket.remoteAddress}] Auth FALLIDA`);
        socket.write(buildPacket(CMD.AUTH_FAIL));
        socket.destroy();
      }
      break;

    case CMD.REGISTER: {
      // Payload: [ULEN][user...][NLEN][nick...]
      if (payload.length < 2) { socket.write(buildPacket(CMD.REG_FAIL, 0, 0, Buffer.from([REG_FAIL_INVALID_CHARS]))); break; }
      const uLen = payload[0];
      if (payload.length < 1 + uLen + 1) { socket.write(buildPacket(CMD.REG_FAIL, 0, 0, Buffer.from([REG_FAIL_INVALID_CHARS]))); break; }
      const username = payload.slice(1, 1 + uLen).toString('utf8');
      const nLen = payload[1 + uLen];
      if (payload.length < 1 + uLen + 1 + nLen) { socket.write(buildPacket(CMD.REG_FAIL, 0, 0, Buffer.from([REG_FAIL_INVALID_CHARS]))); break; }
      const nick = payload.slice(1 + uLen + 1, 1 + uLen + 1 + nLen).toString('utf8');
      const r = authStore.createPending(username, nick);
      if (!r.ok) {
        console.log(`[${socket.remoteAddress}] REGISTER fail user=${username} reason=${r.reason}`);
        socket.write(buildPacket(CMD.REG_FAIL, 0, 0, Buffer.from([mapRegFailReason(r.reason)])));
        break;
      }
      console.log(`[${socket.remoteAddress}] REGISTER pending user=${username} token=${r.token}`);
      // Respuesta: [TLEN][token...]
      const tokBuf = Buffer.from(r.token, 'utf8');
      const pl = Buffer.concat([Buffer.from([tokBuf.length]), tokBuf]);
      socket.write(buildPacket(CMD.REG_PENDING, 0, 0, pl));
      break;
    }

    case CMD.LOGIN: {
      // Payload: [ULEN][user...][PLEN][pass...]
      if (payload.length < 2) { socket.write(buildPacket(CMD.LOGIN_FAIL, 0, 0, Buffer.from([LOGIN_FAIL_BAD_CREDS]))); break; }
      const uLen = payload[0];
      if (payload.length < 1 + uLen + 1) { socket.write(buildPacket(CMD.LOGIN_FAIL, 0, 0, Buffer.from([LOGIN_FAIL_BAD_CREDS]))); break; }
      const username = payload.slice(1, 1 + uLen).toString('utf8');
      const pLen = payload[1 + uLen];
      if (payload.length < 1 + uLen + 1 + pLen) { socket.write(buildPacket(CMD.LOGIN_FAIL, 0, 0, Buffer.from([LOGIN_FAIL_BAD_CREDS]))); break; }
      const password = payload.slice(1 + uLen + 1, 1 + uLen + 1 + pLen).toString('utf8');
      const r = authStore.verifyLogin(username, password);
      if (!r.ok) {
        console.log(`[${socket.remoteAddress}] LOGIN fail user=${username} reason=${r.reason}`);
        socket.write(buildPacket(CMD.LOGIN_FAIL, 0, 0, Buffer.from([mapLoginFailReason(r.reason)])));
        break;
      }
      // Crear sesion
      purgeExpiredSessions();
      const sessionId = generateSessionId();
      const sessionHex = sessionId.toString('hex');
      sessions.set(sessionHex, {
        username: r.user.username,
        role: r.user.role,
        expiresAt: Date.now() + SESSION_TTL_MS,
      });
      state.auth      = true;
      state.loggedIn  = true;
      state.username  = r.user.username;
      state.nick      = r.user.nick;
      state.role      = r.user.role;
      state.sessionId = sessionHex;
      chatSockets.add(socket);
      console.log(`[${socket.remoteAddress}] LOGIN ok user=${username} role=${r.user.role} session=${sessionHex}`);
      // Respuesta: [role][NLEN][nick...][session_id 4B]
      const nickBuf = Buffer.from(r.user.nick, 'utf8');
      const pl = Buffer.concat([
        Buffer.from([ROLE_BYTE[r.user.role] || ROLE_BYTE.user, nickBuf.length]),
        nickBuf,
        sessionId,
      ]);
      socket.write(buildPacket(CMD.LOGIN_OK, 0, 0, pl));
      break;
    }

    case CMD.LOGOUT: {
      if (state.sessionId) {
        sessions.delete(state.sessionId);
        console.log(`[${socket.remoteAddress}] LOGOUT user=${state.username || '?'}`);
      }
      chatSockets.delete(socket);
      state.loggedIn  = false;
      state.username  = null;
      state.role      = null;
      state.sessionId = null;
      // Sin respuesta — el cliente cierra
      break;
    }

    case CMD.SESSION_RESUME: {
      // Payload: [session_id 4B]
      if (payload.length < 4) {
        socket.write(buildPacket(CMD.LOGIN_FAIL, 0, 0, Buffer.from([LOGIN_FAIL_BAD_CREDS])));
        break;
      }
      purgeExpiredSessions();
      const sid = payload.slice(0, 4).toString('hex');
      const sess = sessions.get(sid);
      if (!sess) {
        console.log(`[${socket.remoteAddress}] SESSION_RESUME fail session=${sid} (not found)`);
        socket.write(buildPacket(CMD.LOGIN_FAIL, 0, 0, Buffer.from([LOGIN_FAIL_BAD_CREDS])));
        break;
      }
      // Sesion valida — restaurar estado
      state.auth      = true;
      state.loggedIn  = true;
      state.username  = sess.username;
      state.role      = sess.role;
      state.sessionId = sid;
      {
        const uu = authStore.getUser(sess.username);
        state.nick = uu ? uu.nick : sess.username;
      }
      chatSockets.add(socket);
      // Renovar TTL
      sess.expiresAt = Date.now() + SESSION_TTL_MS;
      console.log(`[${socket.remoteAddress}] SESSION_RESUME ok user=${sess.username} role=${sess.role}`);
      // Respuesta misma forma que LOGIN_OK pero sin nick (ya lo tenia)
      const u = authStore.getUser(sess.username);
      const nickBuf = Buffer.from(u ? u.nick : sess.username, 'utf8');
      const pl = Buffer.concat([
        Buffer.from([ROLE_BYTE[sess.role] || ROLE_BYTE.user, nickBuf.length]),
        nickBuf,
        Buffer.from(sid, 'hex'),
      ]);
      socket.write(buildPacket(CMD.LOGIN_OK, 0, 0, pl));
      break;
    }

    case CMD.ROOM_CREATE: {
      const gameId = payload[0] ?? 0x01;
      if (!gameId) {
        socket.write(buildPacket(CMD.ERROR, 0, 0, Buffer.from([0x03])));
        break;
      }
      const maxPlayers = Math.min(payload[1] ?? 4, MAX_PLAYERS_LIMIT);
      // Byte 2: protocol version (0x01=relay, 0x02=aggregated)
      const protoVer = payload[2] ?? 0x01;
      const mode = (protoVer >= 0x02) ? ROOM_MODE_AGGREGATE : ROOM_MODE_RELAY;
      // Auto-leave si ya estaba en una sala
      if (state.roomId) leaveRoom(socket, state);
      const roomId = createRoom(gameId, maxPlayers, mode);
      if (roomId === null) {
        socket.write(buildPacket(CMD.ERROR, 0, 0, Buffer.from([0x02])));
        break;
      }
      const room   = rooms.get(roomId);
      const pid    = 1; // El creador siempre es P1 (host)
      room.players.set(pid, { socket, connState: state, state: null, lastUpdate: Date.now() });
      state.roomId = roomId;
      state.pid    = pid;
      console.log(`[${socket.remoteAddress}] Crea sala ${roomId} (game=0x${gameId.toString(16)}, max=${maxPlayers}, mode=${mode ? 'AGGREGATE' : 'RELAY'})`);
      socket.write(buildPacket(
        CMD.ROOM_INFO, roomId, pid,
        Buffer.from([roomId, gameId, 1, pid])
      ));
      if (room.handler && room.handler.onPlayerJoined)
        room.handler.onPlayerJoined(room, pid);
      break;
    }

    case CMD.ROOM_JOIN: {
      const roomId = payload[0];
      const room   = rooms.get(roomId);
      if (!room) { socket.write(buildPacket(CMD.ROOM_NOT_FOUND)); break; }
      // Partida ya en curso en modo RELAY: no aceptar nuevos jugadores
      // (en AGGREGATE/MMO se puede entrar siempre).
      if (room.gameStarted && room.mode !== ROOM_MODE_AGGREGATE) {
        socket.write(buildPacket(CMD.ROOM_FULL));
        break;
      }
      const pid = getNextPid(room);
      if (!pid) { socket.write(buildPacket(CMD.ROOM_FULL)); break; }
      // Auto-leave si ya estaba en una sala
      if (state.roomId) leaveRoom(socket, state);
      room.players.set(pid, { socket, connState: state, state: null, lastUpdate: Date.now() });
      state.roomId = roomId;
      state.pid    = pid;
      console.log(`[${socket.remoteAddress}] Entra sala ${roomId} como P${pid}`);
      broadcastToRoom(room, pid,
        buildPacket(CMD.PLAYER_JOINED, roomId, 0, Buffer.from([pid]))
      );
      socket.write(buildPacket(
        CMD.ROOM_INFO, roomId, pid,
        Buffer.from([roomId, room.gameId, room.players.size, pid])
      ));
      // AGGREGATE: si la partida ya arranco, notificar al recien entrado
      // para que MSXon pueda lanzar el .COM (no aplica a RELAY porque ahi
      // rechazamos el JOIN si gameStarted=true).
      if (room.gameStarted && room.mode === ROOM_MODE_AGGREGATE) {
        socket.write(buildPacket(CMD.GAME_START, roomId, 0));
      }
      if (room.handler && room.handler.onPlayerJoined)
        room.handler.onPlayerJoined(room, pid);
      break;
    }

    case CMD.ROOM_LIST: {
      // Respuesta: [N_ROOMS, ROOM_ID, GAME_ID, N_PLAYERS, ROOM_ID, ...]
      // Max 84 salas en un payload de 255 bytes (1 + 84*3 = 253)
      const entries = [];
      let count = 0;
      for (const [id, room] of rooms) {
        if (count >= 84) break;
        entries.push(id, room.gameId, room.players.size);
        count++;
      }
      const pl = Buffer.alloc(1 + entries.length);
      pl[0] = count;
      for (let i = 0; i < entries.length; i++) pl[1 + i] = entries[i];
      socket.write(buildPacket(CMD.ROOM_LIST, 0, 0, pl));
      break;
    }

    case CMD.GAME_LIST: {
      // Respuesta: [N][gameId, flags, maxPlayers, proto, comLen, com..., nameLen, name...] x N
      // flags bit0: 1=private (admin only), 0=public
      // Cliente filtra y rellena su tabla de juegos. Disabled NO se envían.
      const role = state.role || 'user';
      const list = gamesStore.listVisibleFor(role);
      const chunks = [Buffer.from([list.length])];
      for (const g of list) {
        const comBuf  = Buffer.from(g.com,  'utf8');
        const nameBuf = Buffer.from(g.name, 'utf8');
        const flags = g.visibility === 'private' ? 0x01 : 0x00;
        chunks.push(Buffer.from([g.id, flags, g.max, g.proto, comBuf.length]));
        chunks.push(comBuf);
        chunks.push(Buffer.from([nameBuf.length]));
        chunks.push(nameBuf);
      }
      const pl = Buffer.concat(chunks);
      if (pl.length > 255) {
        console.warn(`[gamelist] payload ${pl.length} > 255, truncating not implemented`);
      }
      socket.write(buildPacket(CMD.GAME_LIST, 0, 0, pl));
      break;
    }

    case CMD.ROOM_LEAVE:
      leaveRoom(socket, state);
      break;

    case CMD.STATE_UPDATE: {
      const room = rooms.get(state.roomId);
      if (!room) break;

      // Game handler hook: puede consumir el paquete
      if (room.handler && room.handler.onStateUpdate) {
        if (room.handler.onStateUpdate(room, state.pid, payload)) break;
      }

      if (room.mode === ROOM_MODE_AGGREGATE) {
        // Almacenar estado del jugador — el tick lo enviará agregado
        const pdata = room.players.get(state.pid);
        if (pdata) {
          pdata.state = Buffer.from(payload.slice(0, 8));
          pdata.lastUpdate = Date.now();
        }
      } else {
        // Relay puro
        const relay = buildPacket(CMD.STATE_UPDATE, state.roomId, state.pid, payload);
        broadcastToRoom(room, state.pid, relay);
      }
      break;
    }

    case CMD.GAME_START: {
      const room = rooms.get(state.roomId);
      if (!room || state.pid !== 1) break; // Solo P1 (host) puede arrancar
      room.gameStarted = true;
      broadcastToRoom(room, state.pid,
        buildPacket(CMD.GAME_START, state.roomId, 0)
      );
      // Iniciar tick en modo agregado
      if (room.mode === ROOM_MODE_AGGREGATE) {
        startRoomTick(state.roomId);
      }
      if (room.handler && room.handler.onGameStart)
        room.handler.onGameStart(room);
      break;
    }

    case CMD.GAME_END: {
      const room = rooms.get(state.roomId);
      if (!room || !room.gameStarted) break; // idempotente
      room.gameStarted = false;
      stopRoomTick(state.roomId);
      // Difundir a TODA la sala (incluido el emisor) para que ghosts reseteen
      // y la sala vuelva a estar lista para nueva partida.
      const pkt = buildPacket(CMD.GAME_END, state.roomId, 0);
      for (const [, info] of room.players) info.socket.write(pkt);
      console.log(`Sala ${state.roomId} GAME_END (lista para nueva partida)`);
      break;
    }

    case CMD.CHAT_SEND: {
      // Solo aceptado de usuarios humanos loggeados (no AUTH legacy).
      if (!state.loggedIn || !state.nick) break;
      if (payload.length < 1) break;
      const msgLen = payload[0];
      if (msgLen === 0 || msgLen > CHAT_MAX_MSG_LEN || payload.length < 1 + msgLen) break;
      const msg = payload.slice(1, 1 + msgLen).toString('utf8');
      broadcastChat(state.nick, msg);
      break;
    }

    case CMD.CHAT_HISTORY: {
      if (!state.loggedIn) break;
      // Enviar los ultimos mensajes (uno por paquete; el cliente acumula).
      for (const m of chatBuffer) {
        socket.write(buildChatRecvPacket(m.nick, m.msg));
      }
      break;
    }

    case CMD.PLAYER_LIST: {
      // Responde con la lista de jugadores en la sala del cliente:
      // [N] [PID, NLEN, nick...] x N
      // Ghosts no tienen nick (usaron AUTH legacy) → ponemos "BOT" como nick.
      if (!state.roomId) {
        socket.write(buildPacket(CMD.PLAYER_LIST, 0, 0, Buffer.from([0])));
        break;
      }
      const room = rooms.get(state.roomId);
      if (!room) {
        socket.write(buildPacket(CMD.PLAYER_LIST, 0, 0, Buffer.from([0])));
        break;
      }
      const entries = [];
      for (const [pid, info] of room.players) {
        const nick = (info.connState && info.connState.nick) ? info.connState.nick : 'BOT';
        const nickBuf = Buffer.from(String(nick).slice(0, 16), 'utf8');
        entries.push({ pid, nick: nickBuf });
      }
      let totalLen = 1; // [N]
      for (const e of entries) totalLen += 2 + e.nick.length;
      const pl = Buffer.alloc(totalLen);
      let off = 0;
      pl[off++] = entries.length;
      for (const e of entries) {
        pl[off++] = e.pid;
        pl[off++] = e.nick.length;
        e.nick.copy(pl, off); off += e.nick.length;
      }
      socket.write(buildPacket(CMD.PLAYER_LIST, state.roomId, 0, pl));
      break;
    }

    default:
      socket.write(buildPacket(CMD.ERROR, 0, 0, Buffer.from([0x01])));
  }
}

// ── Servidor TCP ───────────────────────────────────────────────

const server = net.createServer((socket) => {
  const state = {
    auth:   false,
    roomId: null,
    pid:    null,
    buffer: Buffer.alloc(0),
  };

  console.log(`[${socket.remoteAddress}] Conectado`);
  socket.setTimeout(TIMEOUT_MS);

  socket.on('data', (chunk) => {
    state.buffer = Buffer.concat([state.buffer, chunk]);
    for (const pkt of parsePackets(state)) {
      handlePacket(socket, state, pkt);
    }
  });

  socket.on('timeout', () => {
    console.log(`[${socket.remoteAddress}] Timeout`);
    leaveRoom(socket, state);
    socket.destroy();
  });

  socket.on('close', () => {
    leaveRoom(socket, state);
    chatSockets.delete(socket);
    console.log(`[${socket.remoteAddress}] Desconectado`);
  });

  socket.on('error', (err) => {
    // ECONNRESET muy común con ObsoNET — no tratar como error crítico
    if (err.code !== 'ECONNRESET') {
      console.error(`[${socket.remoteAddress}] Error: ${err.message}`);
    }
  });
});

server.on('error', (err) => console.error('Error servidor:', err));

server.listen(PORT, '0.0.0.0', () => {
  console.log(`MSX Game Server escuchando en :${PORT}`);
  console.log(`Máx ${MAX_PLAYERS_LIMIT} jugadores/sala · 255 salas simultáneas`);
  console.log(`Timeout por conexión: ${TIMEOUT_MS / 1000}s`);
  console.log(`Comandos: rooms, connections, help`);
});

// ── HTTP web server (registro vía QR) ─────────────────────────
mountWebServer({ authStore, gamesStore, port: WEB_PORT, host: WEB_HOST });

// ── Consola interactiva ────────────────────────────────────────
const readline = require('readline');
const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
rl.on('line', (line) => {
  const cmd = line.trim().toLowerCase();
  if (cmd === 'rooms' || cmd === 'r') {
    if (rooms.size === 0) {
      console.log('No hay salas abiertas');
    } else {
      console.log(`${rooms.size} sala(s):`);
      for (const [id, room] of rooms) {
        const pids = [...room.players.keys()].join(', ');
        const modeStr = room.mode === ROOM_MODE_AGGREGATE ? 'AGG' : 'RLY';
        console.log(`  Sala ${id} (0x${id.toString(16).padStart(2,'0')}) | juego=${room.gameId} | ${modeStr} | jugadores=${room.players.size}/${room.maxPlayers} [${pids}]`);
      }
    }
  } else if (cmd === 'connections' || cmd === 'c') {
    server.getConnections((err, count) => {
      console.log(`${err ? 'N/A' : count} conexiones activas`);
    });
  } else if (cmd === 'clear') {
    // Borrar salas vacias
    let cleared = 0;
    for (const [id, room] of rooms) {
      if (room.players.size === 0) {
        rooms.delete(id);
        cleared++;
      }
    }
    console.log(`${cleared} sala(s) vacia(s) eliminada(s). Quedan ${rooms.size} sala(s).`);
  } else if (cmd === 'clearall') {
    // Borrar TODAS las salas (desconecta jugadores)
    for (const [id, room] of rooms) {
      stopRoomTick(id);
      for (const [pid, pdata] of room.players) {
        const sock = pdata.socket || pdata;
        if (!sock.destroyed) sock.destroy();
      }
    }
    rooms.clear();
    console.log('Todas las salas eliminadas.');
  } else if (cmd === 'help' || cmd === 'h') {
    console.log('Comandos: rooms (r), connections (c), clear, clearall, help (h)');
  } else if (cmd) {
    console.log(`Comando desconocido: ${cmd}`);
  }
});
