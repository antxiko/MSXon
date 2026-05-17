# MSXon — Contexto del proyecto para Claude
# Ultima actualizacion: 2026-05-17
# Version: 0.8
# Estado: 9 juegos + MSXON.COM — Ball Demo (MOL_039), Damas (DAM_022), Burdyn (BURD_029), Parchis (PAR_011), Texas (TEX_030), Tetris (TET_024), Among (AMG_001 disabled), Bomberman (BMB_001), Frog & Flies (FRG_012)
#
# v0.8 highlights:
# - Pipeline lobby ↔ juego ↔ lobby cerrado: GAME_END (0x33) libera la sala, RELAY rechaza JOIN si gameStarted, AGGREGATE acepta y reenvia GAME_START.
# - Todos los .COM son thin-clients sobre shared/game_runtime.{c,h}: GameRT_Init/Send/Poll/ExitToLobby.
# - GameRT_PacketCb extiende firma con senderPid (necesario para tetris/poker).
# - MSXon (lobby/msxon.c) maneja ROOM_FULL/NOT_FOUND con mensaje rojo + auto-refresh.
# - UX: cursor del menu y lista de salas con HMMV parcial (solo 12px), sin parpadeo de pantalla.
# - Cliente envia CMD_GAME_END automaticamente tras detectar winner (5s mensaje + exit) en tetris/damas/parchis/texas/bomberman. Ghosts resetean al recibir GAME_END.

---

## QUE ES ESTE PROYECTO

Plataforma de juegos online para MSX reales (Z80) con estos componentes:

1. **LOBBY.COM** (`lobby/`) — lobby universal standalone, Screen 0, selector de juegos, connect/auth/salas/waiting
2. **Servidor Node.js** (`server/msx-gameserver.js`) — gestiona salas, relay, game handlers
3. **Ball Demo** (`client/`) — demo de sprites multijugador en Screen 5 (GAME_ID=0x01)
4. **Damas** (`damas/`) — damas online 2 jugadores en Screen 4 (GAME_ID=0x02)
5. **Burdyn** (`burdyn/`) — RPG crawler multijugador en Screen 4 (GAME_ID=0x03)
6. **Parchis** (`parchis/`) — parchis online 4 jugadores en Screen 4 (GAME_ID=0x04)
7. **Texas Hold'em** (`texas/`) — poker online hasta 6 jugadores, dealer server-side (GAME_ID=0x05)
8. **Tetris 4P** (`tetris/`) — tetris competitivo 4 jugadores con garbage (GAME_ID=0x06)
9. **Among MSX** (`among/`) — impostor online 4-8 jugadores (GAME_ID=0x07, en desarrollo)
10. **Frog & Flies** (`frogflies/`) — 4 ranas cazan moscas, moscas server-side (GAME_ID=0x69)

Codigo compartido en `shared/`: network.h, protocol.h, log.h, lobby_client.h/c

---

## AUTOR

- **Antxiko** — desarrollador retro-computing
- Mantiene noSignal BBS (Mystic BBS, orientado a MSX/C64/Spectrum/Amstrad/Atari)

---

## ESTRUCTURA DE ARCHIVOS

