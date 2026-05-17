# MSXon — v0.8

Plataforma de juegos online multijugador para ordenadores **MSX2 reales** sobre TCP/IP.

> **Estado v0.8 (2026-05-17)**: pipeline completo lobby ↔ juego ↔ lobby funcionando para los 7 juegos online. Lifecycle de sala con detección de winner, cierre automático y rechazo de JOIN durante partida (modo RELAY). UX del lobby pulida (cursor parcial sin parpadeo, mensajes de error visibles). El sistema es jugable end-to-end contra ghosts en VPS.

```
  MSX ──ESP-01 WiFi──┐                 ┌──Badcat──── MSX
                     │    ┌────────┐   │
  MSX ──GR8Net──────┼────│ Server │───┤
                     │    │ Node.js│   │
  MSX ──ESP-FPGA────┘    └────────┘   └──GR8Net──── MSX
                          TCP :9876
```

---

## Juegos

| Juego | GAME_ID | Jugadores | Modo | Screen | Lifecycle | Estado |
|-------|---------|-----------|------|--------|-----------|--------|
| **Ball Demo** | 0x01 | 4 | RELAY | Screen 5 | — | Funcional (MOL_039) |
| **Damas Online** | 0x02 | 2 | RELAY | Screen 4 | GAME_END | Funcional (DAM_022) |
| **Burdyn RPG** | 0x03 | 14 | AGGREGATE | Screen 4 | MMO (sin GAME_END) | Funcional (BURD_029) |
| **Parchis** | 0x04 | 4 | RELAY | Screen 4 | GAME_END | Funcional (PAR_011) |
| **Texas Hold'em** | 0x05 | 6 | RELAY+handler | Screen 4 | GAME_END | Funcional (TEX_030) |
| **Tetris 4P** | 0x06 | 4 | RELAY | Screen 4 | GAME_END | Funcional (TET_024) |
| **Among MSX** | 0x07 | 4-8 | RELAY | Screen 4 | — | En desarrollo (AMG_001, `disabled` en games.json) |
| **Bomberman** | 0x08 | 4 | RELAY | Screen 4 | GAME_END | Funcional (BMB_001) |
| **Frog & Flies** | 0x69 | 4 | RELAY+handler | Screen 4 | — | Funcional (FRG_012) |

**Lifecycle GAME_END**: cuando el cliente detecta winner único, muestra el mensaje 5s, envía `CMD_GAME_END` (0x33) al server y vuelve automáticamente a MSXon. El server difunde el END a la sala (los ghosts resetean su estado), pone `gameStarted=false` y la sala vuelve a admitir jugadores. Si alguien intenta entrar a una sala con `gameStarted=true` en modo RELAY, el server responde `ROOM_FULL` — MSXon lo muestra como "SALA OCUPADA O EN PARTIDA" en rojo y auto-refresca. En modo AGGREGATE (Burdyn MMO) el JOIN se permite siempre y el server reenvía `GAME_START` al recién entrado.

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

### Bomberman (`bomberman/`)
4 players grid-based: pone bombas, último vivo gana. Tablero compartido por relay. Mensaje "GANA Pn" 5s tras detectar winner único → `CMD_GAME_END` → vuelta a MSXon.

### Among MSX (`among/`)
Among Us simplificado para 4-8 jugadores. 7 habitaciones, 1 impostor. En desarrollo.

---

## MSXON.COM — Lobby universal

Programa standalone que hace de punto de entrada para todos los juegos. Toda la interfaz va en **Screen 5** (bitmap, BBS verde sobre negro):

1. **Intro**: logo MSXon con arpegio PSG (Do-Mi-Sol-Do).
2. **CHOICE**: `[1] LOGIN  [2] REGISTRARSE  [ESC] salir`.
3. **LOGIN**: form usuario+password. Tras `LOGIN_OK` el cliente pide el catálogo dinámico con `CMD_GAME_LIST`.
4. **REGISTER**: form usuario+nick. Tras `REG_PENDING` el cliente renderiza un **QR** (con `tool/qrcode_tiny` de MSXgl) apuntando a `https://msxon.nosignalbbs.com/r?u=&t=`. El usuario escanea con el móvil, define password en el form HTML y vuelve al MSX a hacer LOGIN.
5. **Menú dinámico**: muestra los juegos que el server envió en `GAME_LIST`, filtrados por el rol del usuario (`[P]` marca los `private` solo visibles a admin/superadmin; los `disabled` no se envían).
6. **Lobby de sala**: lista de salas existentes para el juego elegido. ENTER une, C crea, R refresca.
7. **Waiting**: muestra jugadores conectados. Si la sala se llena, MSXon **lanza el .COM del juego automáticamente**.

### Lanzamiento del juego — keyboard stuffing

En lugar de depender de un trampolín en `AUTOEXEC.BAT`, MSXon escribe el comando del juego directamente al **buffer del teclado MSX** (`0xFBF0..0xFC0F`, ajustando `PUTPNT/GETPNT` en `0xF3F8/0xF3FA`) y termina con `Bios_Exit(0)`. El shell de MSX-DOS lee el buffer al recuperar control y ejecuta el comando — sin `_LAUNCH.BAT`, sin `autoexec.bat`. Lo mismo de vuelta: cuando el juego termina, `GameRT_ExitToLobby()` stuffea `MSXON\r` y vuelve a DOS → MSXon re-arranca.

`LOBBY.DAT` se escribe igualmente para que el juego lea conexión, pid y sala. La conexión TCP persiste entre programas porque UNAPI vive en el cartucho (ESP-01 WiFi / GR8Net / Badcat / ESP-FPGA), no en la RAM del .COM.

