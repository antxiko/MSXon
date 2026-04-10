// lobby_client.h — MSXonLINE Lobby Client
// Read LOBBY.DAT written by LOBBY.COM to skip lobby and go straight to game.
// Include this in each game instead of lobby.h when using standalone LOBBY.COM.
#ifndef LOBBY_CLIENT_H
#define LOBBY_CLIENT_H

#include "msxgl.h"
#include "dos.h"
#include "network.h"

#define LOBBY_MAGIC 0xAA
#define LOBBY_DAT_FILE "LOBBY.DAT"

typedef struct {
    u8 magic;       // 0xAA
    u8 conn;        // TCP connection handle
    u8 pid;         // Player PID (1-based)
    u8 roomId;      // Room ID
    u8 active;      // Bitmask of connected players
    u8 gameId;      // Game ID for validation
    u8 protoVer;    // 0x01=RELAY, 0x02=AGGREGATE
    u8 reserved;
} LobbyData;

extern LobbyData g_LobbyData;
extern bool g_FromLobby;

// Load LOBBY.DAT. Returns TRUE if launched from LOBBY.COM.
// Deletes the file after reading so next direct launch detects offline mode.
bool LobbyClient_Load(void);

#endif // LOBBY_CLIENT_H
