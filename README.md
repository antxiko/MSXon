# MSXon

Plataforma de juegos online multijugador para ordenadores **MSX2 reales** sobre TCP/IP.

```
  MSX ──ESP-01 WiFi──┐                 ┌──ObsoNET── MSX
                     │    ┌────────┐   │
  MSX ──GR8NET──────┼────│ Server │───┤
                     │    │ Node.js│   │
  MSX ──ObsoNET─────┘    └────────┘   └──GR8NET── MSX
                          TCP :9876
```

---

## Juegos

| Juego | GAME_ID | Jugadores | Modo | Screen | Estado |
|-------|---------|-----------|------|--------|--------|
| **Ball Demo** | 0x01 | 4 | RELAY | Screen 5 | Funcional (MOL_039) |
| **Damas Online** | 0x02 | 2 | RELAY | Screen 4 | Funcional (DAM_022) |
| **Burdyn RPG** | 0x03 | 14 | AGGREGATE | Screen 4 | Funcional (BURD_029) |
| **Parchis** | 0x04 | 4 | RELAY | Screen 4 | Funcional (PAR_011) |
| **Texas Hold'em** | 0x05 | 6 | RELAY+handler | Screen 4 | Funcional (TEX_030) |
| **Tetris 4P** | 0x06 | 4 | RELAY | Screen 4 | Funcional (TET_024) |
| **Among MSX** | 0x07 | 4-8 | RELAY | Screen 4 | En desarrollo (AMG_001) |
| **Frog & Flies** | 0x69 | 4 | RELAY+handler | Screen 4 | Funcional (FRG_012) |

### Ball Demo (`client/`)
Demo de sprites multijugador. Cada jugador mueve una bola por la pantalla en Screen 5 (bitmap). Hasta 4 jugadores por sala.

### Damas Online (`damas/`)
Juego de damas para 2 jugadores. Tablero cenital con fichas como sprites 16x16. Incluye capturas, multi-capturas y coronacion. P1=blancas, P2=negras.

### Burdyn RPG Crawler (`burdyn/`)
RPG crawler multijugador para hasta 14 jugadores. Mapa 64x64 con scroll centrado incremental, 83 tiles, HUD sidebar, inventario. Modo AGGREGATE (world state a 10Hz).

### Parchis Online (`parchis/`)
Parchis clasico para 4 jugadores. Recorrido de 68 casillas, pasillos, casillas seguras, capturas, barreras. Host arranca la partida.

### Texas Hold'em Poker (`texas/`)
Poker Texas Hold'em para hasta 6 jugadores. El servidor es el dealer (poker-handler.js) — baraja, reparte cartas privadas, gestiona rondas de apuestas, evalua manos. El MSX solo muestra UI y envia acciones. Ghost bot para jugar solo.

### Tetris 4P (`tetris/`)
Tetris competitivo a 4 jugadores, 8 columnas por tablero. Full board sync empaquetado (86 bytes). Garbage con targeting. Ghost bots con IA.

### Frog & Flies (`frogflies/`)
Clon del Frog & Flies (1982). 4 ranas en nenufares cazan moscas. Charge-jump con 3 potencias. Moscas gestionadas 100% por el servidor (frogflies-handler.js): spawn cada 1.5s, movimiento a 10Hz, validacion de catches. Gana el primero en llegar a 20 moscas.

### Among MSX (`among/`)
Among Us simplificado para 4-8 jugadores. 7 habitaciones, 1 impostor. En desarrollo.

---

## LOBBY.COM — Lobby universal

Programa standalone (Screen 0) que hace de punto de entrada para todos los juegos:

1. Selector de juegos (1-8)
2. Diagnostico UNAPI (IP, gateway, servidor)
3. Conectar al servidor, autenticar
4. Lista de salas, crear/unir
5. Waiting room (jugadores conectados, host pulsa S)
6. Escribe `LOBBY.DAT` + `_LAUNCH.BAT`, sale
7. El juego arranca, lee `LOBBY.DAT`, salta directamente a PLAYING

La conexion TCP persiste entre programas porque UNAPI vive en el cartucho (ESP-01/GR8NET/ObsoNET), no en la RAM del .COM.

