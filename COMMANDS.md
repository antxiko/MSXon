# MSXon — Comandos de referencia

## Servidor VPS (<VPS_IP>)

```bash
# Ver logs en tiempo real
ssh root@<VPS_IP> "journalctl -u msx-server -f"

# Reiniciar servidor (limpia todas las salas)
ssh root@<VPS_IP> "systemctl restart msx-server"

# Estado del servicio
ssh root@<VPS_IP> "systemctl status msx-server"

# Desplegar nueva versión
cd ~/Documents/MSXonLIVE/MSXon && sed -i 's/\r$//' server/update.sh && scp server/msx-gameserver.js server/update.sh root@<VPS_IP>:/tmp/ && ssh root@<VPS_IP> "bash /tmp/update.sh"
```

## Server Status (monitor local)

```bash
cd ~/Documents/MSXonLIVE/MSXon && node server/server-status.js
```

Opciones: 1=salas, 2=estado, 3=crear sala, 4=ping, 5=reconectar, 6=ghost player, 7=stop ghost

## openMSX (emulador)

```bash
# Ball Demo
openmsx -machine Philips_NMS_8250 -ext msxdos2 -diska build/dsk/BURDYN.dsk

# Burdyn
openmsx -machine Philips_NMS_8250 -ext msxdos2 -diska build/dsk/BURDYN.dsk
```

## Ghost Service (VPS)

```bash
# Desplegar ghost-service (modular v2.0)
cd server && scp ghost-base.js ghost-room-registry.js ghost-damas.js ghost-burdyn.js ghost-tetris.js ghost-poker.js ghost-parchis.js ghost-service.js msx-ghost.service root@<VPS_IP>:/tmp/
ssh root@<VPS_IP> "cp /tmp/ghost-*.js /opt/msx-server/ && cp /tmp/msx-ghost.service /etc/systemd/system/ && systemctl daemon-reload && systemctl restart msx-ghost"

# Estado
ssh root@<VPS_IP> "systemctl status msx-ghost"

# Reiniciar
ssh root@<VPS_IP> "systemctl restart msx-ghost"

# Logs
ssh root@<VPS_IP> "journalctl -u msx-ghost -f"
```

## Auth Backend (VPS) — Fase 2

```bash
# Desplegar auth backend (msx-gameserver.js, auth-store.js, msx-web.js)
cd server && \
ssh root@<VPS_IP> "cp /opt/msx-server/msx-gameserver.js /opt/msx-server/msx-gameserver.js.bak.\$(date +%Y%m%d-%H%M%S)" && \
scp auth-store.js msx-web.js msx-gameserver.js root@<VPS_IP>:/opt/msx-server/ && \
ssh root@<VPS_IP> "systemctl restart msx-server && sleep 2 && journalctl -u msx-server -n 15 --no-pager"

# Crear archivo .superadmin (una sola vez, define el superadmin inicial)
ssh root@<VPS_IP> 'echo "<your_username>" > /opt/msx-server/.superadmin && chown nobody:nogroup /opt/msx-server/.superadmin && chmod 644 /opt/msx-server/.superadmin && systemctl restart msx-server'

# Permisos del directorio (necesario para que nobody escriba users.json)
ssh root@<VPS_IP> 'chgrp nogroup /opt/msx-server && chmod 775 /opt/msx-server'

# Ver usuarios registrados
ssh root@<VPS_IP> "cat /opt/msx-server/users.json"

# Ver que tu cuenta tiene role superadmin
ssh root@<VPS_IP> "cat /opt/msx-server/users.json | grep -A1 -E '\"username\"|\"role\"'"

# Backup de users.json antes de cambios criticos
ssh root@<VPS_IP> "cp /opt/msx-server/users.json /opt/msx-server/users.json.bak.\$(date +%Y%m%d-%H%M%S)"
```

## Caddy + dominio (VPS)

```bash
# Recargar config
ssh root@<VPS_IP> "systemctl reload caddy"

# Ver logs de TLS (renovacion certs, ACME)
ssh root@<VPS_IP> "journalctl -u caddy -n 30 --no-pager | grep -E 'certificate|obtained|error'"

# Verificar HTTPS funcionando
curl -v https://msxon.nosignalbbs.com/ 2>&1 | head -20
```

DNS: `msxon.nosignalbbs.com` (registro A en piensasolutions) → `<VPS_IP>`.
Caddy reverse_proxy → `127.0.0.1:8080` (Node HTTP en mismo proceso que TCP).

## Test E2E auth desde PC

```bash
# Flujo completo: REGISTER → URL para movil → LOGIN
node tools/test-register.js [username] [nick]
# Defaults: username=myuser, nick=MyUser
```

## Git

```bash
# Push a v2-redesign
cd ~/Documents/MSXonLIVE/MSXon && git add -A && git commit -m "mensaje" && git push origin v2-redesign

# Volver a versión estable
git checkout main
```
