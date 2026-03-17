# MSX ONLINE — Contexto del proyecto para Claude
# Ultima actualizacion: 2026-03-17
# Estado: Cliente compila y ejecuta en MSX2 (probado en openMSX Philips NMS 8250 + MSX-DOS 2)

---

## QUE ES ESTE PROYECTO

Sistema de juego online para ordenadores MSX reales (Z80) con dos componentes:

1. **Servidor Node.js** (`server/msx-gameserver.js`) — relay TCP puro, sin logica de juego
2. **Cliente MSX** (`MSXgl/projects/msxonline/msxonlin.c`) — programa .COM para MSX2 + MSX-DOS 2

El servidor solo gestiona salas y reenvia paquetes. Anadir un nuevo juego = cambiar el payload de STATE_UPDATE en el cliente. El servidor no cambia.

---

## EMPRESA

- **FX-Media Audio Video, S.L.** — empresa espanola de doblaje y postproduccion audiovisual
- Proyecto hobby/experimento de retro-computing
- Mantienen noSignal BBS (Mystic BBS, orientado a MSX/C64/Spectrum/Amstrad/Atari)

---

## ESTRUCTURA DE ARCHIVOS (real, verificada)

```
MSXonLIVE/
|
|-- CLAUDE.md                          <-- ESTE ARCHIVO
|
|-- msxonline.code-workspace          <-- Workspace VSCode (multi-carpeta)
|
|-- server/
|   |-- msx-gameserver.js              <-- Servidor TCP Node.js (UNICO archivo de logica)
|   |-- test-client.js                 <-- Cliente de prueba Node.js (simula MSX sin hardware)
|   |-- deploy.sh                      <-- Script de despliegue automatizado para VPS
|   |-- package.json
|   +-- msx-server.service             <-- Unidad systemd para VPS Ubuntu
|
|-- MSXgl/                             <-- Libreria MSXgl completa (submodulo/clone)
|   |-- engine/
|   |   |-- src/                       <-- Fuentes de la libreria
|   |   |   |-- vdp.h, input.h, print.h, bios.h, system.h, core.h...
|   |   |   |-- network/unapi_tcp.h    <-- API TCP/IP UNAPI
|   |   |   +-- network/unapi_tcp.asm  <-- Implementacion ASM del wrapper UNAPI
|   |   +-- content/font/              <-- Fuentes bitmap embebidas
|   |       +-- font_mgl_sample6.h     <-- Fuente usada por el cliente (6px)
|   |
|   +-- projects/msxonline/            <-- ** DIRECTORIO DE TRABAJO DEL CLIENTE **
|       |-- msxonlin.c                 <-- Codigo fuente principal (EDITAR ESTE)
|       |-- msxonline.c                <-- Copia de respaldo (no la usa el build)
|       |-- network.h                  <-- Capa de abstraccion UNAPI TCP
|       |-- protocol.h                 <-- Protocolo binario (compartido con servidor)
|       |-- msxgl_config.h             <-- Configuracion de modulos MSXgl
|       |-- project_config.js          <-- Configuracion del build system MSXgl
|       |-- build.sh                   <-- Script de compilacion
|       |-- out/                       <-- Salida de compilacion
|       |   |-- msxonlin.com           <-- Binario MSX-DOS 2 compilado
|       |   |-- msxgl.lib              <-- Libreria precompilada
|       |   |-- crt0_dos.rel           <-- Startup code MSX-DOS
|       |   +-- unapi_tcp.rel          <-- Modulo TCP compilado
|       +-- emul/
|           |-- dos2/                  <-- Contenido del disco MSX-DOS 2
|           |   |-- MSXDOS2.SYS, COMMAND2.COM, msxonlin.com, autoexec.bat
|           +-- dsk/
|               +-- DOS2_msxonlin.dsk  <-- Imagen de disco 720K lista para usar
|
|-- build/                             <-- Copia de resultados de compilacion
|   |-- bin/                           <-- msxonlin.com + DOS2_msxonlin.dsk
|   +-- dsk/                           <-- Contenido del disco suelto
|
|-- client/                            <-- Copia de referencia (NO compilar aqui)
|   |-- msxonline.c                    <-- Copia de msxonlin.c (solo referencia)
|   |-- network.h, protocol.h, msxgl_config.h
|   +-- Makefile                       <-- OBSOLETO — usar build.sh de MSXgl
|
+-- docs/
    |-- MSX_GameServer_v1.0.pdf
    +-- MSX_Client_Doc_v1.0.pdf
```

