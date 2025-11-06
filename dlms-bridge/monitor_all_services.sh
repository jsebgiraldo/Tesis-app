#!/bin/bash
# Monitor de todos los servicios del sistema DLMS

clear
echo "╔═══════════════════════════════════════════════════════════════════╗"
echo "║          🔍 MONITOR GENERAL - SERVICIOS DLMS                      ║"
echo "╚═══════════════════════════════════════════════════════════════════╝"
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "📊 SERVICIOS ACTIVOS"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
systemctl list-units --type=service --state=running | grep -E "dlms|tb-gateway|mqtt|qos" || echo "No se encontraron servicios activos"
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🔧 1. DLMS Multi-Meter (Servicio Principal)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
sudo systemctl status dlms-multi-meter.service --no-pager -n 5 2>/dev/null || echo "❌ Servicio no encontrado o no está corriendo"
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "📡 2. DLMS to Mosquitto Bridge (DESHABILITADO)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if systemctl is-enabled --quiet dlms-mosquitto-bridge.service 2>/dev/null; then
    echo "⚠️  Servicio habilitado - debería estar deshabilitado para evitar conflictos"
    sudo systemctl status dlms-mosquitto-bridge.service --no-pager -n 3 2>/dev/null
else
    echo "✅ Servicio correctamente deshabilitado (evita conflictos MQTT)"
fi
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🌉 3. ThingsBoard Gateway (DESHABILITADO)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if systemctl is-enabled --quiet tb-gateway-dlms.service 2>/dev/null; then
    echo "⚠️  Servicio habilitado - debería estar deshabilitado para evitar conflictos"
    sudo systemctl status tb-gateway-dlms.service --no-pager -n 3 2>/dev/null
else
    echo "✅ Servicio correctamente deshabilitado (evita conflictos MQTT)"
fi
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "⚡ 4. QoS Supervisor (OPCIONAL)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
sudo systemctl status qos-supervisor.service --no-pager -n 5 2>/dev/null || echo "⏹️  Servicio detenido (opcional)"
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🔍 COMANDOS DISPONIBLES"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "Ver logs en tiempo real por servicio:"
echo "  1. DLMS Multi-Meter:  sudo journalctl -u dlms-multi-meter.service -f"
echo "  2. Ver solo lecturas:  sudo journalctl -u dlms-multi-meter.service -f | grep '| V:'"
echo "  3. MeterWorker:        ./monitor_meter_worker.sh"
echo ""
echo "Reiniciar servicio principal:"
echo "  - sudo systemctl restart dlms-multi-meter.service"
echo ""
echo "Recuperación post-apagón:"
echo "  - ./recover_from_power_loss.sh"
echo ""
