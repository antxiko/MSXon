//=============================================================================
// game_runtime.h — Runtime mínimo para juegos lanzados desde MSXon
//
// Cada juego online es lanzado por MSXON.COM tras unirse a una sala.
// MSXON.COM escribe LOBBY.DAT con (conn, pid, roomId, gameId, ...) y deja
// la conexión TCP abierta. Cuando el .COM del juego arranca, esta API:
//
//   1. Lee LOBBY.DAT y popula `g_Game` (handle conn, pid, sala, ...).
//   2. Permite enviar paquetes con cabecera ya rellena.
//   3. Permite procesar paquetes con un callback simple por juego.
//
// Si no hay LOBBY.DAT, `GameRT_Init()` devuelve FALSE — el juego debe
// mostrar un mensaje "Arranca MSXon primero" y salir.
//
// Esto elimina del .c de cada juego: Diag_ShowNetInfo, Net_ConnectToServer,
// Net_RequestRoomList/CreateRoom/JoinRoom, gestión de STATE_LOBBY_*, etc.
//=============================================================================
#ifndef GAME_RUNTIME_H
#define GAME_RUNTIME_H

#include "msxgl.h"
#include "network.h"

// Buffer máximo de payload aceptado (header LEN es u8 → 255 max)
#define GAMERT_PAYLOAD_MAX  255

typedef struct {
    NetConn conn;        // Handle TCP heredado del lobby
    u8 pid;              // PID asignado por el server (1..N)
    u8 roomId;
    u8 active;           // Bitmask de jugadores conectados al entrar
    u8 gameId;
    u8 protoVer;
} GameContext;

extern GameContext g_Game;

// Callback que el juego registra para procesar paquetes entrantes.
// senderPid es el byte 4 del header (PID del emisor; 0 si viene del server).
typedef void (*GameRT_PacketCb)(u8 cmd, u8 senderPid, u8* payload, u8 len);

// Lee LOBBY.DAT, popula g_Game, hace Net_Init.
// Devuelve TRUE si el juego fue lanzado desde MSXon. FALSE si no.
bool GameRT_Init(void);

// Procesa hasta `maxPackets` paquetes pendientes del socket. Llama al
// callback con cada uno. Sale antes si el buffer queda sin un paquete
// completo (lo retoma en la siguiente llamada).
void GameRT_Poll(GameRT_PacketCb cb, u8 maxPackets);

// Envía un paquete con header (magic + cmd + roomId + pid + len + payload).
void GameRT_Send(u8 cmd, const u8* payload, u8 len);

// Atajo: envía un PING (sin payload).
void GameRT_SendPing(void);

// Útil para reflejar en pantalla rivales en la sala.
// Devuelve el número de bits a 1 en `g_Game.active`.
u8 GameRT_ActiveCount(void);

// Devuelve el nick del jugador pid (1-based). Si no hay nick (LOBBY.DAT v1
// o slot vacio), devuelve un fallback "P1".."P9","Pa".."Pf" en buffer estatico.
const c8* GameRT_GetNick(u8 pid);

// Sale del juego y vuelve al lobby MSXon: cierra la conexión TCP,
// escribe "MSXON\r" al keyboard buffer del MSX (0xFBF0..) y termina con
// Bios_Exit(0). El shell de MSX-DOS lee el buffer y relanza MSXON.COM.
void GameRT_ExitToLobby(void);

#endif // GAME_RUNTIME_H