```
MSXon/                           <-- Repo GitHub (antxiko/MSXon)
|
|-- server/
|   |-- msx-gameserver.js            <-- Servidor TCP + HTTP integrado (puerto 9876 TCP, 8080 HTTP)
|   |-- auth-store.js                <-- Almacen de usuarios (JSON atomico, scrypt, pending tokens)
|   |-- msx-web.js                   <-- Endpoint HTTP /r para activacion via QR (detras de Caddy)
|   |-- server-status.js             <-- Monitor interactivo + ghost player
|   |-- ghost-service.js             <-- Entry point modular ghosts (v2.0)
|   |-- ghost-base.js                <-- Clase base (plumbing comun, backoff, cleanup)
|   |-- ghost-room-registry.js       <-- Singleton coordinacion roomId compartido
|   |-- ghost-damas.js               <-- Ghost Damas
|   |-- ghost-burdyn.js              <-- Ghost Burdyn (multi-instancia)
|   |-- ghost-tetris.js              <-- Ghost Tetris (multi-instancia)
|   |-- ghost-poker.js               <-- Ghost Poker
|   |-- ghost-parchis.js             <-- Ghost Parchis (multi-instancia)
|   |-- game-handlers/
|   |   |-- index.js                 <-- Registro de handlers por GAME_ID
|   |   |-- poker-handler.js         <-- Dealer de Texas Hold'em (server-side)
|   |   +-- frogflies-handler.js     <-- Moscas server-side para Frog & Flies
|   |-- update.sh                    <-- Script de actualizacion VPS
|   |-- msx-server.service           <-- Unidad systemd
|   |-- users.json                   <-- (RUNTIME, no en git) cuentas activadas con scrypt hash
|   +-- .superadmin                  <-- (RUNTIME, no en git) username del superadmin inicial
|
|-- tools/
|   +-- test-register.js             <-- Test E2E del flujo REGISTER + activacion + LOGIN
|
|-- shared/                          <-- Codigo compartido entre juegos
|   |-- network.h                    <-- Capa UNAPI TCP
|   |-- protocol.h                   <-- Protocolo binario
|   |-- log.h                        <-- Logging a fichero MSX-DOS 2
|   |-- lobby_client.h               <-- Lee LOBBY.DAT (lanzado desde LOBBY.COM)
|   +-- lobby_client.c               <-- Implementacion lobby_client
|
|-- lobby/                           <-- LOBBY.COM standalone (Screen 0)
|   +-- lobby_main.c                 <-- Selector de juegos + connect + salas
|
|-- client/                          <-- Ball Demo (GAME_ID=0x01, Screen 5)
|   +-- msxonline.c                  <-- build: MOL_039
|
|-- damas/                           <-- Damas (GAME_ID=0x02, Screen 4)
|   +-- damas.c                      <-- build: DAM_022
|
|-- burdyn/                          <-- RPG Crawler (GAME_ID=0x03, Screen 4)
|   |-- burdyn.c                     <-- build: BURD_029
|   +-- editor.html                  <-- Editor de mapas HTML
|
|-- parchis/                         <-- Parchis (GAME_ID=0x04, Screen 4)
|   |-- parchis.c                    <-- build: PAR_011 (solo en MSXgl)
|   +-- path_editor.html             <-- Editor visual del tablero
|
|-- texas/                           <-- Texas Hold'em (GAME_ID=0x05, Screen 4)
|   +-- texas.c                      <-- build: TEX_030
|
|-- tetris/                          <-- Tetris 4P (GAME_ID=0x06, Screen 4)
|   +-- tetris.c                     <-- build: TET_024
|
|-- among/                           <-- Among MSX (GAME_ID=0x07, en desarrollo)
|   +-- among.c                      <-- build: AMG_001
|
|-- frogflies/                       <-- Frog & Flies (GAME_ID=0x69, Screen 4)
|   |-- frogflies.c                  <-- build: FRG_012
|   |-- sprite_editor.html           <-- Editor sprites 16x16
|   |-- platform_editor.html         <-- Editor posiciones nenufares
|   +-- screen_editor.html           <-- Editor layout tiles
|
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

### VPS: <VPS_IP>

```bash
# Logs
ssh root@<VPS_IP> "journalctl -u msx-server -f"
# Reiniciar
ssh root@<VPS_IP> "systemctl restart msx-server"
# Desplegar
cd ~/Documents/MSXonLIVE/MSXon && sed -i 's/\r$//' server/update.sh && scp server/msx-gameserver.js server/update.sh root@<VPS_IP>:/tmp/ && ssh root@<VPS_IP> "bash /tmp/update.sh"
```

---

## AUTH BACKEND (Fase 2 - server/auth-store.js + msx-web.js)

Sistema de cuentas de usuario con registro autoservicio via QR. Coexiste con
el AUTH legacy (token `0xDEADBEEF`) que sigue usando el ghost-service.

### Arquitectura

- **Storage**: `users.json` con escritura atomica (writeFile.tmp + rename, debounce 500ms).
  Pendings en RAM con TTL 10 min, NO se persisten (restart invalida tokens en vuelo).
- **Password hashing**: `scrypt` builtin de Node, formato `scrypt$N$salt_hex$hash_hex` con N=16384.
- **Sesiones**: `Map<sessionId, {username, role, expiresAt}>` en RAM, TTL 5 min.
- **Roles**: `superadmin` (0x03) / `admin` (0x02) / `user` (0x01).
- **Bootstrap superadmin**: archivo `/opt/msx-server/.superadmin` con un username (no en git).
  Cuando ese username activa su cuenta, role=superadmin se aplica automaticamente.

### Flujo de registro autoservicio (decidido en plan v3)

1. MSX → server: `REGISTER [user][nick]` (cmd 0x16)
2. Server crea pending, devuelve `REG_PENDING [token-8B]` (cmd 0x17)
3. MSX renderiza QR (con `qrcode_tiny.c` de MSXgl) con URL:
   `https://msxon.nosignalbbs.com/r?u=<user>&t=<token>`
4. Usuario escanea con movil → form HTML pide password (4-16 chars ASCII) → POST → server hashea con scrypt → cuenta activa
5. MSX → server: `LOGIN [user][pass]` (cmd 0x13) → `LOGIN_OK [role][nickLen][nick][session 4B]` (cmd 0x14)

