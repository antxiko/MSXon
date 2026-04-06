# MSX ONLINE — Contexto del proyecto para Claude
# Ultima actualizacion: 2026-04-06
# Estado: 7 juegos — Ball Demo (MOL_039), Damas (DAM_022), Burdyn (BURD_029), Parchis (PAR_011), Texas (TEX_020), Tetris (TET_021), Among (AMG_001)

---

## QUE ES ESTE PROYECTO

Plataforma de juegos online para MSX reales (Z80) con cinco componentes:

1. **Servidor Node.js** (`server/msx-gameserver.js`) — gestiona salas, relay, game handlers
2. **Ball Demo** (`client/`) — demo de sprites multijugador en Screen 5 (GAME_ID=0x01)
3. **Damas** (`damas/`) — damas online 2 jugadores en Screen 4 (GAME_ID=0x02)
4. **Burdyn** (`burdyn/`) — RPG crawler multijugador en Screen 4 (GAME_ID=0x03)
5. **Parchis** (`parchis/`) — parchis online 4 jugadores en Screen 4 (GAME_ID=0x04)
6. **Texas Hold'em** (`texas/`) — poker online hasta 6 jugadores, dealer server-side (GAME_ID=0x05)
7. **Tetris 4P** (`tetris/`) — tetris competitivo 4 jugadores con garbage (GAME_ID=0x06)
8. **Among MSX** (`among/`) — impostor online 4-8 jugadores (GAME_ID=0x07, en desarrollo)

Codigo compartido en `shared/`: network.h, protocol.h, log.h

---

## AUTOR

- **Antxiko** — desarrollador retro-computing
- Mantiene noSignal BBS (Mystic BBS, orientado a MSX/C64/Spectrum/Amstrad/Atari)

---

## ESTRUCTURA DE ARCHIVOS

```
MSXonLINE/                           <-- Repo GitHub (antxiko/MSXonLINE)
|
|-- server/
|   |-- msx-gameserver.js            <-- Servidor TCP (relay + game handlers)
|   |-- server-status.js             <-- Monitor interactivo + ghost player
|   |-- ghost-service.js             <-- Ghosts persistentes en VPS
|   |-- game-handlers/
|   |   |-- index.js                 <-- Registro de handlers por GAME_ID
|   |   +-- poker-handler.js         <-- Dealer de Texas Hold'em (server-side)
|   |-- update.sh                    <-- Script de actualizacion VPS
|   +-- msx-server.service           <-- Unidad systemd
|
|-- shared/                          <-- Codigo compartido entre juegos
|   |-- network.h                    <-- Capa UNAPI TCP
|   |-- protocol.h                   <-- Protocolo binario
|   +-- log.h                        <-- Logging a fichero MSX-DOS 2
|
|-- client/                          <-- Ball Demo (GAME_ID=0x01, Screen 5)
|   |-- msxonline.c                  <-- Fuente principal
|   +-- build: MOL_039
|
|-- damas/                           <-- Damas (GAME_ID=0x02, Screen 4)
|   |-- damas.c                      <-- Fuente principal
|   +-- build: DAM_022
|
|-- burdyn/                          <-- RPG Crawler (GAME_ID=0x03, Screen 4)
|   |-- burdyn.c                     <-- Fuente principal
|   |-- editor.html                  <-- Editor de mapas HTML
|   |-- assets/burdyn_map.bin        <-- Mapa 64x64
|   +-- build: BURD_024
|
|-- parchis/                         <-- Parchis (GAME_ID=0x04, Screen 4)
|   |-- parchis.c                    <-- Fuente principal (NO incluido, solo en MSXgl)
|   |-- path_editor.html             <-- Editor visual del tablero
|   |-- tileset.png                  <-- Tileset grafico
|   |-- screen_layout.json           <-- Layout 32x24 del tablero
|   |-- parchis_path.json            <-- Recorrido + casas + pasillos
|   +-- build: PAR_011
|
|-- PROTOCOL.md                      <-- Especificacion del protocolo
|-- COMMANDS.md                      <-- Referencia de comandos SSH/deploy
+-- README.md
```

Directorios de compilacion (fuera del repo):
- `MSXgl/projects/msxonline/` — Ball Demo
- `MSXgl/projects/damas/` — Damas
- `MSXgl/projects/burdyn/` — Burdyn
- `MSXgl/projects/parchis/` — Parchis
Los fuentes se sincronizan al repo antes de push.

