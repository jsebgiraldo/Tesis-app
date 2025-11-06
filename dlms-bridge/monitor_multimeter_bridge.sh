#!/bin/bash
# Monitor específico para el componente MultiMeterBridge (MQTT Manager)

echo "╔═══════════════════════════════════════════════════════════════════╗"
echo "║         📡 MONITOR: MultiMeterBridge (MQTT Manager)              ║"
echo "╚═══════════════════════════════════════════════════════════════════╝"
echo ""

# Ver logs relacionados con MQTT en tiempo real
echo "🔄 Monitoreando conexiones MQTT, publicaciones y Client ID..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

sudo journalctl -u dlms-multi-meter.service -f --since "5 minutes ago" | \
    grep --line-buffered -E "MQTT|mqtt|Client ID|client_id|Published|publish|Connected|Token|rc=|code="
