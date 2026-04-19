# Lobby Universal — MSXon

Modulo compartido de lobby/red para todos los juegos de MSXon.

## Que incluye

- Diagnostico TCP/IP (Screen 0)
- Conexion al servidor + autenticacion
- Lista de salas filtrada por GAME_ID
- Crear/unir salas
- Sala de espera con jugadores conectados
- Host (P1) arranca la partida
- Net_Poll con callback para paquetes del juego
- Ping periodico

## Uso

```c
#include "lobby.h"

static const u8 SRV_IP[4] = {217,154,107,144};
static const LobbyConfig cfg = {
    "MI JUEGO", 0x07, 4, SRV_IP, 9876
};

void Game_OnPacket(u8 cmd, u8 senderPid, u8* payload, u8 len) {
    // procesar paquetes del juego
}

void main(void) {
    Lobby_Init(&cfg, Game_OnPacket);
    Lobby_SetTileOffsets(T_FA, T_F0);
    Lobby_Diag();
    Lobby_Connect();
    // VDP_SetMode, load tiles...
    if(g_LobbyOnline) { Lobby_RequestRooms(); g_LobbyState=LOBBY_ST_LIST_WAIT; }
    while(1) {
        Halt(); NB_Flush(); Keyboard_Update();
        if(g_LobbyState < LOBBY_ST_PLAYING) Lobby_Update();
        else { Lobby_Poll(); /* game logic */ }
    }
    Lobby_SendRoomLeave();
}
```

## Build

Anadir "lobby" a ProjModules en project_config.js:
```javascript
ProjModules = [ ProjName, "lobby" ];
```

Copiar lobby.h y lobby.c al directorio del proyecto.

## Juegos que lo usan

- Texas Hold'em (TEX_030)
- Tetris 4P (TET_024)

---

*Proyecto de Antxiko*
