// lobby_client.h — MSXon Lobby Client
// Read LOBBY.DAT written by MSXon to skip lobby and go straight to game.
//
// Formato:
//   v1 (magic 0xAA, 8 bytes): solo header
//   v2 (magic 0xAB, 8 + 16*17 = 280 bytes): header + tabla de nicks por PID
//     - 16 slots de 17 bytes cada uno: [NLEN][16 bytes nick]
//     - NLEN=0 = slot sin jugador (o nick desconocido)
#ifndef LOBBY_CLIENT_H
#define LOBBY_CLIENT_H

#include "msxgl.h"
#include "dos.h"
#include "network.h"

#define LOBBY_MAGIC      0xAA   // v1 (8 bytes)
#define LOBBY_MAGIC_V2   0xAB   // v2 (8 + nicks)
#define LOBBY_DAT_FILE   "LOBBY.DAT"
#define LOBBY_MAX_NICKS  16
#define LOBBY_NICK_LEN   16

typedef struct {
    u8 magic;       // 0xAA o 0xAB
    u8 conn;        // TCP connection handle
    u8 pid;         // Player PID (1-based)
    u8 roomId;      // Room ID
    u8 active;      // Bitmask de jugadores conectados
    u8 gameId;      // Game ID for validation
    u8 protoVer;    // 0x01=RELAY, 0x02=AGGREGATE
    u8 reserved;
} LobbyData;

extern LobbyData g_LobbyData;
extern bool g_FromLobby;
// Nicks indexados por PID-1. nicks[pid-1][0] = 0 si no hay nick conocido.
// Solo se rellena cuando magic == LOBBY_MAGIC_V2.
extern c8   g_LobbyNicks[LOBBY_MAX_NICKS][LOBBY_NICK_LEN + 1];

// Load LOBBY.DAT. Returns TRUE if launched from MSXon (v1 o v2).
bool LobbyClient_Load(void);

#endif // LOBBY_CLIENT_H
