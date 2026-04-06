# Texas Hold'em Poker — MSX Online

Poker Texas Hold'em para MSX2 con hasta 6 jugadores. GAME_ID = 0x05.

## Estado

- **Build actual**: TEX_020
- **Modo**: Cliente puro — servidor es el dealer
- **Ghost**: Bot de poker en servidor para jugar solo

## Caracteristicas

- Screen 4 (Graphic 3), 32x24 tiles
- Cartas simplificadas 2x3 tiles (valor + palo reutilizables)
- Stack fijo: 1000 fichas, ciegas 10/20
- 4 rondas de apuestas: pre-flop, flop, turn, river
- Servidor evalua manos (game-handlers/poker-handler.js)
- Cartas privadas enviadas individualmente a cada jugador
- Reglas: fold, check, call, raise, all-in
- Ghost bot con IA simple en el servidor
- 30 segundos timeout por accion (auto-fold)

## Codificacion de cartas

Cada carta = 1 byte:
- Bits 0-3: valor (1=A, 2-10, 11=J, 12=Q, 13=K)
- Bits 4-5: palo (0=picas, 1=corazones, 2=diamantes, 3=treboles)
- 0x00 = sin carta

## Tiles

| Rango | Uso |
|-------|-----|
| 0 | Negro (fondo) |
| 1-4 | Marco carta abierta (TL, TR, BL, BR) |
| 5-8 | Carta boca abajo (TL, TR, BL, BR) |
| 9-12 | Palos: picas, corazones, diamantes, treboles |
| 13-25 | Valores negros: A,2,3,4,5,6,7,8,9,10,J,Q,K |
| 26-38 | Valores rojos: A,2,3,4,5,6,7,8,9,10,J,Q,K |
| 39-47 | Mesa: felt, bordes, esquinas |
| 48 | Boton dealer |
| 49 | Separador |
| 50-51 | Cursor > < |
| 52-77 | Font A-Z |
| 78-87 | Font 0-9 |
| 88-93 | Puntuacion |
| 94-95 | Carta boca abajo medio (ML, MR) |

## Herramientas

- **tile_editor.html** — Editor de tiles pixel a pixel con paleta MSX
- **table_editor.html** — Editor visual del layout de la mesa (posiciones de jugadores, cartas, HUD)
- **convert_tileset.js** — Convierte tileset PNG a tileset_data.h

## Compilar

```bash
cd MSXgl/projects/texas && bash build.sh
```

## Ficheros

| Fichero | Descripcion |
|---------|-------------|
| `texas.c` | Fuente principal (dealer local + UI) |
| `tileset_data.h` | Tiles auto-generados desde PNG |
| `screen_data.h` | Layout de la mesa 32x24 |
| `project_config.js` | ForceRamAddr=0x8000, DOS2 |
| `msxgl_config.h` | Screen 4, VDP_INIT_50HZ=OFF |

---

*Proyecto de Antxiko*
