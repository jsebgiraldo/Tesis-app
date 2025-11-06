#!/bin/bash

# Script de provisioning y arranque automático para ThingsBoard local
# Hace el provisioning del dispositivo y arranca el bridge MQTT

echo "╔══════════════════════════════════════════════════════════════════════╗"
echo "║   DLMS Bridge - Auto-Provisioning para ThingsBoard Local            ║"
echo "╔══════════════════════════════════════════════════════════════════════╗"
echo ""

# Colores
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

print_info() {
    echo -e "${BLUE}ℹ${NC} $1"
}

# Directorio de trabajo
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR" || exit 1

# Configuración por defecto
TB_HOST="${TB_HOST:-localhost}"
TB_PORT="${TB_PORT:-8080}"
TB_USERNAME="${TB_USERNAME:-tenant@thingsboard.org}"
TB_PASSWORD="${TB_PASSWORD:-tenant}"
DEVICE_NAME="${DEVICE_NAME:-medidor_dlms_principal}"

echo "=== Configuración ==="
echo ""
echo "  ThingsBoard Host:     $TB_HOST"
echo "  ThingsBoard Port:     $TB_PORT"
echo "  Usuario:              $TB_USERNAME"
echo "  Nombre del Dispositivo: $DEVICE_NAME"
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# Preguntar si continuar con valores por defecto
print_info "¿Usar estos valores? (Y/n/custom): "
read -r REPLY

if [[ $REPLY =~ ^[Cc]ustom$ ]] || [[ $REPLY =~ ^[Cc]$ ]]; then
    echo ""
    print_info "Configuración personalizada:"
    echo ""
    
    read -p "  ThingsBoard Host [$TB_HOST]: " input
    TB_HOST="${input:-$TB_HOST}"
    
    read -p "  ThingsBoard Port [$TB_PORT]: " input
    TB_PORT="${input:-$TB_PORT}"
    
    read -p "  Usuario [$TB_USERNAME]: " input
    TB_USERNAME="${input:-$TB_USERNAME}"
    
    read -sp "  Contraseña [***]: " input
    TB_PASSWORD="${input:-$TB_PASSWORD}"
    echo ""
    
    read -p "  Nombre dispositivo [$DEVICE_NAME]: " input
    DEVICE_NAME="${input:-$DEVICE_NAME}"
    
    echo ""
elif [[ $REPLY =~ ^[Nn]$ ]]; then
    echo ""
    print_warning "Operación cancelada"
    exit 0
fi

echo ""
print_info "Verificando ThingsBoard..."
echo ""

# Verificar que ThingsBoard esté corriendo
if curl -s -o /dev/null -w "%{http_code}" "http://$TB_HOST:$TB_PORT" | grep -q "200\|302"; then
    print_success "ThingsBoard está corriendo en http://$TB_HOST:$TB_PORT"
else
    print_error "No se puede conectar a ThingsBoard en http://$TB_HOST:$TB_PORT"
    echo ""
    print_info "Verifica que ThingsBoard esté corriendo:"
    echo "    sudo systemctl status thingsboard"
    echo "    # o"
    echo "    docker ps | grep thingsboard"
    echo ""
    exit 1
fi

echo ""
print_info "Ejecutando provisioning automático..."
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# Ejecutar provisioning
python3 thingsboard_provisioning.py \
    --host "$TB_HOST" \
    --port "$TB_PORT" \
    --username "$TB_USERNAME" \
    --password "$TB_PASSWORD" \
    --device-name "$DEVICE_NAME" \
    --config mqtt_config.json

provisioning_exit_code=$?

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

if [ $provisioning_exit_code -ne 0 ]; then
    print_error "Provisioning falló"
    echo ""
    print_info "Posibles causas:"
    echo "  • Usuario/contraseña incorrectos"
    echo "  • ThingsBoard no accesible"
    echo "  • Permisos insuficientes"
    echo ""
    exit 1
fi

print_success "Provisioning completado exitosamente!"
echo ""

# Preguntar si iniciar el bridge
print_info "¿Iniciar el DLMS to MQTT Bridge ahora? (Y/n): "
read -r -n 1 REPLY
echo ""

if [[ ! $REPLY =~ ^[Nn]$ ]]; then
    echo ""
    print_success "Iniciando DLMS to MQTT Bridge..."
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    echo "Presiona Ctrl+C para detener"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    
    # Pequeña pausa para leer el mensaje
    sleep 2
    
    # Iniciar el bridge
    python3 dlms_mqtt_bridge.py --config mqtt_config.json
    
    bridge_exit_code=$?
    
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    if [ $bridge_exit_code -eq 0 ]; then
        print_success "Bridge detenido correctamente"
    else
        print_warning "Bridge terminó con código de salida: $bridge_exit_code"
    fi
else
    echo ""
    print_info "Para iniciar el bridge manualmente, ejecuta:"
    echo "    ./start_mqtt_polling.sh"
    echo "    # o directamente:"
    echo "    python3 dlms_mqtt_bridge.py --config mqtt_config.json"
fi

echo ""
echo "╚══════════════════════════════════════════════════════════════════════╝"
echo ""

exit 0