---

## SERVIDOR (server/msx-gameserver.js)

### Dos modos de sala

- **RELAY** (legacy): recibe STATE_UPDATE, lo reenvía a los demas. Usado por Ball Demo.
- **AGGREGATE**: almacena estados, envia WORLD_STATE (0x41) a todos a 10Hz. Usado por Burdyn.

El modo se elige con PROTO_VERSION byte en ROOM_CREATE: 0x01=relay, 0x02=aggregate.

### Configuracion

```javascript
PORT = 9876
AUTH_TOKEN = DEADBEEF (configurable via MSX_AUTH_TOKEN env var)
MAX_PLAYERS_LIMIT = 16
TIMEOUT_MS = 90000
```

### VPS: 217.154.107.144

```bash
# Logs
ssh root@217.154.107.144 "journalctl -u msx-server -f"
# Reiniciar
ssh root@217.154.107.144 "systemctl restart msx-server"
# Desplegar
cd ~/Documents/MSXonLIVE/MSXonLINE && sed -i 's/\r$//' server/update.sh && scp server/msx-gameserver.js server/update.sh root@217.154.107.144:/tmp/ && ssh root@217.154.107.144 "bash /tmp/update.sh"
```

---

## COMPILAR

### Ball Demo
```bash
cd MSXgl/projects/msxonline && bash build.sh
```

### Damas
```bash
cd MSXgl/projects/damas && bash build.sh
```

### Burdyn
```bash
cd MSXgl/projects/burdyn && bash build.sh
```

### Parchis
```bash
cd MSXgl/projects/parchis && bash build.sh
```

Todos generan .COM en `out/` y DSK en `emul/dsk/`. Copiar a `build/bin/` y `build/dsk/`.

### Requisitos
- SDCC 4.5.0 (`C:\Program Files\SDCC` o `MSXgl/tools/sdcc/`)
- Node.js (para build script MSXgl)
- `ForceRamAddr = 0x8000` en project_config.js (CRITICO)

---

## HARDWARE

- MSX2 + MSX-DOS 2
- ESP-01 WiFi modem con UNAPI driver (ducasp) — NO InterNestor Lite
- Funciona tambien con GR8NET y ObsoNET
- Offline si no hay UNAPI

---

## PROBLEMAS RESUELTOS CRITICOS

### ForceRamAddr = 0x8000
Anadir codigo (incluso un string) desplaza DATA segment y rompe UNAPI. Fijar en 0x8000. Mover a 0xC000 si codigo > 32KB.

### __sdcccall(0) en UNAPI
SDCC 4.5 usa sdcccall(1). unapi_tcp.asm necesita sdcccall(0). Poner `__sdcccall(0)` en CADA extern de unapi_tcp.h. NUNCA usar --sdcccall 0 global.

### Auto-flush en Net_Send
InterNestor/ESP-01 buferizan. Flush obligatorio tras cada tcp_send.

### g_ConnResult global
El puntero `&conn` de tcpip_tcp_open debe ser global, no en stack.

### Dirty tile system (Burdyn)
Buffer de 768 bytes en RAM + dirty index array (max 128). Si desborda, full flush. El scroll escribe directo al buffer sin dirty tracking y marca full flush.

### Buffer teclado BIOS
Vaciar PUTPNT=GETPNT cada frame para evitar acumulacion. Teclas se capturan en flags inmediatamente tras Keyboard_Update.

---

## BALL DEMO (MOL_039)

- Screen 5 (256x212, 16 colores bitmap)
- Sprites Mode 2 (VDP_SetSpriteExUniColor)
- Lobby grafico con lista de salas
- Movimiento con cursores + joystick
- Multiplayer funcional en MSX real
- GAME_ID = 0x01, modo RELAY

---

## DAMAS (DAM_022)

- Screen 4 (Graphic 3, 32x24 tiles)
- Tablero 8x8, 2 jugadores
- Sprites Mode 2 para fichas
- Lobby grafico filtrado por GAME_ID_DAMAS (0x02)
- RELAY mode con endTurn flag (payload[4])
- Multi-captura: endTurn=0 mientras queden capturas
- GAME_ID = 0x02, modo RELAY

