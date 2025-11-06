#!/bin/bash
# Monitor en tiempo real del sistema DLMS con watchdog
# Muestra logs, estadísticas y alertas en tiempo real

echo "╔════════════════════════════════════════════════════════════════╗"
echo "║     MONITOR EN TIEMPO REAL - DLMS Bridge con Watchdog         ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""

# Función para mostrar estadísticas
show_stats() {
    echo "📊 ESTADÍSTICAS (Última hora)"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    # Contar mensajes publicados
    MQTT_COUNT=$(journalctl -u dlms-multi-meter.service --since "1 hour ago" --no-pager | grep -c "📤 Published")
    
    # Contar errores HDLC
    HDLC_ERRORS=$(journalctl -u dlms-multi-meter.service --since "1 hour ago" --no-pager | grep -ciE "hdlc|frame boundary|unterminated")
    
    # Contar intervenciones del watchdog
    WATCHDOG_COUNT=$(journalctl -u dlms-multi-meter.service --since "1 hour ago" --no-pager | grep -c "🚨 WATCHDOG")
    
    # Contar reconexiones
    RECONNECT_COUNT=$(journalctl -u dlms-multi-meter.service --since "1 hour ago" --no-pager | grep -c "♻️.*Reiniciando conexión")
    
    echo "  📤 Mensajes MQTT:           $MQTT_COUNT"
    echo "  🔴 Errores HDLC:            $HDLC_ERRORS"
    echo "  🐕 Intervenciones Watchdog: $WATCHDOG_COUNT"
    echo "  ♻️  Reconexiones:            $RECONNECT_COUNT"
    echo ""
}

# Función para mostrar estado actual de BD
show_db_stats() {
    echo "💾 BASE DE DATOS"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    cd /home/pci/Documents/sebas_giraldo/Tesis-app/dlms-bridge
    
    python3 - << 'PY'
import sqlite3
from datetime import datetime, timedelta

db = 'data/admin.db'
con = sqlite3.connect(db)
cur = con.cursor()

# Diagnósticos totales
total = cur.execute("SELECT COUNT(*) FROM dlms_diagnostics").fetchone()[0]
print(f"  📋 Total diagnósticos:      {total}")

# Diagnósticos última hora
hour_ago = (datetime.utcnow() - timedelta(hours=1)).isoformat()
last_hour = cur.execute(f"SELECT COUNT(*) FROM dlms_diagnostics WHERE timestamp >= '{hour_ago}'").fetchone()[0]
print(f"  🕐 Última hora:             {last_hour}")

# Alarmas críticas
critical = cur.execute("SELECT COUNT(*) FROM alarms WHERE severity='critical' AND acknowledged=0").fetchone()[0]
print(f"  🚨 Alarmas críticas:        {critical}")

# Alarmas watchdog
watchdog = cur.execute("SELECT COUNT(*) FROM alarms WHERE category='watchdog' AND acknowledged=0").fetchone()[0]
print(f"  🐕 Alarmas watchdog:        {watchdog}")

con.close()
PY
    echo ""
}

# Mostrar estadísticas iniciales
show_stats
show_db_stats

echo "📡 LOGS EN TIEMPO REAL (Ctrl+C para salir)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# Seguir logs con colores
journalctl -u dlms-multi-meter.service -f --no-pager | while read line; do
    # Colorear según tipo de mensaje
    if echo "$line" | grep -q "📤 Published"; then
        echo -e "\e[32m$line\e[0m"  # Verde para publicaciones exitosas
    elif echo "$line" | grep -q "🚨 WATCHDOG"; then
        echo -e "\e[1;31m$line\e[0m"  # Rojo brillante para watchdog
    elif echo "$line" | grep -qiE "error|hdlc|fail"; then
        echo -e "\e[31m$line\e[0m"  # Rojo para errores
    elif echo "$line" | grep -q "♻️"; then
        echo -e "\e[33m$line\e[0m"  # Amarillo para reconexiones
    elif echo "$line" | grep -q "✅"; then
        echo -e "\e[36m$line\e[0m"  # Cyan para éxitos
    else
        echo "$line"
    fi
done
