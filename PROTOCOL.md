# MSX Online — Protocol Specification v2.0

Guide for developers who want to write their own client for the MSX Online network.

---

## Server

- **IP**: 217.154.107.144
- **Port**: 9876
- **Transport**: TCP raw (no TLS)
- **Auth token**: `0xDE 0xAD 0xBE 0xEF` (4 bytes, configurable via `MSX_AUTH_TOKEN` env var on server)

---

## Packet Format

Every packet (client→server and server→client) uses this 6-byte header + payload:

```
Offset  Size  Field       Description
0       1     MAGIC_0     Always 0x46 ('F')
1       1     MAGIC_1     Always 0x4D ('M')
2       1     CMD         Command byte (see table below)
3       1     ROOM        Room ID (0x00 if not in a room)
4       1     PID         Player ID (0x00 if not assigned)
5       1     LEN         Payload length (0..255)
6..     LEN   PAYLOAD     Command-specific data
```

Total packet size: 6 + LEN bytes (max 261).

---

## Commands

### Authentication

| CMD  | Name      | Direction | Payload | Description |
|------|-----------|-----------|---------|-------------|
| 0x10 | AUTH      | C→S       | 4 bytes token | Send auth token |
| 0x11 | AUTH_OK   | S→C       | — | Auth accepted |
| 0x12 | AUTH_FAIL | S→C       | — | Auth rejected, connection closed |

### Rooms

| CMD  | Name           | Direction | Payload | Description |
|------|----------------|-----------|---------|-------------|
| 0x20 | ROOM_CREATE    | C→S       | `[GAME_ID, MAX_PLAYERS]` | Create room. `MAX_PLAYERS` optional (default 4, max 16) |
| 0x21 | ROOM_JOIN      | C→S       | `[ROOM_ID]` | Join existing room |
| 0x22 | ROOM_LEAVE     | C→S       | — | Leave current room |
| 0x23 | ROOM_INFO      | S→C       | `[ROOM_ID, GAME_ID, N_PLAYERS, MY_PID]` | Room joined successfully |
| 0x24 | ROOM_FULL      | S→C       | — | Room is full |
| 0x25 | ROOM_NOT_FOUND | S→C       | — | Room doesn't exist |
| 0x26 | ROOM_LIST      | C→S       | — | Request room list |
| 0x26 | ROOM_LIST      | S→C       | `[N, ROOM_ID, GAME_ID, N_PLAYERS, ...]` | Room list response (max 84 rooms) |

### Game Events

| CMD  | Name           | Direction | Payload | Description |
|------|----------------|-----------|---------|-------------|
| 0x30 | PLAYER_JOINED  | S→C       | `[PID]` | New player joined your room |
| 0x31 | PLAYER_LEFT    | S→C       | `[PID]` | Player left your room |
| 0x32 | GAME_START     | C→S / S→C | — | Host (PID=1) starts game, broadcast to others |

### Game Data

| CMD  | Name           | Direction | Payload | Description |
|------|----------------|-----------|---------|-------------|
| 0x40 | STATE_UPDATE   | C→S       | up to 255 bytes | Game state. Server relays to all other players in room |
| 0x40 | STATE_UPDATE   | S→C       | up to 255 bytes | Relayed state from another player. PID header field = sender |

### Keepalive

| CMD  | Name | Direction | Payload | Description |
|------|------|-----------|---------|-------------|
| 0x01 | PING | C→S       | — | Keepalive |
| 0x02 | PONG | S→C       | — | Keepalive response |

### Error

| CMD  | Name  | Direction | Payload | Description |
|------|-------|-----------|---------|-------------|
| 0xFF | ERROR | S→C       | `[CODE]` | 0x01=unknown cmd, 0x02=no free rooms, 0x03=invalid gameId |

---

## Connection Flow

```
Client                              Server
  |                                    |
  |--- TCP connect ------------------->|
  |--- AUTH [DE AD BE EF] ------------>|
  |<-- AUTH_OK ------------------------|
  |                                    |
  |--- ROOM_LIST --------------------->|
  |<-- ROOM_LIST [N, rooms...] --------|
  |                                    |
  |--- ROOM_JOIN [room_id] ----------->|  (or ROOM_CREATE [game_id, max])
  |<-- ROOM_INFO [room, game, n, pid] -|
  |                                    |
  |--- STATE_UPDATE [game data] ------>|  (every frame with changes)
  |<-- STATE_UPDATE [from other pid] --|  (relayed by server)
  |                                    |
  |--- PING --------------------------->|  (every ~5 seconds)
  |<-- PONG ----------------------------|
  |                                    |
  |--- ROOM_LEAVE --------------------->|
  |<-- (server broadcasts PLAYER_LEFT) -|
```

---

## Game IDs

The server is **game-agnostic**. It only validates that `GAME_ID != 0x00`. The payload of `STATE_UPDATE` is opaque — the server just relays it.