---

## BURDYN RPG (BURD_029)

- Screen 4 (Graphic 3, 32x24 tiles)
- Mapa 64x64 cargado desde BURDYN.MAP
- Viewport 24x24 con scroll centrado + HUD sidebar 8 columnas
- 83 tiles: 8 terreno + 32 items + 43 fuente
- Sprites Mode 2 para jugadores (colores por PID)
- Name table buffer 768 bytes con dirty tile tracking
- Lobby grafico filtrado por GAME_ID_CRAWLER
- Multiplayer AGGREGATE (WORLD_STATE 10Hz)
- Editor de mapas HTML (burdyn/editor.html)
- GAME_ID = 0x03, modo AGGREGATE

---

## PARCHIS (PAR_011)

- Screen 4 (Graphic 3, 32x24 tiles)
- Tablero parchis, 4 jugadores humanos (sin IA online)
- Sprites Mode 2 (8x8) para fichas (16 sprites max)
- Recorrido 68 casillas + 8 pasillo por color + 12 casillas seguras
- Dado animado con tiles 2x2 (6 caras)
- Tileset: A=tile 97, a=129, 0=80, negro=tile 0
- Lobby grafico filtrado por GAME_ID_PARCHIS (0x04)
- P1 (host) arranca partida con S (CMD_GAME_START)
- RELAY mode con endTurn flag
- Reglas: sacar 5 para salir, 3 seises pierde ficha, barreras, bonus captura/meta
- path_editor.html para editar tablero visualmente
- GAME_ID = 0x04, modo RELAY

---

## TEXAS HOLD'EM (TEX_030)

- Screen 4 (Graphic 3, 32x24 tiles)
- Poker Texas Hold'em hasta 6 jugadores humanos
- Cliente puro: sin dealer local, sin IA, sin evaluador de manos
- El servidor es el dealer (game-handlers/poker-handler.js)
- Cartas simplificadas 2x3 tiles (valor+palo reutilizables)
- Stack fijo 1000 fichas, ciegas 10/20
- 4 rondas de apuestas: pre-flop, flop, turn, river
- Cartas privadas enviadas individualmente por el servidor
- Ghost bot en servidor para jugar solo
- table_editor.html y tile_editor.html para diseño visual
- GAME_ID = 0x05, modo RELAY

---

## TETRIS 4P (TET_024)

- Screen 4 (Graphic 3, 32x24 tiles)
- Tetris competitivo a 4 jugadores, 8 columnas por tablero
- Shadow buffer: solo redibuja celdas que cambian (targeted draw)
- Full board sync empaquetado (86 bytes) cada 5 frames
- Garbage: 2 lineas=1, 3=2, tetris=4 filas, gap determinístico
- Targeting con SPACE (elige a quien atacar, sprite flecha)
- Ghost bots con IA en servidor (evalua posiciones)
- JIFFY counter para velocidad uniforme entre MSX
- GAME_ID = 0x06, modo RELAY

---

## AMONG MSX (AMG_001, en desarrollo)

- Screen 4 (Graphic 3, 32x24 tiles)
- Among Us simplificado, 4-8 jugadores, 1 impostor
- 7 habitaciones separadas (pantallas completas, sin scroll)
- Puertas para cambiar de habitacion
- Interruptores: impostor sabotea, inocentes arreglan
- Solo ves jugadores en tu habitacion
- GAME_ID = 0x07, modo RELAY

---

## REGLAS PARA CLAUDE

1. **NUNCA tocar conectividad cuando se piden cambios de UI** — rompe la conexion
2. **NUNCA hacer cambios que no se pidieron** — cada byte extra puede romper UNAPI
3. **NUNCA restaurar desde repo sin re-aplicar fixes de network.h** — g_ConnResult, flush
4. **"Cliente" = MSX siempre** — nunca PC client salvo que se diga explicitamente
5. **ForceRamAddr = 0x8000 siempre** en project_config.js
6. **Builds versionadas**: MOL_XXX, DAM_XXX, BURD_XXX, PAR_XXX, TEX_XXX, TET_XXX, AMG_XXX — max 5 copias
7. **SDCC: variables al inicio de funcion**, no declarar dentro de for/if

---

_Proyecto de Antxiko · MSX Online v2.0_
