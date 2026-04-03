# MSX Online — Comandos de referencia

## Servidor VPS (217.154.107.144)

```bash
# Ver logs en tiempo real
ssh root@217.154.107.144 "journalctl -u msx-server -f"

# Reiniciar servidor (limpia todas las salas)
ssh root@217.154.107.144 "systemctl restart msx-server"

# Estado del servicio
ssh root@217.154.107.144 "systemctl status msx-server"

# Desplegar nueva versión
cd ~/Documents/MSXonLIVE/MSXonLINE && sed -i 's/\r$//' server/update.sh && scp server/msx-gameserver.js server/update.sh root@217.154.107.144:/tmp/ && ssh root@217.154.107.144 "bash /tmp/update.sh"
```

## Server Status (monitor local)

```bash
cd ~/Documents/MSXonLIVE/MSXonLINE && node server/server-status.js
```

Opciones: 1=salas, 2=estado, 3=crear sala, 4=ping, 5=reconectar, 6=ghost player, 7=stop ghost

## openMSX (emulador)

```bash
# Ball Demo
openmsx -machine Philips_NMS_8250 -ext msxdos2 -diska build/dsk/BURDYN.dsk

# Burdyn
openmsx -machine Philips_NMS_8250 -ext msxdos2 -diska build/dsk/BURDYN.dsk
```

## Git

```bash
# Push a v2-redesign
cd ~/Documents/MSXonLIVE/MSXonLINE && git add -A && git commit -m "mensaje" && git push origin v2-redesign

# Volver a versión estable
git checkout main
```
