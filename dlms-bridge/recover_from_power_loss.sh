#!/bin/bash
#
# Script de Recuperación Post-Apagón
# Asegura que todos los servicios arranquen correctamente después de un apagón
#

echo "╔═══════════════════════════════════════════════════════════════════╗"
echo "║       🔧 RECUPERACIÓN POST-APAGÓN - Sistema DLMS               ║"
echo "╚═══════════════════════════════════════════════════════════════════╝"
echo ""

VENV_PATH="/home/pci/Documents/sebas_giraldo/Tesis-app/dlms-bridge/venv"
WORK_DIR="/home/pci/Documents/sebas_giraldo/Tesis-app/dlms-bridge"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Verificar que estamos en el directorio correcto
cd "$WORK_DIR" || { echo "❌ No se puede acceder a $WORK_DIR"; exit 1; }

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🔍 Paso 1: Verificando conectividad del medidor DLMS"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if ping -c 2 -W 3 192.168.1.127 > /dev/null 2>&1; then
    echo -e "${GREEN}✅ Medidor DLMS accesible (192.168.1.127)${NC}"
else
    echo -e "${RED}❌ Medidor DLMS no responde. Verifique la red.${NC}"
    echo "   Ejecute: ping 192.168.1.127"
    exit 1
fi
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🛑 Paso 2: Deteniendo servicios conflictivos"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Detener servicios que pueden causar conflictos MQTT
SERVICES_TO_STOP=("dlms-mosquitto-bridge.service" "tb-gateway-dlms.service" "dlms-admin-api.service")

for service in "${SERVICES_TO_STOP[@]}"; do
    if systemctl is-active --quiet "$service"; then
        echo "  Deteniendo $service..."
        sudo systemctl stop "$service"
        echo -e "  ${YELLOW}⏹️  $service detenido${NC}"
    else
        echo "  $service ya está detenido"
    fi
done
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🔧 Paso 3: Verificando módulos Python necesarios"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Verificar módulos críticos
CRITICAL_MODULES=("dlms_client_robust.py" "dlms_optimized_reader.py" "dlms_reader.py" "dlms_poller_production.py")
ALL_MODULES_OK=true

for module in "${CRITICAL_MODULES[@]}"; do
    if [ -f "$module" ]; then
        echo -e "  ${GREEN}✓${NC} $module existe"
    else
        echo -e "  ${RED}✗${NC} $module NO ENCONTRADO"
        ALL_MODULES_OK=false
    fi
done

if [ "$ALL_MODULES_OK" = false ]; then
    echo -e "${RED}❌ Faltan módulos críticos. Sistema no puede arrancar.${NC}"
    exit 1
fi
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🔄 Paso 4: Reiniciando servicio principal (dlms-multi-meter)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

sudo systemctl restart dlms-multi-meter.service
sleep 5

if systemctl is-active --quiet dlms-multi-meter.service; then
    echo -e "${GREEN}✅ dlms-multi-meter.service está corriendo${NC}"
else
    echo -e "${RED}❌ dlms-multi-meter.service falló al iniciar${NC}"
    echo ""
    echo "Logs del servicio:"
    sudo journalctl -u dlms-multi-meter.service --since "1 minute ago" --no-pager -n 20
    exit 1
fi
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🔍 Paso 5: Verificando conflictos MQTT"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

sleep 10  # Esperar un poco para que el servicio intente conectar

# Buscar errores MQTT código 7 en los últimos segundos
MQTT_ERRORS=$(sudo journalctl -u dlms-multi-meter.service --since "15 seconds ago" --no-pager | grep -c "code 7" || true)

if [ "$MQTT_ERRORS" -gt 5 ]; then
    echo -e "${YELLOW}⚠️  Detectados $MQTT_ERRORS desconexiones MQTT (código 7)${NC}"
    echo "   Esto indica conflicto de tokens MQTT."
    echo ""
    echo "   Verificando procesos MQTT activos:"
    ps aux | grep -E "(mqtt|dlms)" | grep -v grep | grep -v "$(basename $0)"
    echo ""
    echo -e "${YELLOW}   Considere verificar manualmente qué está usando el token MQTT.${NC}"
else
    echo -e "${GREEN}✅ Sin conflictos MQTT detectados${NC}"
fi
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "📊 Paso 6: Estado de servicios"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

ALL_SERVICES=("dlms-multi-meter.service" "dlms-mosquitto-bridge.service" "tb-gateway-dlms.service" "qos-supervisor.service")

for service in "${ALL_SERVICES[@]}"; do
    if systemctl is-active --quiet "$service"; then
        echo -e "  ${GREEN}✓ ACTIVO${NC}   - $service"
    elif systemctl is-enabled --quiet "$service" 2>/dev/null; then
        echo -e "  ${YELLOW}○ INACTIVO${NC} - $service (habilitado para auto-start)"
    else
        echo -e "  ${RED}✗ DETENIDO${NC} - $service"
    fi
done
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "✅ Paso 7: Verificando auto-start en boot"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Verificar que el servicio principal esté habilitado
if systemctl is-enabled --quiet dlms-multi-meter.service; then
    echo -e "${GREEN}✅ dlms-multi-meter.service habilitado para auto-start${NC}"
else
    echo -e "${YELLOW}⚠️  dlms-multi-meter.service NO está habilitado para auto-start${NC}"
    echo "   Ejecutando: sudo systemctl enable dlms-multi-meter.service"
    sudo systemctl enable dlms-multi-meter.service
fi
echo ""

echo "╔═══════════════════════════════════════════════════════════════════╗"
echo "║                   ✅ RECUPERACIÓN COMPLETADA                     ║"
echo "╚═══════════════════════════════════════════════════════════════════╝"
echo ""
echo "📊 RESUMEN:"
echo "  • Medidor DLMS: Accesible"
echo "  • Módulos Python: OK"
echo "  • Servicio principal: Corriendo"
echo "  • Auto-start: Configurado"
echo ""
echo "📝 Para monitorear en vivo:"
echo "  sudo journalctl -u dlms-multi-meter.service -f"
echo ""
echo "🔍 Para verificar salud completa:"
echo "  ./monitor_all_services.sh"
echo ""