Todos los juegos son backward compatible: funcionan sin LOBBY.COM.

---

## Estructura del repo

```
MSXon/
├── server/              Servidor Node.js
│   ├── msx-gameserver.js    Servidor principal (relay + aggregate + handlers)
│   ├── server-status.js     Monitor interactivo
│   ├── ghost-service.js     Entry point modular ghosts (v2.0)
│   ├── ghost-base.js        Clase base (plumbing, backoff, cleanup)
│   ├── ghost-room-registry.js  Singleton coordinacion roomId
│   ├── ghost-damas.js       Ghost Damas
│   ├── ghost-burdyn.js      Ghost Burdyn (multi-instancia)
│   ├── ghost-tetris.js      Ghost Tetris (multi-instancia)
│   ├── ghost-poker.js       Ghost Poker
│   ├── ghost-parchis.js     Ghost Parchis (multi-instancia)
│   └── game-handlers/
│       ├── index.js             Registro de handlers
│       ├── poker-handler.js     Dealer Texas Hold'em
│       └── frogflies-handler.js Moscas Frog & Flies
├── shared/              Codigo compartido
│   ├── network.h            Capa UNAPI TCP
│   ├── protocol.h           Protocolo binario
│   ├── log.h                Logging MSX-DOS 2
│   ├── lobby_client.h       Lee LOBBY.DAT (lanzado desde LOBBY.COM)
│   └── lobby_client.c
├── lobby/               LOBBY.COM standalone (Screen 0)
│   └── lobby_main.c
├── client/              Ball Demo (0x01)
├── damas/               Damas (0x02)
├── burdyn/              Burdyn RPG (0x03)
├── parchis/             Parchis (0x04)
├── texas/               Texas Hold'em (0x05)
├── tetris/              Tetris 4P (0x06)
├── among/               Among MSX (0x07)
├── frogflies/           Frog & Flies (0x69)
├── build/               Builds + serve.js
└── CLAUDE.md            Contexto para Claude
```

---

## Servidor

**IP**: 217.154.107.144:9876

Tres modos de sala:
- **RELAY**: reenvía STATE_UPDATE a todos
- **AGGREGATE**: almacena estados, envía WORLD_STATE a 10Hz (Burdyn)
- **RELAY+handler**: relay + logica server-side por GAME_ID (Texas, Frog&Flies)

```bash
# Logs
ssh root@217.154.107.144 "journalctl -u msx-server -f"

# Reiniciar
ssh root@217.154.107.144 "systemctl restart msx-server"

# Desplegar
cd MSXon && sed -i 's/\r$//' server/update.sh && \
scp server/msx-gameserver.js server/game-handlers/*.js server/update.sh \
root@217.154.107.144:/tmp/ && ssh root@217.154.107.144 "bash /tmp/update.sh"
```

---

## Compilar

Requiere [MSXgl](https://github.com/aoineko-fr/MSXgl) y SDCC 4.5.0.

```bash
cd MSXgl/projects/lobby && bash build.sh       # LOBBY.COM
cd MSXgl/projects/msxonline && bash build.sh   # Ball Demo
cd MSXgl/projects/damas && bash build.sh       # Damas
cd MSXgl/projects/burdyn && bash build.sh      # Burdyn
cd MSXgl/projects/parchis && bash build.sh     # Parchis
cd MSXgl/projects/texas && bash build.sh       # Texas Hold'em
cd MSXgl/projects/tetris && bash build.sh      # Tetris 4P
cd MSXgl/projects/frogflies && bash build.sh   # Frog & Flies
```

**IMPORTANTE**: `ForceRamAddr = 0x8000` en `project_config.js` es obligatorio para evitar conflictos UNAPI.

---

## Hardware soportado

- **MSX2** con MSX-DOS 2
- **Red**: ESP-01 WiFi (UNAPI ducasp), ObsoNET, GR8NET
- Funciona offline si no hay UNAPI

---

## Protocolo

Ver [PROTOCOL.md](PROTOCOL.md) para la especificacion completa.

```
[0x46][0x4D][CMD][ROOM][PID][LEN][...payload...]
  'F'   'M'
```

---

*Proyecto de Antxiko*
