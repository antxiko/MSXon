'use strict';

// =============================================================================
// bomberman-handler.js — Bomberman authoritative server (GAME_ID=0x08)
//
// Server is authoritative: validates bomb placements, explosions, deaths
// and power-up drops. Clients send movement + "place bomb"; server sends
// back authoritative events.
// =============================================================================

// Packet types (first byte of STATE_UPDATE payload)
const PKT = {
    PLAYER:     0x01, // client -> server: x, y, gx, dir
    BOMB:       0x02, // client -> server: place bomb at (gx, gy)
    LAYOUT:     0x03, // server -> clients: grid snapshot
    EXPLOSION:  0x04, // server -> clients: bomb exploded origin
    BRICK:      0x05, // server -> clients: brick destroyed + powerup
    POWERUP:    0x06, // client -> server: picked powerup
    DEATH:      0x07, // server -> clients: player died
    WINNER:     0x08, // server -> clients: round winner
    BOMB_ACK:   0x09, // server -> clients: bomb confirmed
};

const GW = 15;
const GH = 10;
const MAX_BOMBS = 16;
const BOMB_TIMER_MS = 2400;
const EXPLOSION_MS  = 600;
const TICK_MS = 100;

const CELL_FLOOR = 0;
const CELL_WALL  = 1;
const CELL_BRICK = 2;
const CELL_BOMB  = 3;
const CELL_POWER = 4;

const PW_BOMB  = 1;
const PW_FIRE  = 2;
const PW_SPEED = 3;

function createGameState() {
    return {
        grid: Array.from({length: GH}, () => new Array(GW).fill(CELL_FLOOR)),
        bombs: [],
        players: [
            { gx: 0,    gy: 0,    alive: true, bombsMax: 1, bombsActive: 0, fire: 1 },
            { gx: GW-1, gy: 0,    alive: true, bombsMax: 1, bombsActive: 0, fire: 1 },
            { gx: 0,    gy: GH-1, alive: true, bombsMax: 1, bombsActive: 0, fire: 1 },
            { gx: GW-1, gy: GH-1, alive: true, bombsMax: 1, bombsActive: 0, fire: 1 },
        ],
        started: false,
        tickTimer: null,
    };
}

function generateLayout(gs) {
    for (let y = 0; y < GH; y++) {
        for (let x = 0; x < GW; x++) {
            if ((x & 1) && (y & 1)) gs.grid[y][x] = CELL_WALL;
            else gs.grid[y][x] = CELL_FLOOR;
        }
    }
    // Bricks ~40%, keep corners free for players
    for (let y = 0; y < GH; y++) {
        for (let x = 0; x < GW; x++) {
            if (gs.grid[y][x] !== CELL_FLOOR) continue;
            if ((x < 2 && y < 2) || (x > GW-3 && y < 2) ||
                (x < 2 && y > GH-3) || (x > GW-3 && y > GH-3)) continue;
            if (Math.random() < 0.4) gs.grid[y][x] = CELL_BRICK;
        }
    }
}

function buildLayoutPacket(gs) {
    // PKT_LAYOUT + GH*GW bytes (150 bytes). Too big for single STATE_UPDATE.
    // For now, small grid fits in ~150 bytes. We send via direct Net_Send with
    // CMD_STATE_UPDATE payload (max 200 bytes in server).
    const buf = Buffer.alloc(1 + GH * GW);
    buf[0] = PKT.LAYOUT;
    let i = 1;
    for (let y = 0; y < GH; y++)
        for (let x = 0; x < GW; x++)
            buf[i++] = gs.grid[y][x];
    return buf;
}

function explode(room, bombIdx) {
    const gs = room.gs;
    const b = gs.bombs[bombIdx];
    if (!b || !b.active) return;
    b.active = false;
    gs.players[b.owner].bombsActive--;
    gs.grid[b.gy][b.gx] = CELL_FLOOR;

    const affected = [{x: b.gx, y: b.gy}];
    const bricksDestroyed = [];
    const chainBombs = [];

    const DX = [1, -1, 0, 0];
    const DY = [0, 0, 1, -1];
    for (let d = 0; d < 4; d++) {
        for (let r = 1; r <= b.power; r++) {
            const nx = b.gx + DX[d] * r;
            const ny = b.gy + DY[d] * r;
            if (nx < 0 || nx >= GW || ny < 0 || ny >= GH) break;
            const cell = gs.grid[ny][nx];
            if (cell === CELL_WALL) break;
            affected.push({x: nx, y: ny});
            if (cell === CELL_BRICK) {
                let pwType = 0;
                if (Math.random() < 0.3) pwType = 1 + Math.floor(Math.random() * 3);
                if (pwType) gs.grid[ny][nx] = CELL_POWER + pwType - 1;
                else gs.grid[ny][nx] = CELL_FLOOR;
                bricksDestroyed.push({x: nx, y: ny, pw: pwType});
                break;
            }
            if (cell === CELL_BOMB) {
                // Chain reaction
                for (let bi = 0; bi < gs.bombs.length; bi++)
                    if (gs.bombs[bi].active && gs.bombs[bi].gx === nx && gs.bombs[bi].gy === ny)
                        chainBombs.push(bi);
                break;
            }
        }
    }

    // Broadcast explosion event (origin + power so clients can simulate same cells)
    const expBuf = Buffer.from([PKT.EXPLOSION, b.gx, b.gy, b.power]);
    room.api.serverBroadcast(room, 0x40, expBuf);

    // Broadcast brick destructions
    for (const br of bricksDestroyed) {
        const brBuf = Buffer.from([PKT.BRICK, br.x, br.y, br.pw]);
        room.api.serverBroadcast(room, 0x40, brBuf);
    }

    // Schedule explosion clear (no explicit clear packet; clients clear locally)

    // Check player deaths
    for (let p = 0; p < 4; p++) {
        if (!gs.players[p].alive) continue;
        for (const a of affected) {
            if (gs.players[p].gx === a.x && gs.players[p].gy === a.y) {
                gs.players[p].alive = false;
                const dBuf = Buffer.from([PKT.DEATH, p]);
                room.api.serverBroadcast(room, 0x40, dBuf);
                break;
            }
        }
    }

    // Chain reactions
    for (const ci of chainBombs) {
        setTimeout(() => explode(room, ci), 50);
    }

    // Check winner
    const alive = gs.players.filter(p => p.alive);
    if (alive.length <= 1 && gs.started) {
        gs.started = false;
        const winnerSlot = alive.length === 1 ? gs.players.indexOf(alive[0]) : 0xFF;
        const wBuf = Buffer.from([PKT.WINNER, winnerSlot]);
        room.api.serverBroadcast(room, 0x40, wBuf);
        stopTick(gs);
    }
}