### Patrón thin-client: `game_runtime`

Cada `.COM` de juego es un thin-client que delega plumbing en `shared/game_runtime.{c,h}`:

- `GameRT_Init()` lee `LOBBY.DAT` (magic `0xAA`, 8 bytes con `{conn,pid,roomId,active,gameId,protoVer}`). Si no hay LOBBY.DAT, el juego imprime "lanzar desde MSXon" y sale (sin AUTH ni Net_Open propios).
- `GameRT_Send(cmd, payload, len)` construye el header FM y manda. `GameRT_SendPing()` para keepalive.
- `GameRT_Poll(cb, maxPkts)` lee paquetes y los pasa al callback `void cb(u8 cmd, u8 senderPid, u8* payload, u8 len)`.
- `GameRT_ExitToLobby()` cierra TCP + keyboard stuffing `MSXON\r` + `Bios_Exit(0)`.

El cliente no maneja conexión, autenticación ni rooms — todo eso lo hizo MSXon. Esto eliminó el lobby propio que cada juego tenía duplicado.

---

## Estructura del repo

```
MSXon/
├── server/              Servidor Node.js (TCP 9876 + HTTP 8080 detras de Caddy)
│   ├── msx-gameserver.js    Servidor principal (relay + aggregate + handlers + auth + web)
│   ├── auth-store.js        Almacen de usuarios (JSON atomico + scrypt + pending tokens)
│   ├── games-store.js       Catalogo de juegos (JSON atomico + visibility public/private/disabled)
│   ├── games.json           Catalogo (commiteado, sin datos sensibles)
│   ├── msx-web.js           Endpoint HTTP /r (activacion QR) + panel /admin (login + gestion)
│   ├── admin.js             CLI de administracion (list-users, promote, set-visibility, ...)
│   ├── test-game-list.js    Test del handler GAME_LIST
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
├── lobby/               MSXON.COM standalone (Screen 5: intro/login/register/QR/menu)
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

**IP**: <VPS_IP>:9876

Tres modos de sala:
- **RELAY**: reenvía STATE_UPDATE a todos
- **AGGREGATE**: almacena estados, envía WORLD_STATE a 10Hz (Burdyn)
- **RELAY+handler**: relay + logica server-side por GAME_ID (Texas, Frog&Flies)

```bash
# Logs
ssh root@<VPS_IP> "journalctl -u msx-server -f"

# Reiniciar
ssh root@<VPS_IP> "systemctl restart msx-server"

# Desplegar
cd MSXon && sed -i 's/\r$//' server/update.sh && \
scp server/msx-gameserver.js server/game-handlers/*.js server/update.sh \
root@<VPS_IP>:/tmp/ && ssh root@<VPS_IP> "bash /tmp/update.sh"
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

**IMPORTANTE**: `ForceRamAddr` en `project_config.js` es obligatorio para evitar conflictos UNAPI. Los juegos pequeños usan `0x8000` (32KB de código). MSXon (`lobby/msxon.c`) tiene Screen 5 + bitmap font + qrcode_tiny + logo y supera 32KB → usa `0xC000`.

---

## Catálogo de juegos y administración

### `games.json` + visibility tri-state

El catálogo vive en `server/games.json` (gestionado con `games-store.js`). Cada juego tiene `visibility`:

- `public` → visible para cualquier usuario.
- `private` → solo visible para `admin` / `superadmin` (marcado `[P]` en el menú).
- `disabled` → no aparece en el menú para nadie. Útil para juegos en desarrollo o retirados temporalmente.

El cliente MSX recibe la lista filtrada server-side cuando manda `CMD_GAME_LIST` tras `LOGIN_OK`.

### CLI `admin.js`

Operación desde el VPS (sin tocar JSONs a mano):

```bash
node admin.js list-users                         # tabla de usuarios + roles + lastLogin
node admin.js list-games                         # catálogo + visibility
node admin.js list-pending                       # tokens de activación pendientes
node admin.js promote <user> <role>              # user/admin/superadmin
node admin.js demote  <user>                     # atajo de "promote <user> user"
node admin.js reset-password <user>              # invalida pw + genera token QR (URL clicable)
node admin.js set-visibility <id> <vis>          # public | private | disabled
node admin.js add-game <id> <name> <com> <max> <proto> <vis>
node admin.js del-game <id>
```

Requiere reinicio del servidor para que `games-store` y `auth-store` carguen el JSON nuevo (no hay hot-reload):
`systemctl restart msx-server`.

### Panel web `/admin`

`https://msxon.nosignalbbs.com/admin` — login con cuenta `admin` o `superadmin`. Permite:

- Ver y gestionar usuarios (cambiar role inline, reset password con URL clicable).
- Ver registros pendientes de activación (token + URL + minutos restantes).
- Cambiar visibility de juegos.
- **Reiniciar el servidor** con un botón.

Cookie de sesión firmada con HMAC SHA256 (24h). Secreto persistente en `/opt/msx-server/.cookie-secret` (no commiteado).

---

## Hardware soportado

- **MSX2** con MSX-DOS 2
- **Red**: cualquier solución UNAPI:
  - **ESP-01 WiFi** con firmware UNAPI de ducasp (cartucho dongle).
  - **GR8Net** (cartucho Ethernet de GR8Bit).
  - **Badcat** (cartucho de red).
  - **ESP-FPGA** para máquinas tipo OCM (módulo WiFi integrado en el FPGA).
  - **[openMSXnet](https://github.com/antxiko/openMSXnet)** — fork de openMSX con device UNAPI builtin para desarrollar/testear sin hardware.
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
