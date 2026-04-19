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
  WORLD_STATE:    0x41,
  ERROR:          0xFF,
};

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
      room.players.set(pid, { socket, state: null, lastUpdate: Date.now() });
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
      const pid = getNextPid(room);
      if (!pid) { socket.write(buildPacket(CMD.ROOM_FULL)); break; }
      // Auto-leave si ya estaba en una sala
      if (state.roomId) leaveRoom(socket, state);
      room.players.set(pid, { socket, state: null, lastUpdate: Date.now() });
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
