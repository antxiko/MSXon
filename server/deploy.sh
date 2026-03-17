#!/bin/bash
# =============================================================================
# deploy.sh — Despliegue de MSX Game Server en VPS Ubuntu
#
# Uso:
#   1. Copia este script al VPS:  scp deploy.sh root@TU_IP:/tmp/
#   2. Copia el servidor:         scp msx-gameserver.js root@TU_IP:/tmp/
#   3. Ejecuta en el VPS:         ssh root@TU_IP 'bash /tmp/deploy.sh'
#
#   O todo de golpe desde tu Mac (ejecutar desde la carpeta server/):
#
#   VPS=root@TU_IP && scp msx-gameserver.js deploy.sh $VPS:/tmp/ && ssh $VPS 'bash /tmp/deploy.sh'
#
# =============================================================================
set -euo pipefail

PORT=9876
INSTALL_DIR=/opt/msx-server
SERVICE_NAME=msx-server

echo "══════════════════════════════════════════════════"
echo "  MSX Game Server — Despliegue automático"
echo "══════════════════════════════════════════════════"

# ── 1. Comprobar Node.js ─────────────────────────────────────────
echo ""
echo ">> Comprobando Node.js..."
if ! command -v node &>/dev/null; then
    echo "   Node.js no encontrado. Instalando Node.js 20 LTS..."
    curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
    apt-get install -y nodejs
fi
NODE_VER=$(node -v)
echo "   Node.js $NODE_VER OK"

# ── 2. Comprobar que el puerto está libre ────────────────────────
echo ""
echo ">> Comprobando puerto $PORT..."
if ss -tlnp | grep -q ":${PORT} "; then
    echo "   ⚠️  PUERTO $PORT YA EN USO:"
    ss -tlnp | grep ":${PORT} "
    echo "   Abortando. Libera el puerto o cambia PORT en msx-gameserver.js"
    exit 1
fi
echo "   Puerto $PORT libre OK"

# ── 3. Instalar archivos ────────────────────────────────────────
echo ""
echo ">> Instalando en $INSTALL_DIR..."
mkdir -p "$INSTALL_DIR"
cp /tmp/msx-gameserver.js "$INSTALL_DIR/"
echo "   msx-gameserver.js copiado"

# ── 4. Crear servicio systemd ───────────────────────────────────
echo ""
echo ">> Creando servicio systemd: $SERVICE_NAME..."
cat > /etc/systemd/system/${SERVICE_NAME}.service <<EOF
[Unit]
Description=MSX Online Game Server
After=network.target

[Service]
Type=simple
User=nobody
WorkingDirectory=$INSTALL_DIR
ExecStart=/usr/bin/node msx-gameserver.js
Restart=always
RestartSec=5
MemoryMax=64M
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
echo "   Servicio creado"

# ── 5. Arrancar el servicio ─────────────────────────────────────
echo ""
echo ">> Arrancando $SERVICE_NAME..."
systemctl enable "$SERVICE_NAME"
systemctl restart "$SERVICE_NAME"
sleep 1

if systemctl is-active --quiet "$SERVICE_NAME"; then
    echo "   ✅ Servicio activo"
else
    echo "   ❌ El servicio no arrancó. Revisa con: journalctl -u $SERVICE_NAME -n 20"
    exit 1
fi

# ── 6. Firewall ─────────────────────────────────────────────────
echo ""
echo ">> Configurando firewall..."
if command -v ufw &>/dev/null; then
    ufw allow ${PORT}/tcp comment "MSX Game Server" 2>/dev/null || true
    echo "   ufw: puerto $PORT/tcp permitido"
elif command -v firewall-cmd &>/dev/null; then
    firewall-cmd --permanent --add-port=${PORT}/tcp 2>/dev/null || true
    firewall-cmd --reload 2>/dev/null || true
    echo "   firewalld: puerto $PORT/tcp permitido"
else
    echo "   No se detectó ufw ni firewalld. Asegúrate de abrir el puerto $PORT/tcp manualmente."
fi

# ── 7. Verificación final ───────────────────────────────────────
echo ""
echo "══════════════════════════════════════════════════"
echo "  ✅ DESPLIEGUE COMPLETADO"
echo ""
echo "  Servicio:  systemctl status $SERVICE_NAME"
echo "  Logs:      journalctl -u $SERVICE_NAME -f"
echo "  Puerto:    $PORT/tcp"
echo ""
echo "  Comandos útiles:"
echo "    systemctl restart $SERVICE_NAME"
echo "    systemctl stop $SERVICE_NAME"
echo "    journalctl -u $SERVICE_NAME -n 50"
echo "══════════════════════════════════════════════════"
