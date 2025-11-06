#!/bin/bash
# Monitor específico para el componente MeterWorker (Lectura DLMS)

echo "╔═══════════════════════════════════════════════════════════════════╗"
echo "║          ⚡ MONITOR: MeterWorker (Lectura DLMS)                   ║"
echo "╚═══════════════════════════════════════════════════════════════════╝"
echo ""

# Ver logs relacionados con lecturas DLMS en tiempo real
echo "🔄 Monitoreando lecturas DLMS, latencias y conexiones..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

sudo journalctl -u dlms-multi-meter.service -f --since "5 minutes ago" | \
    grep --line-buffered -E "| V:|DLMS|dlms|Conexión|Connected|Optimizado|OPTIMIZED|Caché|cache|Latencia|latency|\(.*s\)"