| ID   | Game | STATE_UPDATE payload |
|------|------|---------------------|
| 0x01 | Ball Demo | `[X_HI, X_LO, Y_HI, Y_LO, FRAME, FLAGS, 0, 0]` (8 bytes) |
| 0x02 | Damas | TBD |

To add a new game: pick an unused GAME_ID, define your own STATE_UPDATE payload, implement client logic. Server needs zero changes.

---

## Important Notes

- **Timeout**: Server disconnects after 90 seconds of inactivity. Send PING every ~5s.
- **TCP chunks**: MSX hardware (ObsoNET, ESP-01) may split one packet across multiple TCP segments. Always use an accumulative buffer parser.
- **No TLS**: Connection is plaintext. Auth token travels in the clear. Z80 limitation.
- **Max 255 rooms**, max 16 players per room, max 255 bytes payload per packet.
- **ROOM_LIST response**: first byte is count, then triplets of `[ROOM_ID, GAME_ID, N_PLAYERS]`.
- **Auto-leave**: Creating or joining a room automatically leaves any previous room.

---

## MSXgl-Specific Notes

If building an MSX client with MSXgl + SDCC:

### SDCC Calling Convention

The UNAPI TCP assembly (`unapi_tcp.asm`) uses `sdcccall(0)` (stack-based args). SDCC 4.2+ defaults to `sdcccall(1)` (register-based). **Every UNAPI extern must have `__sdcccall(0)`**:

```c
extern int tcpip_tcp_open(...) __sdcccall(0);
extern int tcpip_get_ipinfo(...) __sdcccall(0);
extern int tcpip_tcp_send(...) __sdcccall(0);
// ALL of them
```

**Never** use `--sdcccall 0` as a global flag — it breaks MSXgl internals.

### ForceRamAddr

Adding code shifts DATA segment addresses, which can break UNAPI calls:

```javascript
// project_config.js
ForceRamAddr = 0x8000;  // Pin DATA at page 2
// Move to 0xC000 when code exceeds 32KB
```

### Global Variables

UNAPI structs MUST be global, never stack-local:

```c
static tcpip_unapi_tcp_conn_parms g_TcpParms;  // GOOD
static int g_ConnResult = 0;                     // GOOD

void func() {
    tcpip_unapi_tcp_conn_parms parms;  // BAD — stack corruption
}
```

### Auto-Flush

InterNestor and ESP-01 drivers buffer data. Always flush after send:

```c
tcpip_tcp_send(conn, data, len, 1);  // PUSH flag
tcpip_tcp_flush(conn);               // ESSENTIAL
```

### Non-Blocking TCP

`tcpip_tcp_open` returns immediately in SYN_SENT state. Poll `tcpip_tcp_state` until ESTABLISHED before sending data.

### Required LibModules

```javascript
LibModules = [ "system", "bios", "vdp", "print", "input", "memory", "dos" ];
AddSources = [ "../../engine/src/network/unapi_tcp.asm" ];
```

### Static, Not Inline

Never use `inline` for functions calling UNAPI/DOS. Use `static`.

---

## Example: Minimal Node.js Client

```javascript
const net = require('net');

const MAGIC = Buffer.from([0x46, 0x4D]);
const AUTH_TOKEN = Buffer.from([0xDE, 0xAD, 0xBE, 0xEF]);

function buildPacket(cmd, room, pid, payload) {
    payload = payload || Buffer.alloc(0);
    const pkt = Buffer.alloc(6 + payload.length);
    pkt[0] = 0x46; pkt[1] = 0x4D;
    pkt[2] = cmd; pkt[3] = room; pkt[4] = pid;
    pkt[5] = payload.length;
    payload.copy(pkt, 6);
    return pkt;
}

const sock = net.connect(9876, '217.154.107.144', () => {
    // 1. Authenticate
    sock.write(buildPacket(0x10, 0, 0, AUTH_TOKEN));
});

let buffer = Buffer.alloc(0);
sock.on('data', (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    while (buffer.length >= 6) {
        if (buffer[0] !== 0x46 || buffer[1] !== 0x4D) {
            buffer = buffer.subarray(1);
            continue;
        }
        const len = buffer[5];
        if (buffer.length < 6 + len) break;
        const cmd = buffer[2];
        const payload = buffer.subarray(6, 6 + len);
        buffer = buffer.subarray(6 + len);

        if (cmd === 0x11) {
            console.log('Auth OK!');
            // 2. Request room list
            sock.write(buildPacket(0x26, 0, 0));
        } else if (cmd === 0x26) {
            console.log(`${payload[0]} rooms available`);
            // 3. Create a room (game=0x01, max=4)
            sock.write(buildPacket(0x20, 0, 0, Buffer.from([0x01, 4])));
        } else if (cmd === 0x23) {
            console.log(`Joined room ${payload[0]} as P${payload[3]}`);
        }
    }
});
```

---

_MSX Online Game Server v2.0 — FX-Media Audio Video, S.L._
