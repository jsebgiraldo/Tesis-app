#!/bin/bash
# Monitor específico para el Sistema de Monitoreo (Alertas, Errores, Recuperación)

echo "╔═══════════════════════════════════════════════════════════════════╗"
echo "║     🚨 MONITOR: Sistema de Alertas y Recuperación                ║"
echo "╚═══════════════════════════════════════════════════════════════════╝"
echo ""

# Ver logs relacionados con alertas, errores y recuperación
echo "🔄 Monitoreando alertas, errores HDLC, recuperaciones y buffer..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

sudo journalctl -u dlms-multi-meter.service -f --since "5 minutes ago" | \
    grep --line-buffered -E "ALERTA|ALERT|WARNING|ERROR|error|Error|HDLC|hdlc|Recuper|Recovery|🧹|Buffer|buffer|Reconect|Reconnect|Failed|failed"
