# Tetris 4P — MSX Online

Tetris competitivo a 4 jugadores para MSX2 online. GAME_ID = 0x06.

## Estado

- **Build actual**: TET_021
- **Modo**: RELAY con full board sync
- **Online**: funcionando con ghosts IA

## Caracteristicas

- Screen 4 (Graphic 3), 32x24 tiles
- 4 tableros de 8 columnas, fondo de color por jugador
- Shadow buffer: solo redibuja celdas que cambian
- 7 tetrominoes con rotacion y wall kick
- Garbage: 2 lineas=1, 3=2, tetris=4 filas
- Targeting con SPACE (elige a quien atacar)
- Sprite flecha sobre el target
- Deteccion de ganador, nueva partida con S

## Protocolo de red

Full board sync simplificado (como Burdyn):
- Cada 5 frames: envia tablero completo empaquetado (86 bytes)
- Board packed: 2 celdas por byte = 80 bytes
- Garbage: paquete separado (targetSlot, count, gapCol) = 4 bytes
- Sin deltas, sin race conditions, receptor sobreescribe

## Compilar

```bash
cd MSXgl/projects/tetris && bash build.sh
```

## Controles

- Flechas: mover pieza
- Arriba: rotar
- Abajo: soft drop
- Espacio: cambiar target
- ESC: salir

## Ficheros

| Fichero | Descripcion |
|---------|-------------|
| `tetris.c` | Fuente principal (juego + red + render) |
| `project_config.js` | ForceRamAddr=0x8000, DOS2 |
| `msxgl_config.h` | Screen 4, VDP_INIT_50HZ=OFF |

---

*Proyecto de Antxiko*
