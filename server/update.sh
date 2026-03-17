#!/bin/bash
# =============================================================================
# update.sh — Actualiza msx-gameserver.js en el VPS
#
# Uso (ejecutar desde la carpeta server/):
#
#   VPS=root@TU_IP && scp msx-gameserver.js update.sh $VPS:/tmp/ && ssh $VPS 'bash /tmp/update.sh'
#
# =============================================================================
set -euo pipefail

INSTALL_DIR=/opt/msx-server
SERVICE_NAME=msx-server

echo "══════════════════════════════════════════════════"
echo "  MSX Game Server — Actualización"
echo "══════════════════════════════════════════════════"

# ── 1. Verificar que el servicio existe ───────────────────────────
echo ""
echo ">> Verificando servicio $SERVICE_NAME..."
if ! systemctl cat "$SERVICE_NAME" &>/dev/null; then
    echo "   ❌ Servicio no encontrado. Ejecuta deploy.sh primero."
    exit 1
fi
echo "   Servicio encontrado OK"

# ── 2. Copiar nuevo archivo ──────────────────────────────────────
echo ""
echo ">> Actualizando $INSTALL_DIR/msx-gameserver.js..."
cp /tmp/msx-gameserver.js "$INSTALL_DIR/msx-gameserver.js"
echo "   Archivo copiado"

# ── 3. Reiniciar servicio ────────────────────────────────────────
echo ""
echo ">> Reiniciando $SERVICE_NAME..."
systemctl restart "$SERVICE_NAME"
sleep 1

if systemctl is-active --quiet "$SERVICE_NAME"; then
    echo "   ✅ Servicio activo"
else
    echo "   ❌ El servicio no arrancó. Revisa con: journalctl -u $SERVICE_NAME -n 20"
    exit 1
fi

# ── 4. Mostrar estado ───────────────────────────────────────────
echo ""
systemctl status "$SERVICE_NAME" --no-pager -l
echo ""
echo "══════════════════════════════════════════════════"
echo "  ✅ ACTUALIZACIÓN COMPLETADA"
echo ""
echo "  Logs:  journalctl -u $SERVICE_NAME -f"
echo "══════════════════════════════════════════════════"
