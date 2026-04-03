# MSX ONLINE — Contexto del proyecto para Claude
# Ultima actualizacion: 2026-04-03
# Estado: Ball Demo (MOL_038) y Burdyn RPG (BURD_012) funcionando en MSX real con multiplayer

---

## QUE ES ESTE PROYECTO

Plataforma de juegos online para MSX reales (Z80) con tres componentes:

1. **Servidor Node.js** (`server/msx-gameserver.js`) — gestiona salas, relay + aggregation
2. **Ball Demo** (`client/`) — demo de sprites multijugador en Screen 5 (GAME_ID=0x01)
3. **Burdyn** (`burdyn/`) — RPG crawler multijugador en Screen 4 (GAME_ID=0x03)

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
|   |-- msx-gameserver.js            <-- Servidor TCP (relay + aggregate)
|   |-- server-status.js             <-- Monitor interactivo + ghost player
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
|   |-- network.h, protocol.h, log.h, msxgl_config.h
|   +-- build: MOL_038
|
|-- burdyn/                          <-- RPG Crawler (GAME_ID=0x03, Screen 4)
|   |-- burdyn.c                     <-- Fuente principal
|   |-- editor.html                  <-- Editor de mapas HTML
|   |-- assets/burdyn_map.bin        <-- Mapa 64x64
|   |-- log.h, msxgl_config.h, project_config.js
|   +-- build: BURD_012
|
|-- PROTOCOL.md                      <-- Especificacion del protocolo
|-- COMMANDS.md                      <-- Referencia de comandos SSH/deploy
+-- README.md
```

El directorio de compilacion real es `MSXgl/projects/msxonline/` y `MSXgl/projects/burdyn/` (fuera del repo). Los fuentes se sincronizan al repo antes de push.

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

### Burdyn
```bash
cd MSXgl/projects/burdyn && bash build.sh
```

Ambos generan .COM en `out/` y DSK en `emul/dsk/`. Copiar a `build/bin/` y `build/dsk/`.

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

## BALL DEMO (MOL_038)

- Screen 5 (256x212, 16 colores bitmap)
- Sprites Mode 2 (VDP_SetSpriteExUniColor)
- Lobby grafico con lista de salas
- Movimiento con cursores + joystick
- Multiplayer funcional en MSX real
- GAME_ID = 0x01, modo RELAY

---

## BURDYN RPG (BURD_012)

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

## REGLAS PARA CLAUDE

1. **NUNCA tocar conectividad cuando se piden cambios de UI** — rompe la conexion
2. **NUNCA hacer cambios que no se pidieron** — cada byte extra puede romper UNAPI
3. **NUNCA restaurar desde repo sin re-aplicar fixes de network.h** — g_ConnResult, flush
4. **"Cliente" = MSX siempre** — nunca PC client salvo que se diga explicitamente
5. **ForceRamAddr = 0x8000 siempre** en project_config.js
6. **Builds versionadas**: MOL_XXX (Ball Demo), BURD_XXX (Burdyn), max 5 copias
7. **SDCC: variables al inicio de funcion**, no declarar dentro de for/if

---

_Proyecto de Antxiko · MSX Online v2.0_
