# MSX Online

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
| **Ball Demo** | 0x01 | 4 | RELAY | Screen 5 | ✅ Funcional (MOL_039) |
| **Damas Online** | 0x02 | 2 | RELAY | Screen 4 | ✅ Funcional (DAM_022) |
| **Burdyn RPG** | 0x03 | 14 | AGGREGATE | Screen 4 | ✅ Funcional (BURD_024) |
| **Parchis** | 0x04 | 4 | RELAY | Screen 4 | ✅ Funcional (PAR_011) |
| **Texas Hold'em** | 0x05 | 6 | RELAY | Screen 4 | 🚧 En desarrollo (TEX_015) |

### Ball Demo (`client/`)
Demo de sprites multijugador. Cada jugador mueve una bola por la pantalla en Screen 5 (bitmap). Hasta 4 jugadores por sala.

### Damas Online (`damas/`)
Juego de damas para 2 jugadores. Tablero cenital con fichas como sprites 16x16. Incluye capturas, multi-capturas y coronación (damas/kings). P1=blancas, P2=negras.

### Burdyn RPG Crawler (`burdyn/`)
RPG crawler multijugador para hasta 14 jugadores. Mapa 64x64 con scroll centrado, 83 tiles, HUD sidebar, inventario de 8 items. Incluye editor de mapas HTML.

### Parchis Online (`parchis/`)
Parchis clasico para 4 jugadores. Tablero con recorrido de 68 casillas, pasillos, casillas seguras, capturas, barreras. P1=Amarillo, P2=Azul, P3=Rojo, P4=Verde. Host arranca la partida.

### Texas Hold'em Poker (`texas/`)
Poker Texas Hold'em para hasta 6 jugadores. Cartas simplificadas con tiles reutilizables (valor+palo). Stack fijo 1000 fichas, ciegas 10/20. Dealer local con IA para testing offline. Evaluador de manos en Z80. Incluye editor de tiles y editor de layout HTML.

---

## Estructura del repo

```
MSXonLINE/
├── server/              Servidor Node.js (relay + aggregate)
│   ├── msx-gameserver.js    Servidor principal
│   ├── server-status.js     Monitor + ghost players
│   └── update.sh            Deploy al VPS
├── shared/              Codigo compartido entre juegos
│   ├── network.h            Capa UNAPI TCP
│   ├── protocol.h           Protocolo binario
│   └── log.h                Logging MSX-DOS 2
├── client/              Ball Demo (GAME_ID=0x01)
├── damas/               Damas Online (GAME_ID=0x02)
│   └── damas.c
├── burdyn/              Burdyn RPG (GAME_ID=0x03)
│   ├── burdyn.c
│   ├── editor.html          Editor de mapas
│   └── assets/              Mapas (.bin)
├── parchis/             Parchis Online (GAME_ID=0x04)
│   ├── path_editor.html     Editor visual del tablero
│   ├── tileset.png          Tileset grafico
│   └── screen_layout.json   Layout 32x24
├── texas/               Texas Hold'em (GAME_ID=0x05)
│   ├── tile_editor.html     Editor de tiles
│   ├── table_editor.html    Editor de layout de mesa
│   └── assets/              Tileset, layout JSON
├── tools/               Herramientas
│   └── msxonline-cli/       TUI en Rust para gestionar el servidor
├── build/
│   └── serve.js             Servidor HTTP para descargar builds desde MSX
├── PROTOCOL.md          Especificacion del protocolo
├── COMMANDS.md          Referencia de comandos SSH/deploy
└── CLAUDE.md            Contexto del proyecto
```

---

## Servidor

**IP**: 217.154.107.144:9876

Dos modos de sala:
- **RELAY**: reenvía STATE_UPDATE a todos (Ball Demo, Damas, Parchis, Texas)
- **AGGREGATE**: almacena estados, envía WORLD_STATE a 10Hz (Burdyn)

```bash
# Logs
ssh root@217.154.107.144 "journalctl -u msx-server -f"

# Reiniciar
ssh root@217.154.107.144 "systemctl restart msx-server"

# Desplegar
cd MSXonLINE && sed -i 's/\r$//' server/update.sh && \
scp server/msx-gameserver.js server/update.sh root@217.154.107.144:/tmp/ && \
ssh root@217.154.107.144 "bash /tmp/update.sh"
```

---

## Compilar

Requiere [MSXgl](https://github.com/aoineko-fr/MSXgl) y SDCC 4.5.0.

```bash
cd MSXgl/projects/msxonline && bash build.sh    # Ball Demo
cd MSXgl/projects/damas && bash build.sh        # Damas
cd MSXgl/projects/burdyn && bash build.sh       # Burdyn
cd MSXgl/projects/parchis && bash build.sh      # Parchis
cd MSXgl/projects/texas && bash build.sh        # Texas Hold'em
```

**IMPORTANTE**: `ForceRamAddr = 0x8000` en `project_config.js` es obligatorio para evitar conflictos UNAPI.

---

## Hardware soportado

- **MSX2** con MSX-DOS 2
- **Red**: ESP-01 WiFi (UNAPI ducasp), ObsoNET, GR8NET
- Funciona offline si no hay UNAPI

---

## Herramientas

### Server Status (`server/server-status.js`)
Monitor interactivo del servidor. Ver salas, crear salas, ghost players para testing.

```bash
node server/server-status.js
```

### MSX Online CLI (`tools/msxonline-cli/`)
TUI en Rust con paneles, colores y auto-refresh.

```bash
cargo build --release
./target/release/msxonline-cli
```

### Build Server (`build/serve.js`)
Servidor HTTP para descargar builds desde MSX con hget.

```bash
cd build && node serve.js
# Desde MSX: hget http://IP_PC:8080/bin/damas.com
```

---

## Protocolo

Ver [PROTOCOL.md](PROTOCOL.md) para la especificacion completa.

```
[0x46][0x4D][CMD][ROOM][PID][LEN][...payload...]
  'F'   'M'
```

---

*Proyecto de Antxiko*