### Web HTTPS (Caddy + letsencrypt)

- Subdominio `msxon.nosignalbbs.com` (DNS A → <VPS_IP>).
- Caddy en VPS hace TLS termination + reverse_proxy a `localhost:8080`.
- Caddyfile (`/etc/caddy/Caddyfile`):
  ```
  msxon.nosignalbbs.com {
      reverse_proxy 127.0.0.1:8080
  }
  ```
- Cert renovacion automatica.

### Permisos VPS criticos

- Server corre como `User=nobody` (en `msx-server.service`).
- `/opt/msx-server/` debe permitir escritura a `nogroup` para que se cree `users.json`:
  `chgrp nogroup /opt/msx-server && chmod 775 /opt/msx-server`.
- `.superadmin` debe ser legible por `nobody`:
  `chown nobody:nogroup /opt/msx-server/.superadmin && chmod 644`.
- Si fallan los permisos, en logs aparece `[auth] flush error: EACCES: permission denied`.

### Catalogo de juegos dinamico (`games-store.js` + `games.json`)

**Implementado**. El cliente MSX ya no tiene la tabla `g_Games[]` hardcoded; tras `LOGIN_OK` envia `CMD_GAME_LIST` (0x27) y el server responde con la lista filtrada por rol.

Schema `games.json`:
```json
{ "id": 2, "name": "DAMAS", "com": "DAMAS", "max": 2, "proto": 1, "visibility": "public" }
```

`visibility`: `public` (todos) / `private` (solo admin/superadmin/service, marcado `[P]` en menu) / `disabled` (no se envia a nadie).

Filtrado server-side en `msx-gameserver.js` handler `CMD.GAME_LIST`:
- `user` → solo public.
- `admin` / `superadmin` / `service` → public + private.
- `disabled` → nunca enviado.

Sin enforcement aun en `ROOM_CREATE`/`ROOM_JOIN` (un cliente con paquete crafted podria entrar a sala privada conociendo el roomId). Decision consciente para hobbistas; revisar si la comunidad crece.

### Admin CLI (`admin.js`)

Operacion sin tocar JSONs a mano:
```bash
node admin.js list-users | list-games | list-pending
node admin.js promote <user> <role>  | demote <user>  | reset-password <user>
node admin.js set-visibility <id> <vis>  | add-game ... | del-game <id>
```
Lee/escribe directamente `users.json` y `games.json`. El server NO tiene hot-reload; reiniciar `msx-server` tras cambios.

### Panel web `/admin`

`https://msxon.nosignalbbs.com/admin` — accesible desde movil. Login con cuenta `admin`/`superadmin`. Cookie firmada HMAC SHA256, secreto en `/opt/msx-server/.cookie-secret` (no commiteado, generado al primer arranque, persiste reinicios).

Endpoints en `msx-web.js`:
- `GET /admin/login`, `POST /admin/login` (form), `GET /admin/logout`.
- `GET /admin` (dashboard: tabla users + pendientes + games).
- `POST /admin/promote`, `/admin/reset-password`, `/admin/set-visibility`, `/admin/restart-server`.

Restart-server llama `systemctl restart msx-server`. El proceso del server se mata pero la respuesta HTTP ya esta enviada antes del spawn (`detached: true`, `unref()`).

### Cliente MSX `lobby/msxon.c` (binario `MSXON.COM`)

Reemplaza al antiguo `LOBBY.COM`. **Toda la UI en Screen 5** (V9938 bitmap, BBS verde sobre negro). Estados:

- `ST_INTRO` → logo MSXon (bitmap) + arpegio PSG Do-Mi-Sol-Do.
- `ST_CHOICE` → 1=LOGIN / 2=REGISTER / ESC=salir.
- `ST_LOGIN` → form usuario+pw. ENTER en pw → `NetConnectAndAuth` (AUTH legacy) → `NetLogin` → si OK, `NetFetchGameList` → `DrawMenu` → `ST_MENU`.
- `ST_REGISTER` → form usuario+nick. ENTER en nick → `NetRegister` → si OK (`REG_PENDING`), guarda token y va a `ST_QR`.
- `ST_QR` → genera QR con `tool/qrcode_tiny` (CPU-bound ~20s en hardware), spinner animado con hook H_TIMI ciclando paletas 4-7, render con `VDP_CommandHMMV` por modulo.
- `ST_MENU` → menu de juegos (tabla dinamica `g_Games[16]` rellenada desde GAME_LIST). Cursores + ENTER, ESC sale.
- `ST_LOBBY` → lista de salas filtrada por gameId. ENTER une, C crea, R refresca.
- `ST_WAITING` → muestra players de la sala. Si `numP >= max` o llega el ultimo via `PLAYER_JOINED` → auto-launch del .COM.