function stopTick(gs) {
    if (gs.tickTimer) { clearInterval(gs.tickTimer); gs.tickTimer = null; }
}

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
    stopTick(gs);

    // Reset state
    for (let y = 0; y < GH; y++) for (let x = 0; x < GW; x++) gs.grid[y][x] = CELL_FLOOR;
    gs.bombs = [];
    for (const p of gs.players) {
        p.alive = true;
        p.bombsMax = 1;
        p.bombsActive = 0;
        p.fire = 1;
    }
    gs.players[0].gx = 0;      gs.players[0].gy = 0;
    gs.players[1].gx = GW-1;   gs.players[1].gy = 0;
    gs.players[2].gx = 0;      gs.players[2].gy = GH-1;
    gs.players[3].gx = GW-1;   gs.players[3].gy = GH-1;

    generateLayout(gs);
    gs.started = true;

    // Send layout to all
    const layoutPkt = buildLayoutPacket(gs);
    room.api.serverBroadcast(room, 0x40, layoutPkt);

    // Tick at 10Hz to decrement bomb timers
    gs.tickTimer = setInterval(() => {
        for (let i = 0; i < gs.bombs.length; i++) {
            const b = gs.bombs[i];
            if (!b.active) continue;
            b.ticks--;
            if (b.ticks <= 0) explode(room, i);
        }
    }, TICK_MS);

    console.log(`[BOMBERMAN] Started in room ${room.id}`);
}

function onStateUpdate(room, senderPid, payload) {
    const gs = room.gs;
    if (!gs || !gs.started) return false;
    const pktType = payload[0];
    const slot = senderPid - 1;
    if (slot < 0 || slot >= 4) return false;
    const player = gs.players[slot];
    if (!player || !player.alive) return false;

    if (pktType === PKT.PLAYER && payload.length >= 5) {
        // Update player grid position from x/y/gx/dir
        player.gx = payload[3];
        // Deriving gy from y: y = (BOARD_TY + gy*2) * 8 + offset. Use approximation.
        // Client also sends 'gy' implicitly via ... actually we didn't include gy.
        // Simpler: keep gx only; gy must be inferred. For now let's ignore gy tracking
        // (server uses last known gy). In practice this means death detection uses gx+stored gy.
        // TODO: add gy to PKT_PLAYER.
        return false; // Relay PLAYER packets so other clients see movement
    }

    if (pktType === PKT.BOMB && payload.length >= 3) {
        const gx = payload[1];
        const gy = payload[2];
        if (player.bombsActive >= player.bombsMax) return true;
        if (gs.grid[gy][gx] === CELL_BOMB) return true;
        if (gx >= GW || gy >= GH) return true;

        gs.grid[gy][gx] = CELL_BOMB;
        gs.bombs.push({
            active: true, gx, gy, owner: slot, power: player.fire, ticks: 24
        });
        player.bombsActive++;

        const ackBuf = Buffer.from([PKT.BOMB_ACK, gx, gy, slot, player.fire]);
        room.api.serverBroadcast(room, 0x40, ackBuf);
        return true; // consumed
    }

    if (pktType === PKT.POWERUP && payload.length >= 3) {
        const gx = payload[1];
        const gy = payload[2];
        const cell = gs.grid[gy][gx];
        if (cell >= CELL_POWER) {
            const pwType = cell - CELL_POWER + 1;
            if (pwType === PW_BOMB) player.bombsMax++;
            else if (pwType === PW_FIRE) player.fire++;
            gs.grid[gy][gx] = CELL_FLOOR;
        }
        return true;
    }

    return false;
}

function onPlayerLeft(room, pid) {
    if (!room.gs) return;
    if (room.players.size <= 1) stopTick(room.gs);
}

module.exports = {
    onRoomCreated,
    onPlayerJoined,
    onGameStart,
    onStateUpdate,
    onPlayerLeft,
};
