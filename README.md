# MSX Online

Sistema de juego online multijugador para ordenadores **MSX2 reales** sobre TCP/IP.

Un servidor Node.js gestiona salas y reenvía paquetes entre clientes MSX conectados por Ethernet (ObsoNET/GR8NET) usando el stack TCP/IP [InterNestor Lite](https://github.com/Konamiman/InterNestor-Lite) vía [UNAPI](https://github.com/Konamiman/MSX-UNAPI-specification).

```
  MSX 8250 ──ObsoNET──┐                 ┌──ObsoNET── MSX 8250
                       │    ┌────────┐   │
  MSX turboR ─GR8NET──┼────│ Server │───┤
                       │    │ Node.js│   │
  MSX 8245 ──ObsoNET──┘    └────────┘   └──GR8NET── MSX 8280
                            TCP :9876
```

---

## Servidor (`server/`)

Relay TCP puro escrito en Node.js. **Sin dependencias externas** (solo el módulo nativo `net`). No contiene lógica de juego — solo autentica clientes, gestiona salas y reenvía paquetes entre jugadores.

### Características

- Protocolo binario ligero (6 bytes header + payload variable)
- Salas de hasta 4 jugadores simultáneos (máx 255 salas)
- Autenticación por token de 4 bytes
- Buffer acumulativo para manejar fragmentación TCP (crítico con ObsoNET)
- Timeout de 90s por conexión (generoso para el Z80)
- Keepalive via PING/PONG
- ECONNRESET silenciado (muy frecuente con hardware MSX real)

### Requisitos

- Node.js >= 20 LTS

### Uso local

```bash
cd server
node msx-gameserver.js
```

El servidor escucha en el puerto **9876/TCP**. Para desarrollo con recarga automática:

```bash
node --watch msx-gameserver.js
```

### Cliente de prueba

Incluye `test-client.js` para depurar el servidor sin hardware MSX:

```bash
node test-client.js
```

### Despliegue en VPS

```bash
# Automático
bash server/deploy.sh

# Manual
sudo mkdir -p /opt/msx-server
sudo cp server/msx-gameserver.js /opt/msx-server/
sudo cp server/msx-server.service /etc/systemd/system/
sudo systemctl daemon-reload && sudo systemctl enable --now msx-server
sudo ufw allow 9876/tcp
```

### Protocolo binario

```
[0x46][0x4D][CMD][ROOM][PID][LEN][...payload...]
  'F'   'M'   │    │    │    │
              cmd  sala  pid  longitud payload (0-255)
```

| CMD | Nombre | Dirección |
|-----|--------|-----------|
| `0x10` | AUTH | MSX → SRV |
| `0x11` | AUTH_OK | SRV → MSX |
| `0x20` | ROOM_CREATE | MSX → SRV |
| `0x21` | ROOM_JOIN | MSX → SRV |
| `0x23` | ROOM_INFO | SRV → MSX |
| `0x26` | ROOM_LIST | Ambos |
| `0x30` | PLAYER_JOINED | SRV → MSX |
| `0x31` | PLAYER_LEFT | SRV → MSX |
| `0x40` | STATE_UPDATE | Ambos |

Añadir un nuevo juego = definir un nuevo GAME_ID y cambiar el payload de STATE_UPDATE en el cliente. **El servidor no cambia.**

---

## Cliente MSX (`client/`)

Programa `.COM` para **MSX2 + MSX-DOS 2** compilado con [SDCC](https://sdcc.sourceforge.net/) y [MSXgl](https://github.com/aoineko-fr/MSXgl).

### Características

- Screen 5 (256x212, 16 colores)
- Sprites 16x16 con color por jugador
- HUD con información de sala y estado
- Input por teclado (cursores) y joystick
- Lobby con lista de salas activas (crear, unirse, refrescar)
- Máquina de estados: conexión → auth → lobby → sala → juego → salida
- Salida limpia a MSX-DOS 2 con ESC

### Hardware necesario

| Componente | Descripción |
|------------|-------------|
| MSX2 | Philips NMS 8245/8250/8280, Sony HB-F9P, etc. |
| MSX-DOS 2 | En ROM o cargado desde disco |
| Tarjeta Ethernet | [ObsoNET](http://www.yourspec.com/ObsoNET/) (BIOS >= 1.1) o GR8NET |
| Stack TCP/IP | [InterNestor Lite 2.x](https://github.com/Konamiman/InterNestor-Lite) cargado como TSR |

### Compilación

Requiere MSXgl clonado en el mismo nivel que el proyecto:

```bash
# 1. Clonar MSXgl (si no lo tienes)
git clone https://github.com/aoineko-fr/MSXgl.git

# 2. Copiar el proyecto msxonline dentro de MSXgl
cp -r client/ MSXgl/projects/msxonline/

# 3. Compilar
cd MSXgl/projects/msxonline
bash build.sh
```

La salida se genera en:
- `out/msxonlin.com` — binario MSX-DOS 2 (~11 KB)
- `emul/dsk/DOS2_msxonlin.dsk` — imagen de disco 720K bootable

### Configuración

Editar `msxonlin.c` antes de compilar:

```c
static const u8 SERVER_IP[4] = { 217, 154, 107, 144 }; // IP del servidor
#define SERVER_PORT     9876
```

### Ejecución en MSX real

1. Arrancar MSX-DOS 2
2. Cargar InterNestor Lite (`INL.COM`)
3. Ejecutar `MSXONLIN.COM`

### Emulador (openMSX)

```bash
openmsx -machine "Philips_NMS_8250" -ext msxdos2 \
  -diska "MSXgl/projects/msxonline/emul/dsk/DOS2_msxonlin.dsk"
```

### Binarios precompilados

La carpeta `build/` incluye el último binario compilado y el contenido del disco listo para copiar a un medio MSX:

```
build/
├── bin/
│   ├── msxonlin.com         ← Binario MSX-DOS 2
│   └── DOS2_msxonlin.dsk    ← Imagen de disco 720K
└── dsk/
    ├── MSXDOS2.SYS          ← Kernel MSX-DOS 2
    ├── COMMAND2.COM          ← Intérprete de comandos
    ├── msxonlin.com          ← El juego
    └── autoexec.bat          ← Arranque automático
```

---

## Cliente PC (`pc-client/`)

Cliente de prueba en HTML5 Canvas que habla el mismo protocolo que el MSX. Permite probar el servidor y jugar en la misma sala que clientes MSX reales desde el navegador.

### Características

- Simula Screen 5 del MSX (256x212 escalado x3)
- Renderiza sprites como bolas de colores (paleta TMS9918)
- HUD con info de sala y jugadores
- Mismo protocolo binario que el cliente MSX
- Bridge TCP↔WebSocket en Node.js (una sola dependencia: `ws`)

### Uso

```bash
cd pc-client
npm install
node bridge.js [server_ip] [server_port]
```

Abre `http://localhost:8080` en el navegador. Por defecto conecta a `127.0.0.1:9876`.

Para conectar al VPS:

```bash
node bridge.js 217.154.107.144 9876
```

### Controles

**Lobby:**

| Tecla | Acción |
|-------|--------|
| Flechas arriba/abajo | Navegar lista de salas |
| ENTER | Unirse a sala seleccionada |
| C | Crear sala nueva |
| R | Refrescar lista |
| J | Introducir Room ID manualmente |
| ESC | Desconectar |

**En partida:**

| Tecla | Acción |
|-------|--------|
| Flechas | Mover |
| ESC | Salir de sala (vuelve al lobby) |

### Multijugador (4 ventanas)

Abre varias pestañas en `http://localhost:8080`. Cada una muestra el lobby con las salas activas. Uno crea sala con **C**, los demás la seleccionan y pulsan **ENTER** para unirse.

Para auto-join directo (sin lobby), usa el parámetro `?join=N`:

```
http://localhost:8080?join=8    ← Se une directamente a sala 8
```

Cada jugador recibe un color distinto: P1=blanco, P2=cian, P3=rojo, P4=amarillo.

---

## Estructura del proyecto

```
MSXonLIVE/
├── server/
│   ├── msx-gameserver.js     ← Servidor TCP (único archivo)
│   ├── test-client.js        ← Cliente de prueba Node.js
│   ├── deploy.sh             ← Script de despliegue VPS
│   ├── update.sh             ← Script de actualización VPS
│   ├── msx-server.service    ← Unidad systemd
│   └── package.json
├── client/
│   ├── msxonline.c           ← Código fuente del cliente MSX
│   ├── network.h             ← Capa de abstracción UNAPI TCP
│   ├── protocol.h            ← Protocolo binario (compartido)
│   └── msxgl_config.h        ← Configuración de MSXgl
├── pc-client/
│   ├── bridge.js             ← Bridge TCP↔WebSocket
│   ├── index.html            ← Cliente HTML5 Canvas
│   └── package.json
├── build/                    ← Binarios compilados
├── docs/                     ← Documentación PDF
└── CLAUDE.md                 ← Contexto técnico completo
```

> **Nota**: MSXgl no está incluido en el repositorio. Clónalo por separado desde [github.com/aoineko-fr/MSXgl](https://github.com/aoineko-fr/MSXgl).

---

## Licencia

MIT

---

*Proyecto de [FX-Media Audio Video, S.L.](https://fx-media.es) — noSignal BBS*