**Lanzamiento sin AUTOEXEC.BAT — keyboard stuffing**: en `LaunchGame()`, antes de `Bios_Exit(0)`, MSXon escribe `g_CurGame->comFile + \r` directamente al buffer del teclado MSX (`0xFBF0..0xFC0F`) y ajusta `PUTPNT/GETPNT` (`0xF3F8/0xF3FA`). El shell de MSX-DOS lee el buffer al recuperar control y ejecuta el comando como si lo tuvieras tipeado. Idiomatico en la escena MSX, sin trampolines `_LAUNCH.BAT`.

`LOBBY.DAT` se sigue escribiendo (magic 0xAA, 8 bytes con conn/pid/roomId/active/gameId/proto) para que el juego siga la conexion del lobby. Los 8 juegos online ya recompilados con `LobbyClient_Load()` en su `main` reusan la conexion.

**Edge detection del ENTER**: cuando se cambia de estado por paquete del server (`ST_LOBBY`, `ST_WAITING`, `ST_LAUNCHING`), `ProcessPacket` hace `while(Keyboard_IsKeyPressed(KEY_RET)) Halt();` antes de aplicar el cambio. Asi el ENTER del menu no se "arrastra" disparando JOIN inmediato en el lobby.

**Tamano binario**: `lobby/msxon.c` con Screen 5 + bitmap font + qrcode_tiny + logo + dispatcher de estados pesa ~48KB. Requiere `ForceRamAddr=0xC000` en `project_config.js`.

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

### Ghost service modular (v2.0)
`ghost-service.js` refactorizado a entry point + 5 archivos por juego heredando de `GhostBase`. Soluciona cuelgue tras horas: backoff exponencial (1s -> 2s -> 4s ... cap 60s), `recvBuf` con cap 64KB, SIGTERM clean shutdown, `GhostRoomRegistry` para coordinar roomId compartido entre ghosts del mismo juego (Burdyn, Tetris, Parchis). Validado con test largo local 12h+ (RSS estable 30-38MB, 0 errores) antes de deploy al VPS.

### Auth backend (Fase 2 cerrada 2026-05-09)
Cuentas de usuario con scrypt + sesiones, registro autoservicio via QR mostrado en MSX y form HTTPS en movil (`msxon.nosignalbbs.com` con Caddy). Detalle completo en seccion "AUTH BACKEND" arriba. AUTH legacy (0xDEADBEEF) mantenido para ghost-service y server-status sin tocar. Cuenta superadmin bootstrap via archivo `.superadmin` no commiteado.

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

## LOBBY.COM (standalone)

- Screen 0 (texto 40 columnas), sin tileset
- Selector de 8 juegos con cursores + ENTER
- Diagnostico UNAPI, connect, auth (misma logica que lobby.c)
- Lista de salas filtrada por gameId, crear/unir/refrescar
- Waiting room con jugadores, host pulsa S
- Al GAME_START: escribe LOBBY.DAT (8 bytes) + _LAUNCH.BAT, sale con Bios_Exit(0)
- AUTOEXEC.BAT en bucle: LOBBY -> _LAUNCH.BAT -> juego -> LOBBY
- Todos los juegos soportan lobby_client.h (lee LOBBY.DAT, salta a PLAYING)
- Backward compatible: juegos funcionan sin LOBBY.COM (lobby inline o offline)
- LOBBY.DAT: magic(0xAA) + conn + pid + roomId + active + gameId + protoVer + reserved
- Conexion UNAPI persiste entre programas (vive en el cartucho, no en RAM)
- Compilar: `cd MSXgl/projects/lobby && bash build.sh`

---

## FROG & FLIES (FRG_012)

- Screen 4 (Graphic 3, 32x24 tiles)
- 4 ranas en nenufares, cazan moscas volantes
- Charge-jump: 3 potencias (tap/short/long hold)
- Salto vertical u horizontal segun direccion durante carga
- Lengua en el aire (SPACE), colision con moscas
- Moscas gestionadas 100% por el servidor (frogflies-handler.js)
- Servidor spawna moscas cada 1.5s, mueve a 10Hz, envia posiciones
- Jugador envia PKT_CATCH(flyIdx), servidor valida y broadcast PKT_SCORE
- Win condition: primero en 20 moscas gana (servidor envia PKT_WINNER)
- AI frogs SOLO offline (online desincronizadas entre clientes)
- Sprites: idle R/L, jump R/L, fly1, fly2, tongue (slots 0-24)
- Font A-Z y 0-9 inyectado en tiles 200-235 sobre tileset PNG
- Usa lobby.h para Lobby_Poll/SendStateUpdate durante juego
- GAME_ID = 0x69, modo RELAY + handler

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

_Proyecto de Antxiko · MSXon v2.0_
