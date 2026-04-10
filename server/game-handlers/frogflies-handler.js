'use strict';

// =============================================================================
// frogflies-handler.js — Frog & Flies fly spawner (server-side)
//
// The server manages all flies. Players only send their frog position
// and PKT_CATCH when they grab a fly. Server validates catches,
// updates scores, and broadcasts fly state.
// =============================================================================

const PKT_FROG  = 0x01;  // Player sends: frog state (relayed to others)
const PKT_FLIES = 0x02;  // Server sends: all fly positions
const PKT_CATCH = 0x03;  // Player sends: caught fly index
const PKT_SCORE = 0x04;  // Server sends: score update
const PKT_WINNER = 0x05; // Server sends: winner (slot 0-3)

const MAX_FLIES = 8;
const SPAWN_INTERVAL_MS = 1500;   // new fly every 1.5s
const FLY_TICK_MS = 100;          // update flies 10 times/sec
const WIN_SCORE = 20;             // first to 20 catches wins

// api is stored in room.api (per-room, like poker-handler)

// ── Per-room game state ─────────────────────────────────────────────

function createGameState() {
  return {
    flies: Array.from({length: MAX_FLIES}, () => ({
      x: 0, y: 0, vx: 0, vy: 0, active: false
    })),
    scores: [0, 0, 0, 0],
    frameCount: 0,
    spawnTimer: null,
    tickTimer: null,
    gameTimer: null,
    started: false,
  };
}

// ── Fly logic (runs on server) ──────────────────────────────────────

function spawnFly(gs) {
  for (let i = 0; i < MAX_FLIES; i++) {
    if (gs.flies[i].active) continue;
    const fromLeft = Math.random() > 0.5;
    gs.flies[i].active = true;
    gs.flies[i].x = fromLeft ? -8 : 256;
    gs.flies[i].y = 20 + Math.floor(Math.random() * 80);
    // vx scaled for 10Hz tick (client runs at 50Hz, so 5x faster)
    gs.flies[i].vx = fromLeft ? (5 + Math.floor(Math.random() * 5)) : -(5 + Math.floor(Math.random() * 5));
    gs.flies[i].vy = 0;
    break;
  }
}

function tickFlies(gs) {
  gs.frameCount++;
  for (let i = 0; i < MAX_FLIES; i++) {
    if (!gs.flies[i].active) continue;
    // Move (10Hz tick = ~5 pixels per tick at vx=1)
    gs.flies[i].x += gs.flies[i].vx;
    // Ondulant vertical movement
    gs.flies[i].vy = ((gs.frameCount + i * 7) & 0x0F) < 8 ? -1 : 1;
    gs.flies[i].y += gs.flies[i].vy;
    // Off screen
    if (gs.flies[i].x < -16 || gs.flies[i].x > 270) gs.flies[i].active = false;
  }
}

// ── Build fly packet ────────────────────────────────────────────────

function buildFlyPacket(gs) {
  // PKT_FLIES + 3 bytes per fly (x_low, y_low, flags) = 25 bytes
  const buf = Buffer.alloc(1 + MAX_FLIES * 3);
  buf[0] = PKT_FLIES;
  let idx = 1;
  for (let i = 0; i < MAX_FLIES; i++) {
    buf[idx++] = gs.flies[i].x & 0xFF;
    buf[idx++] = gs.flies[i].y & 0xFF;
    buf[idx++] = (gs.flies[i].active ? 1 : 0);
  }
  return buf;
}

function buildScorePacket(gs) {
  // PKT_SCORE + 4 scores
  const buf = Buffer.alloc(5);
  buf[0] = PKT_SCORE;
  for (let i = 0; i < 4; i++) buf[1 + i] = gs.scores[i];
  return buf;
}

// ── Handler hooks ───────────────────────────────────────────────────

function onRoomCreated(room, handlerApi) {
  room.api = handlerApi;
  room.gs = createGameState();
}

function onPlayerJoined(room, pid) {
  if (!room.gs) room.gs = createGameState();
}

function onGameStart(room) {
  const gs = room.gs;
  if (!gs) return;

  // Stop any previous timers (prevents double intervals on reconnect)
  stopTimers(gs);

  // Reset
  gs.scores = [0, 0, 0, 0];
  gs.frameCount = 0;
  gs.started = true;
  for (let i = 0; i < MAX_FLIES; i++) gs.flies[i].active = false;

  // Start fly spawner
  gs.spawnTimer = setInterval(() => {
    spawnFly(gs);
  }, SPAWN_INTERVAL_MS);

  // Start fly tick (10Hz — send positions to all)
  gs.tickTimer = setInterval(() => {
    tickFlies(gs);
    const pkt = buildFlyPacket(gs);
    room.api.serverBroadcast(room, 0x40, pkt); // CMD_STATE_UPDATE = 0x40
  }, FLY_TICK_MS);

  console.log(`[FROGFLIES] Game started in room ${room.id}`);
}

function onStateUpdate(room, senderPid, payload) {
  const gs = room.gs;
  if (!gs || !gs.started) return false;

  const pktType = payload[0];

  if (pktType === PKT_FROG) {
    // Frog position — relay to others (don't consume, let server relay it)
    return false;
  }

  if (pktType === PKT_CATCH && payload.length >= 2) {
    const flyIdx = payload[1];
    if (flyIdx < MAX_FLIES && gs.flies[flyIdx].active) {
      gs.flies[flyIdx].active = false;
      const slot = senderPid - 1;
      if (slot >= 0 && slot < 4) {
        gs.scores[slot]++;
        // Broadcast score update
        const scorePkt = buildScorePacket(gs);
        room.api.serverBroadcast(room, 0x40, scorePkt);
        // Check win condition
        if (gs.scores[slot] >= WIN_SCORE) {
          stopTimers(gs);
          const winPkt = Buffer.alloc(2);
          winPkt[0] = PKT_WINNER;
          winPkt[1] = slot;
          room.api.serverBroadcast(room, 0x40, winPkt);
        }
      }
    }
    return true; // consumed — don't relay PKT_CATCH
  }

  return false;
}

function onPlayerLeft(room, pid) {
  // If no players left, stop timers
  if (!room.gs) return;
  if (room.players.size <= 1) {
    stopTimers(room.gs);
  }
}

function stopTimers(gs) {
  if (gs.spawnTimer) { clearInterval(gs.spawnTimer); gs.spawnTimer = null; }
  if (gs.tickTimer)  { clearInterval(gs.tickTimer);  gs.tickTimer = null; }
  if (gs.gameTimer)  { clearInterval(gs.gameTimer);  gs.gameTimer = null; }
  gs.started = false;
}

module.exports = {
  onRoomCreated,
  onPlayerJoined,
  onGameStart,
  onStateUpdate,
  onPlayerLeft,
};
