#!/bin/bash

# Script de inicio para DLMS to MQTT Bridge
# Automatiza el inicio del sistema con validaciones

echo "=============================================="
echo "   DLMS to MQTT Bridge - Startup Script"
echo "=============================================="
echo ""

# Colores para output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Función para imprimir con colores
print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

# 1. Verificar que estamos en el directorio correcto
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR" || exit 1

print_success "Directorio de trabajo: $SCRIPT_DIR"
echo ""

# 2. Verificar que existen los archivos necesarios
echo "Verificando archivos necesarios..."

required_files=(
    "dlms_mqtt_bridge.py"
    "mqtt_publisher.py"
    "dlms_poller_production.py"
    "mqtt_config.json"
)

missing_files=0
for file in "${required_files[@]}"; do
    if [ -f "$file" ]; then
        print_success "$file encontrado"
    else
        print_error "$file NO encontrado"
        missing_files=$((missing_files + 1))
    fi
done

echo ""

if [ $missing_files -gt 0 ]; then
    print_error "Faltan $missing_files archivos necesarios. Abortando."
    exit 1
fi

# 3. Verificar dependencias de Python
echo "Verificando dependencias de Python..."

check_python_package() {
    python3 -c "import $1" 2>/dev/null
    return $?
}

dependencies=(
    "paho.mqtt:paho-mqtt"
)

missing_deps=0
for dep in "${dependencies[@]}"; do
    IFS=':' read -r import_name package_name <<< "$dep"
    
    if check_python_package "$import_name"; then
        print_success "$package_name instalado"
    else
        print_warning "$package_name NO instalado"
        missing_deps=$((missing_deps + 1))
    fi
done

echo ""

if [ $missing_deps -gt 0 ]; then
    print_warning "Faltan $missing_deps dependencias. Instalando..."
    pip3 install paho-mqtt --break-system-packages
    echo ""
fi

# 4. Verificar configuración
echo "Verificando configuración..."

if grep -q "YOUR_DEVICE_ACCESS_TOKEN_HERE" mqtt_config.json; then
    print_error "El access_token en mqtt_config.json no ha sido configurado"
    echo ""
    echo "Por favor:"
    echo "  1. Abre mqtt_config.json"
    echo "  2. Reemplaza 'YOUR_DEVICE_ACCESS_TOKEN_HERE' con tu token de ThingsBoard"
    echo "  3. Opcionalmente actualiza mqtt_host si no usas demo.thingsboard.io"
    echo ""
    exit 1
else
    print_success "Configuración parece válida"
fi

echo ""

# 5. Mostrar configuración
echo "=== Configuración Actual ==="
echo ""

# Extraer valores del JSON (simple parsing)
dlms_host=$(grep '"dlms_host"' mqtt_config.json | cut -d'"' -f4)
dlms_port=$(grep '"dlms_port"' mqtt_config.json | grep -o '[0-9]*')
mqtt_host=$(grep '"mqtt_host"' mqtt_config.json | head -1 | cut -d'"' -f4)
mqtt_port=$(grep '"mqtt_port"' mqtt_config.json | grep -o '[0-9]*' | head -1)
interval=$(grep '"interval"' mqtt_config.json | grep -o '[0-9.]*')

echo "  Medidor DLMS:      $dlms_host:$dlms_port"
echo "  Broker MQTT:       $mqtt_host:$mqtt_port"
echo "  Intervalo:         ${interval}s"
echo ""

# Extraer mediciones
echo "  Mediciones:"
grep '"voltage_l1"' mqtt_config.json >/dev/null && echo "    • voltage_l1 (Voltaje)"
grep '"current_l1"' mqtt_config.json >/dev/null && echo "    • current_l1 (Corriente)"
grep '"frequency"' mqtt_config.json >/dev/null && echo "    • frequency (Frecuencia)"
grep '"active_power"' mqtt_config.json >/dev/null && echo "    • active_power (Potencia)"
grep '"active_energy"' mqtt_config.json >/dev/null && echo "    • active_energy (Energía)"

echo ""
echo "=============================================="
echo ""

# 6. Preguntar si desea continuar
read -p "¿Iniciar el bridge? (Y/n): " -n 1 -r
echo ""

if [[ ! $REPLY =~ ^[Yy]$ ]] && [[ ! -z $REPLY ]]; then
    echo "Operación cancelada."
    exit 0
fi

echo ""
print_success "Iniciando DLMS to MQTT Bridge..."
echo ""
echo "Presiona Ctrl+C para detener"
echo ""
echo "=============================================="
echo ""

# 7. Iniciar el bridge
python3 dlms_mqtt_bridge.py --config mqtt_config.json

# 8. Capturar código de salida
exit_code=$?

echo ""
echo "=============================================="

if [ $exit_code -eq 0 ]; then
    print_success "Bridge terminado correctamente"
else
    print_error "Bridge terminado con errores (código: $exit_code)"
fi

echo "=============================================="

exit $exit_code
