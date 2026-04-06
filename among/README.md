# Among MSX — Impostor Online

Among Us simplificado para MSX2 online. GAME_ID = 0x07.

## Estado

- **Build actual**: AMG_001
- **Fase**: Movimiento + habitaciones (offline)

## Concepto

- 4-8 jugadores, 1 impostor elegido al azar
- 7 habitaciones separadas (pantallas completas 32x24, sin scroll)
- Puertas para cambiar de habitacion
- Interruptores: impostor sabotea, inocentes arreglan
- Solo ves jugadores en tu misma habitacion
- Votacion cuando se reporta un cadaver

## Habitaciones

```
REACTOR -- PASILLO -- CAFETERIA
               |
          ELECTRICA -- MOTOR
               |
          MEDBAY -- ESCOTILLA
```

## Controles

- Cursores: mover
- Espacio: interactuar (sabotear/arreglar)
- Enter: reportar cadaver / matar (impostor)
- ESC: salir

## Compilar

```bash
cd MSXgl/projects/among && bash build.sh
```

---

*Proyecto de Antxiko*
