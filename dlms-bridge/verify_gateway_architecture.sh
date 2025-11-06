#!/bin/bash
#
# Script de Verificación: Arquitectura Gateway ThingsBoard
# Verifica que el sistema esté funcionando correctamente sin "code 7"
#

echo "🔍 ============================================"
echo "🔍 VERIFICACIÓN: ARQUITECTURA GATEWAY"
echo "🔍 ============================================"
echo ""

# Colores
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Contador de problemas
ISSUES=0

# 1. Verificar servicios activos
echo "📊 1. Estado de Servicios"
echo "   ========================"

services=("dlms-multi-meter" "mosquitto" "thingsboard-gateway")
for service in "${services[@]}"; do
    if systemctl is-active --quiet "$service.service"; then
        echo -e "   ${GREEN}✓${NC} $service: ACTIVO"
    else
        echo -e "   ${RED}✗${NC} $service: INACTIVO"
        ((ISSUES++))
    fi
done
echo ""

# 2. Verificar puertos MQTT
echo "📊 2. Puertos MQTT"
echo "   ==============="

if sudo netstat -tuln | grep -q ":1883.*LISTEN"; then
    echo -e "   ${GREEN}✓${NC} Puerto 1883 (ThingsBoard): ESCUCHANDO"
else
    echo -e "   ${RED}✗${NC} Puerto 1883: NO disponible"
    ((ISSUES++))
fi

if sudo netstat -tuln | grep -q ":1884.*LISTEN"; then
    echo -e "   ${GREEN}✓${NC} Puerto 1884 (Broker local): ESCUCHANDO"
else
    echo -e "   ${RED}✗${NC} Puerto 1884: NO disponible"
    ((ISSUES++))
fi
echo ""

# 3. Verificar configuración de base de datos
echo "📊 3. Configuración de Medidor"
echo "   ==========================="

DB_PORT=$(python3 << 'EOF'
import sqlite3
try:
    conn = sqlite3.connect('/home/pci/Documents/sebas_giraldo/Tesis-app/dlms-bridge/data/admin.db')
    cursor = conn.cursor()
    cursor.execute("SELECT tb_port, tb_token FROM meters WHERE id=1")
    row = cursor.fetchone()
    print(f"{row[0]}|{row[1] if row[1] else 'NULL'}")
    conn.close()
except Exception as e:
    print(f"ERROR|{e}")
EOF
)

PORT=$(echo $DB_PORT | cut -d'|' -f1)
TOKEN=$(echo $DB_PORT | cut -d'|' -f2)

if [ "$PORT" == "1884" ]; then
    echo -e "   ${GREEN}✓${NC} Puerto configurado: 1884 (correcto)"
else
    echo -e "   ${RED}✗${NC} Puerto configurado: $PORT (debe ser 1884)"
    ((ISSUES++))
fi

if [ "$TOKEN" == "NULL" ]; then
    echo -e "   ${GREEN}✓${NC} Token: NULL (correcto, usa broker local)"
else
    echo -e "   ${YELLOW}⚠${NC} Token configurado: ${TOKEN:0:10}... (debería ser NULL)"
    echo "      Nota: Con token podría causar 'code 7'"
fi
echo ""

# 4. Verificar warnings "code 7" recientes
echo "📊 4. Verificación de Warnings 'code 7'"
echo "   ===================================="

CODE7_COUNT=$(sudo journalctl -u dlms-multi-meter.service --since "5 minutes ago" 2>/dev/null | grep "code 7" | wc -l)

if [ "$CODE7_COUNT" -eq 0 ]; then
    echo -e "   ${GREEN}✓${NC} Sin warnings 'code 7' en los últimos 5 minutos"
else
    echo -e "   ${RED}✗${NC} Encontrados $CODE7_COUNT warnings 'code 7' en los últimos 5 minutos"
    ((ISSUES++))
fi
echo ""

# 5. Verificar que Gateway esté procesando mensajes
echo "📊 5. Gateway Procesando Mensajes"
echo "   ==============================="

GW_MSGS=$(sudo journalctl -u thingsboard-gateway.service --since "2 minutes ago" 2>/dev/null | grep -c "Successfully converted" || echo 0)

if [ "$GW_MSGS" -gt 0 ]; then
    echo -e "   ${GREEN}✓${NC} Gateway procesando mensajes: $GW_MSGS en últimos 2 minutos"
else
    echo -e "   ${YELLOW}⚠${NC} Gateway no ha procesado mensajes recientemente"
    echo "      Puede ser normal si el sistema acaba de iniciar"
fi
echo ""

# 6. Verificar lecturas DLMS recientes
echo "📊 6. Lecturas DLMS Recientes"
echo "   =========================="

READINGS=$(sudo journalctl -u dlms-multi-meter.service --since "1 minute ago" 2>/dev/null | grep -c "V:" || echo 0)

if [ "$READINGS" -gt 0 ]; then
    echo -e "   ${GREEN}✓${NC} Lecturas DLMS: $READINGS en el último minuto"
    echo ""
    echo "   Última lectura:"
    sudo journalctl -u dlms-multi-meter.service --since "2 minutes ago" 2>/dev/null | grep "V:" | tail -1 | sed 's/^/   /'
else
    echo -e "   ${YELLOW}⚠${NC} Sin lecturas DLMS en el último minuto"
fi
echo ""

# Resumen final
echo "============================================"
if [ $ISSUES -eq 0 ]; then
    echo -e "${GREEN}✅ SISTEMA FUNCIONANDO CORRECTAMENTE${NC}"
    echo "   Sin problemas detectados"
else
    echo -e "${RED}❌ SE ENCONTRARON $ISSUES PROBLEMAS${NC}"
    echo "   Revisar logs para más detalles:"
    echo "   - sudo journalctl -u dlms-multi-meter.service -f"
    echo "   - sudo journalctl -u thingsboard-gateway.service -f"
fi
echo "============================================"
echo ""

exit $ISSUES
