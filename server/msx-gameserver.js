'use strict';

// =============================================================================
// msx-gameserver.js — MSX Online Game Server v2.0
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
const AUTH_TOKEN         = Buffer.from(
  (process.env.MSX_AUTH_TOKEN || 'DEADBEEF'), 'hex'
);
const MAX_PLAYERS_LIMIT  = 16;       // Techo absoluto por sala
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
// rooms: Map<roomId, { gameId: u8, maxPlayers: u8, players: Map<pid, socket> }>
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
  for (const [pid, sock] of room.players) {
    if (pid !== excludePid && !sock.destroyed) {
      sock.write(packet);
    }
  }
}

function createRoom(gameId, maxPlayers) {
  // Reusar el primer ID libre (1..255)
  let id = null;
  for (let i = 1; i <= 255; i++) {
    if (!rooms.has(i)) { id = i; break; }
  }
  if (id === null) return null; // 255 salas llenas
  rooms.set(id, { gameId, maxPlayers, players: new Map() });
  return id;
}

function getNextPid(room) {
  for (let pid = 1; pid <= room.maxPlayers; pid++) {
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
      if (!gameId) {
        socket.write(buildPacket(CMD.ERROR, 0, 0, Buffer.from([0x03])));
        break;
      }
      const maxPlayers = Math.min(payload[1] ?? 4, MAX_PLAYERS_LIMIT);
      // Auto-leave si ya estaba en una sala
      if (state.roomId) leaveRoom(socket, state);
      const roomId = createRoom(gameId, maxPlayers);
      if (roomId === null) {
        socket.write(buildPacket(CMD.ERROR, 0, 0, Buffer.from([0x02])));
        break;
      }
      const room   = rooms.get(roomId);
      const pid    = 1; // El creador siempre es P1 (host)
      room.players.set(pid, socket);
      state.roomId = roomId;
      state.pid    = pid;
      console.log(`[${socket.remoteAddress}] Crea sala ${roomId} (game=0x${gameId.toString(16)}, max=${maxPlayers})`);
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
      // Auto-leave si ya estaba en una sala
      if (state.roomId) leaveRoom(socket, state);
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
  console.log(`Máx ${MAX_PLAYERS_LIMIT} jugadores/sala · 255 salas simultáneas`);
  console.log(`Timeout por conexión: ${TIMEOUT_MS / 1000}s`);
  console.log(`Comandos: rooms, connections, help`);
});

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
        console.log(`  Sala ${id} (0x${id.toString(16).padStart(2,'0')}) | juego=${room.gameId} | jugadores=${room.players.size}/${room.maxPlayers} [${pids}]`);
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
      for (const [pid, sock] of room.players) {
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
