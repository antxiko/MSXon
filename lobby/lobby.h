// lobby.h — MSXonLINE Universal Lobby Module
// Shared lobby code for all MSXonLINE games
#pragma once

#include "msxgl.h"
#include "protocol.h"
#include "network.h"
#include "log.h"

// ── Lobby states ────────────────────────────────────────────────────
#define LOBBY_ST_LIST_WAIT  0
#define LOBBY_ST_LIST       1
#define LOBBY_ST_WAITING    2
#define LOBBY_ST_PLAYING    3

// ── Configuration (game fills this before Lobby_Init) ───────────────
typedef struct {
    const c8* gameName;     // "TETRIS ONLINE", "TEXAS HOLDEM", etc.
    u8  gameId;             // GAME_ID_XXX
    u8  maxPlayers;         // 2, 4, 6, 8
    const u8* serverIP;
    u16 serverPort;
} LobbyConfig;

// ── Shared state (game reads these) ─────────────────────────────────
extern NetConn g_LobbyConn;
extern u8 g_LobbyPid;          // my PID (1-based)
extern u8 g_LobbyRoomId;
extern u8 g_LobbyActive;       // bitmask of connected players
extern u8 g_LobbyState;
extern bool g_LobbyOnline;
extern u8 g_LobbySendBuf[20];

// ── Name buffer (game defines u8 g_NB[768]) ─────────────────────────
extern u8 g_NB[768];

// ── Game packet callback ────────────────────────────────────────────
typedef void (*LobbyPacketCB)(u8 cmd, u8 senderPid, u8* payload, u8 len);

// ── API ─────────────────────────────────────────────────────────────

// Init: store config
void Lobby_Init(const LobbyConfig* cfg, LobbyPacketCB gameCB);

// Diag: run in Screen 0 before VDP_SetMode
void Lobby_Diag(void);

// Connect: TCP + auth. Returns TRUE if online
bool Lobby_Connect(void);

// Room operations
void Lobby_RequestRooms(void);
void Lobby_CreateRoom(void);
void Lobby_JoinRoom(u8 roomId);
void Lobby_SendPing(void);
void Lobby_SendGameStart(void);
void Lobby_SendRoomLeave(void);

// Send raw STATE_UPDATE (game uses this for game-specific data)
void Lobby_SendStateUpdate(u8* payload, u8 len);

// Poll: process incoming packets. Handles lobby commands internally,
// passes CMD_STATE_UPDATE to game callback
void Lobby_Poll(void);

// Update: call each frame when g_LobbyState < LOBBY_ST_PLAYING
// Handles lobby draw, input, waiting room
// Returns current state (LOBBY_ST_PLAYING when game starts)
u8 Lobby_Update(void);

// Helpers
void Lobby_NB_Text(u8 x, u8 y, const c8* s);
void Lobby_NB_Num(u8 x, u8 y, u16 v);
void Lobby_NB_ClearRow(u8 y);
void Lobby_SetTileOffsets(u8 fontA, u8 font0);
