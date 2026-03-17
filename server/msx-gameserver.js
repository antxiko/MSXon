'use strict';

// =============================================================================
// msx-gameserver.js — MSX Online Game Server v1.0
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

const net  = require('net');

// ── Configuración ─────────────────────────────────────────────
const PORT               = 9876;
const AUTH_TOKEN         = Buffer.from([0xDE, 0xAD, 0xBE, 0xEF]); // Cambiar en producción
const MAX_PLAYERS_PER_ROOM = 4;
const TIMEOUT_MS         = 90_000;   // 90s — generoso para el Z80

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
  ERROR:          0xFF,
};

const PROTO_HEADER_SZ = 6;
const MAGIC_0 = 0x46; // 'F'
const MAGIC_1 = 0x4D; // 'M'

// ── Estado global ──────────────────────────────────────────────
// rooms: Map<roomId, { gameId: u8, players: Map<pid, socket> }>
const rooms   = new Map();
let nextRoomId = 1;

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
  for (const [pid, sock] of room.players) {
    if (pid !== excludePid && !sock.destroyed) {
      sock.write(packet);
    }
  }
}

function createRoom(gameId) {
  const id = nextRoomId++;
  if (nextRoomId > 255) nextRoomId = 1;
  rooms.set(id, { gameId, players: new Map() });
  return id;
}

function getNextPid(room) {
  for (let pid = 1; pid <= MAX_PLAYERS_PER_ROOM; pid++) {
    if (!room.players.has(pid)) return pid;
  }
  return null; // Sala llena
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

  room.players.delete(state.pid);
  console.log(`[${socket.remoteAddress}] Abandona sala ${state.roomId} (P${state.pid})`);

  if (room.players.size === 0) {
    rooms.delete(state.roomId);
    console.log(`Sala ${state.roomId} eliminada (vacía)`);
  } else {
    broadcastToRoom(room, null,
      buildPacket(CMD.PLAYER_LEFT, state.roomId, 0, Buffer.from([state.pid]))
    );
  }
  state.roomId = null;
  state.pid    = null;
}

// ── Lógica de comandos ─────────────────────────────────────────

function handlePacket(socket, state, { cmd, payload }) {
  // Sin auth solo se permite AUTH y PING
  if (!state.auth && cmd !== CMD.AUTH && cmd !== CMD.PING) {
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
        console.log(`[${socket.remoteAddress}] Auth OK`);
        socket.write(buildPacket(CMD.AUTH_OK));
      } else {
        console.warn(`[${socket.remoteAddress}] Auth FALLIDA`);
        socket.write(buildPacket(CMD.AUTH_FAIL));
        socket.destroy();
      }
      break;

    case CMD.ROOM_CREATE: {
      const gameId = payload[0] ?? 0x01;
      const roomId = createRoom(gameId);
      const room   = rooms.get(roomId);
      const pid    = 1; // El creador siempre es P1 (host)
      room.players.set(pid, socket);
      state.roomId = roomId;
      state.pid    = pid;
      console.log(`[${socket.remoteAddress}] Crea sala ${roomId} (game=0x${gameId.toString(16)})`);
      socket.write(buildPacket(
        CMD.ROOM_INFO, roomId, pid,
        Buffer.from([roomId, gameId, 1, pid])
      ));
      break;
    }

    case CMD.ROOM_JOIN: {
      const roomId = payload[0];
      const room   = rooms.get(roomId);
      if (!room) { socket.write(buildPacket(CMD.ROOM_NOT_FOUND)); break; }
      const pid = getNextPid(room);
      if (!pid) { socket.write(buildPacket(CMD.ROOM_FULL)); break; }
      room.players.set(pid, socket);
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

    case CMD.ROOM_LEAVE:
      leaveRoom(socket, state);
      break;

    case CMD.STATE_UPDATE: {
      // El corazón del servidor: relay del paquete completo a los demás
      const room = rooms.get(state.roomId);
      if (!room) break;
      const relay = buildPacket(CMD.STATE_UPDATE, state.roomId, state.pid, payload);
      broadcastToRoom(room, state.pid, relay);
      break;
    }

    case CMD.GAME_START: {
      const room = rooms.get(state.roomId);
      if (!room || state.pid !== 1) break; // Solo P1 (host) puede arrancar
      broadcastToRoom(room, state.pid,
        buildPacket(CMD.GAME_START, state.roomId, 0)
      );
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
  console.log(`Máx ${MAX_PLAYERS_PER_ROOM} jugadores/sala · 255 salas simultáneas`);
  console.log(`Timeout por conexión: ${TIMEOUT_MS / 1000}s`);
});