**IMPORTANTE**: El archivo fuente principal es `MSXgl/projects/msxonline/msxonlin.c` (8 chars, limite DOS). El build system de MSXgl (ProjName="msxonlin") busca ese nombre. `msxonline.c` en el mismo directorio es una copia de respaldo.

---

## COMO COMPILAR

```bash
cd MSXgl/projects/msxonline
bash build.sh
```

Esto genera:
- `out/msxonlin.com` (~11KB)
- `emul/dsk/DOS2_msxonlin.dsk` (720K con MSX-DOS 2 + autoexec.bat)

**NO usar el Makefile de client/**. El build system de MSXgl maneja crt0, linkado, conversion hex y empaquetado DSK correctamente.

### Requisitos de compilacion (macOS)

- SDCC instalado en `/usr/local/bin/sdcc`
- Node.js (para el build script de MSXgl)
- MSXgl ya esta incluido en el repo

### project_config.js — parametros clave

```javascript
ProjName = "msxonlin";              // Nombre de salida (8 chars DOS)
Target = "DOS2";                     // MSX-DOS 2 .COM
Machine = "2";                       // MSX2
LibModules = [ "system", "bios", "vdp", "print", "input", "memory" ];
AddSources = [ "../../engine/src/network/unapi_tcp.asm" ];
Compiler = "/usr/local/bin/sdcc";    // macOS — ajustar en otro SO
```

---

## COMO PROBAR EN EMULADOR

```bash
openmsx -machine "Philips_NMS_8250" -ext msxdos2 -diska "MSXgl/projects/msxonline/emul/dsk/DOS2_msxonlin.dsk"
```

- `-ext msxdos2` carga la ROM de MSX-DOS 2 (necesaria para arrancar DOS 2)
- El autoexec.bat ejecuta `msxonlin` automaticamente
- Sin ObsoNET/InterNestor mostrara "UNAPI no hallado" — ESC para salir a DOS

---

## HARDWARE MSX TARGET

- **MSX2** (Z80 @ 3.58 MHz, VDP V9938)
- **MSX-DOS 2** (necesario para mapper support)
- **ObsoNET** (tarjeta Ethernet, BIOS >= 1.1 con UNAPI)
- **InterNestor Lite 2.x** (stack TCP/IP, debe cargarse ANTES de ejecutar msxonlin.com)

---

## SERVIDOR NODE.JS (server/msx-gameserver.js)

Archivo unico, sin dependencias externas (solo `net` de Node.js).

### Que hace

- Escucha conexiones TCP en puerto 9876
- Autentica clientes con token de 4 bytes
- Gestiona salas de hasta 4 jugadores
- Hace relay de paquetes STATE_UPDATE entre jugadores de la misma sala
- Broadcast de eventos (PLAYER_JOINED, PLAYER_LEFT, GAME_START)
- Buffer acumulativo para manejar chunks TCP parciales (critico con ObsoNET)
- ECONNRESET silenciado (muy comun con hardware MSX real)

### Arquitectura

- **Sin logica de juego** — el servidor es un relay puro. Solo reenvía STATE_UPDATE a los demas jugadores de la sala. Toda la logica de juego esta en el cliente MSX.
- **Estado global**: `rooms` (Map de roomId -> { gameId, players: Map<pid, socket> })
- **Estado por conexion**: `{ auth, roomId, pid, buffer }` (buffer acumulativo para parsing)
- **Parser de paquetes**: `parsePackets()` — busca magic bytes, extrae header+payload, descarta basura
- **Solo P1 (host)** puede enviar CMD_GAME_START

### Configuracion (en msx-gameserver.js)

```javascript
const PORT               = 9876;
const AUTH_TOKEN         = Buffer.from([0xDE, 0xAD, 0xBE, 0xEF]); // Cambiar en produccion
const MAX_PLAYERS_PER_ROOM = 4;
const TIMEOUT_MS         = 90_000;   // 90s — generoso para el Z80
```

### Despliegue en VPS Ubuntu

```bash
# Copiar archivos
sudo mkdir -p /opt/msx-server
sudo cp server/msx-gameserver.js /opt/msx-server/
sudo cp server/msx-server.service /etc/systemd/system/

# Activar servicio (se reinicia solo si cae)
sudo systemctl daemon-reload
sudo systemctl enable --now msx-server

# Firewall
sudo ufw allow 9876/tcp
```

### Servicio systemd (msx-server.service)

- Ejecuta como usuario `nobody`
- WorkingDirectory: `/opt/msx-server`
- Restart=always, RestartSec=5
- MemoryMax=64M
- Requiere Node.js >= 20 LTS

### Desarrollo local

```bash
cd server
node --watch msx-gameserver.js    # Recarga automatica al editar
```

### Cliente de prueba (server/test-client.js)

Simula un cliente MSX desde Node.js sin necesidad de hardware ni emulador.
Envia AUTH, ROOM_CREATE, STATE_UPDATE. Util para depurar el servidor.

```bash
cd server
node test-client.js
```

### Script de despliegue (server/deploy.sh)

Automatiza la instalacion en VPS Ubuntu: copia archivos, configura systemd, abre firewall.

### Flujo de una conexion tipica

1. Cliente MSX conecta por TCP al puerto 9876
2. Cliente envia CMD_AUTH con token de 4 bytes
3. Servidor valida token -> CMD_AUTH_OK (o AUTH_FAIL + destroy)
4. Cliente envia CMD_ROOM_CREATE con GAME_ID
5. Servidor crea sala, asigna PID=1 -> CMD_ROOM_INFO
6. Otros clientes hacen CMD_ROOM_JOIN -> servidor broadcast CMD_PLAYER_JOINED
7. Bucle de juego: clientes envian CMD_STATE_UPDATE, servidor relay a los demas
8. CMD_PING cada ~5s para mantener viva la conexion (timeout servidor = 90s)
9. Al salir: CMD_ROOM_LEAVE -> servidor broadcast CMD_PLAYER_LEFT y limpia sala

---

## PROTOCOLO BINARIO

```
[0x46][0x4D][CMD][ROOM][PID][LEN][...payload 0-255 bytes...]
  'F'   'M'
```

### Comandos

| CMD  | Nombre          | Direccion | Payload |
|------|-----------------|-----------|---------|
| 0x01 | PING            | Ambos     | - |
| 0x02 | PONG            | SRV->MSX  | - |
| 0x10 | AUTH            | MSX->SRV  | 4 bytes token |
| 0x11 | AUTH_OK         | SRV->MSX  | - |
| 0x12 | AUTH_FAIL       | SRV->MSX  | - |
| 0x20 | ROOM_CREATE     | MSX->SRV  | 1 byte GAME_ID |
| 0x21 | ROOM_JOIN       | MSX->SRV  | 1 byte ROOM_ID |
| 0x22 | ROOM_LEAVE      | MSX->SRV  | - |
| 0x23 | ROOM_INFO       | SRV->MSX  | [ROOM_ID, GAME_ID, N_PLAYERS, MY_PID] |
| 0x30 | PLAYER_JOINED   | SRV->MSX  | 1 byte PID |
| 0x31 | PLAYER_LEFT     | SRV->MSX  | 1 byte PID |
| 0x40 | STATE_UPDATE    | Ambos     | 8 bytes (ver abajo) |

### Payload STATE_UPDATE (8 bytes)

```
[X_HI][X_LO][Y_HI][Y_LO][FRAME][FLAGS][DATA_0][DATA_1]
```

### Token de autenticacion (cambiar en produccion)

Cliente: `protocol.h` → `AUTH_TOKEN_0..3 = 0xDE, 0xAD, 0xBE, 0xEF`
Servidor: `msx-gameserver.js` → `AUTH_TOKEN = Buffer.from([0xDE, 0xAD, 0xBE, 0xEF])`

---

## MAQUINA DE ESTADOS DEL CLIENTE

```
STATE_INIT
  -> STATE_CONNECTING   (Net_Init + Net_Open)
  -> STATE_AUTH_WAIT    (enviado CMD_AUTH, esperando CMD_AUTH_OK)
  -> STATE_CREATE_ROOM  (transicion automatica al recibir AUTH_OK)
  -> STATE_ROOM_WAIT    (enviado CMD_ROOM_CREATE, esperando CMD_ROOM_INFO)
  -> STATE_PLAYING      (bucle de juego activo)
  -> STATE_EXIT         (ESC pulsado -> Game_Shutdown -> Bios_Exit(0))

En cualquier estado de error -> STATE_DISCONNECTED -> ESC -> STATE_EXIT
```

---

## APIs MSXgl VERIFICADAS (NO asumir nombres — siempre grep en engine/src/)

Estas son las funciones REALES de MSXgl usadas en el cliente. Se verificaron contra los headers reales y se probaron en emulador.

### VDP

| Funcion                  | Uso en el cliente |
|--------------------------|-------------------|
| `VDP_SetMode(mode)`      | Cambiar Screen 0/5 |
| `VDP_SetColor(color)`    | Color de borde (NO existe VDP_SetBorderColor) |
| `VDP_FillVRAM(val, destLow, destHigh, count)` | Limpiar VRAM (128K) |
| `VDP_SetSpriteFlag(flags)` | Configurar sprites 16x16 (NO es VDP_SetSprite) |
| `VDP_LoadSpritePattern(data, index, count)` | Cargar patron sprite |
| `VDP_SetSpriteSM1(i, x, y, shape, color)` | Posicionar sprite con color |
| `VDP_CommandHMMV(dx, dy, nx, ny, col)` | Rellenar rectangulo VRAM |
| `VDP_CommandLINE(dx, dy, nx, ny, col, arg, op)` | Dibujar linea |
| `VDP_CommandWait()` | Esperar fin de comando VDP |
| `Halt()` (system.h) | Esperar VBlank (NO existe VDP_WaitVBlank) |

### Print

| Funcion | Nota |
|---------|------|
| `Print_SetMode(PRINT_MODE_BITMAP)` | NO existe Print_SetFontType |
| `Print_SetFont(g_Font_MGL_Sample6)` | NUNCA usar NULL — produce basura en bitmap mode |
| `Print_SetColor(text, bg)` | |
| `Print_SetPosition(x, y)` | |
| `Print_DrawText(string)` | NO es Print_PrintText |
| `Print_DrawChar(chr)` | NO es Print_PrintChar |
| `Print_DrawHex8(value)` | NO es Print_PrintByte |

### Input

| Funcion | Nota |
|---------|------|
| `Keyboard_IsKeyPressed(KEY_ESC)` | |
| `Joystick_Read(JOY_PORT_1)` | NO es JOY_1 |
| `JOY_INPUT_DIR_UP/DOWN/LEFT/RIGHT` | NO es JOY_UP etc. |

### BIOS

| Funcion | Nota |
|---------|------|
| `Bios_Exit(0)` | Salida limpia a MSX-DOS (restaura Screen, llama BDOS) |

### Tipos

- `bool` (NO `bool8`) — definido en core.h como `unsigned char`
- `u8`, `u16`, `c8` — tipos estandar MSXgl

---

## ESTILO DE CODIGO

- C estilo K&R, 4 espacios (no tabs)
- Tipos MSXgl: `u8`, `u16`, `bool` — NO usar `int`, `unsigned char`
- Variables globales con prefijo `g_`
- Constantes: `UPPER_CASE` con prefijo de modulo (`CMD_`, `NET_`, `STATE_FLAG_`)
- Comentarios en espanol
- Sin stdlib — solo MSXgl. No usar malloc, printf, string.h
- Servidor Node.js: `'use strict'`, `const`/`let`, sin `var`

---

## LIMITACIONES Z80 QUE AFECTAN AL DISENO

| Restriccion | Impacto |
|---|---|
| Z80 @ 3.58 MHz | Timeouts generosos (90s en servidor) |
| InterNestor a 50 Hz | Max 4 paquetes procesados por frame en Net_Poll() |
| Buffer TCP ~512-1024B | Payload max 255 bytes por paquete |
| Sin TLS posible | Auth por token en claro. Canal no cifrado |
| ObsoNET drops frecuentes | ECONNRESET silenciado en servidor. STATE_DISCONNECTED en cliente |
| Chunks TCP parciales | Buffer acumulativo obligatorio en servidor Y en cliente |
| REGLA CRITICA | NUNCA llamar Net_Recv() sin comprobar antes Net_Available() |

---

## LIMITACIONES Y NOTAS IMPORTANTES

- **IP del servidor hardcoded** — La IP del VPS esta en `msxonlin.c` como array de bytes. Hay que recompilar para cambiar de servidor. No hay DNS ni configuracion en runtime.
- **Sin TLS** — La conexion es TCP plano. El token de auth viaja en claro. Limitacion del Z80.
- **InterNestor Lite debe cargarse antes** — El TSR de TCP/IP debe estar residente en RAM antes de ejecutar msxonlin.com. Sin el, el programa muestra "UNAPI no hallado".
- **Limite 8 caracteres DOS** — El binario se llama `msxonlin.com` (no `msxonline`) por el limite de nombres de archivo en MSX-DOS.
- **MSXgl es un repositorio git independiente** — El directorio `MSXgl/` tiene su propio `.git`. No es un submodulo del proyecto principal (que aun no es un repo git).
- **client/ es solo referencia** — Los archivos en `client/` son copias de los de `MSXgl/projects/msxonline/`. El directorio de trabajo real es el de MSXgl. Si se edita algo, editar en MSXgl y copiar a client/ si se quiere mantener sincronizado.

---

## WORKSPACE VSCODE (msxonline.code-workspace)

Configurado con multi-carpeta: server/, client/, MSXgl/projects/msxonline/, docs/. Incluye paths de IntelliSense para SDCC + MSXgl y configuracion de formatter JavaScript.

---

## CONFIGURACION DEL CLIENTE (editar antes de compilar)

En `msxonlin.c`:
```c
static const u8 SERVER_IP[4] = { 217, 154, 107, 144 }; // IP del VPS
#define SERVER_PORT     9876
#define MOVE_SPEED      2       // Pixeles/frame
#define PING_INTERVAL   250     // Frames entre keepalives (~5s a 50Hz)
```

---

## PROBLEMAS RESUELTOS

### Salida a MSX-DOS (2026-03-17)
- **Problema**: El programa no volvia a MSX-DOS al pulsar ESC
- **Causa**: `main()` retornaba void y el crt0 intentaba ejecutar BDOS con stack/interrupciones corruptas por las funciones VDP de MSXgl
- **Solucion**: Llamar `Bios_Exit(0)` directamente en `Game_Shutdown()` en vez de confiar en el retorno de main()

### Texto ilegible en HUD (2026-03-17)
- **Problema**: El texto en Screen 5 salia como basura (colores correctos, caracteres ilegibles)
- **Causa**: `Print_SetFont(NULL)` no funciona en modo bitmap — la fuente BIOS no se carga bien
- **Solucion**: Usar fuente embebida `g_Font_MGL_Sample6` via `#include "font/font_mgl_sample6.h"`

---

## PENDIENTE — Proximos pasos

- [ ] **Test en hardware real** — MSX 8250 con ObsoNET + InterNestor Lite
- [ ] **Pantalla de lobby** — listar salas disponibles (requiere CMD_ROOM_LIST en servidor)
- [ ] **Unirse a sala existente** — pantalla de introduccion de ROOM_ID por teclado
- [ ] **Segundo juego** — definir nuevo GAME_ID y payload STATE_UPDATE diferente

---

_Proyecto de FX-Media Audio Video, S.L. · MSX Online Game Server v1.0_
