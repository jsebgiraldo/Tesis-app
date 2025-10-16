#!/bin/bash
# Quick start script - Ejecuta setup completo del SDK

set -euo pipefail

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo "========================================================================="
echo -e "${GREEN}  SDK TESIS IoT - Quick Start${NC}"
echo "  Arquitectura ISO/IEC 30141:2024 con Wi-Fi HaLow Mesh"
echo "========================================================================="
echo ""

# Verificar que estamos en el directorio correcto
if [ ! -f "WORKSPACE_GUIDE.md" ]; then
    echo -e "${RED}❌ Error: Ejecutar desde raíz del repositorio${NC}"
    exit 1
fi

echo -e "${YELLOW}Paso 1/5: Verificando dependencias...${NC}"

# Docker
if ! command -v docker &> /dev/null; then
    echo "⚠️  Docker no encontrado - instalando..."
    curl -fsSL https://get.docker.com -o get-docker.sh
    sudo sh get-docker.sh
    sudo usermod -aG docker $USER
    echo "✅ Docker instalado (requiere logout/login para aplicar permisos)"
else
    echo "✅ Docker encontrado: $(docker --version)"
fi

# Docker Compose
if ! command -v docker-compose &> /dev/null; then
    echo "⚠️  Docker Compose no encontrado - instalando..."
    sudo curl -L "https://github.com/docker/compose/releases/latest/download/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
    sudo chmod +x /usr/local/bin/docker-compose
    echo "✅ Docker Compose instalado"
else
    echo "✅ Docker Compose: $(docker-compose --version)"
fi

# Python 3
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}❌ Python 3 no encontrado - instalar manualmente${NC}"
    exit 1
else
    echo "✅ Python 3: $(python3 --version)"
fi

echo ""
echo -e "${YELLOW}Paso 2/5: Configurando entorno Python...${NC}"

# Crear virtualenv si no existe
if [ ! -d "venv" ]; then
    python3 -m venv venv
    echo "✅ Virtualenv creado"
fi

source venv/bin/activate
pip install --upgrade pip > /dev/null 2>&1
pip install -r requirements.txt > /dev/null 2>&1
echo "✅ Dependencias Python instaladas"

echo ""
echo -e "${YELLOW}Paso 3/5: Levantando infraestructura Docker...${NC}"

# ThingsBoard Server
echo "  → Iniciando ThingsBoard Server..."
cd infrastructure/thingsboard/server
docker-compose up -d > /dev/null 2>&1
cd ../../..
echo "✅ ThingsBoard Server: http://localhost:8080"

# Leshan LwM2M Server
echo "  → Iniciando Leshan LwM2M Server..."
cd protocols/lwm2m/server_leshan
docker-compose up -d > /dev/null 2>&1
cd ../../..
echo "✅ Leshan Server: http://localhost:8081"

# Esperar a que arranquen
echo "  ⏳ Esperando servicios (30s)..."
sleep 30

echo ""
echo -e "${YELLOW}Paso 4/5: Verificando servicios...${NC}"

# Verificar ThingsBoard
if curl -s http://localhost:8080 > /dev/null; then
    echo "✅ ThingsBoard operativo"
else
    echo -e "${RED}⚠️  ThingsBoard no responde - verificar logs${NC}"
fi

# Verificar Leshan
if curl -s http://localhost:8081/api/clients > /dev/null; then
    echo "✅ Leshan operativo"
else
    echo -e "${RED}⚠️  Leshan no responde - verificar logs${NC}"
fi

echo ""
echo -e "${YELLOW}Paso 5/5: Generando guías de acceso...${NC}"

cat > QUICK_ACCESS.md << 'EOFACCESS'
# Accesos Rápidos - SDK Tesis IoT

## 🌐 Interfaces Web

### ThingsBoard Server
- **URL**: http://localhost:8080
- **Usuario**: `tenant@thingsboard.org`
- **Password**: `tenant`
- **Uso**: Dashboard principal, gestión dispositivos, reglas

### Leshan LwM2M Server
- **URL**: http://localhost:8081
- **Autenticación**: No requerida (development)
- **Uso**: Gestión clientes LwM2M, objetos IPSO, operaciones Read/Write

## 📡 Endpoints MQTT

### Broker Local (Mosquitto)
```bash
# Publicar telemetría
mosquitto_pub -h localhost -t "iot/sensors/test/telemetry" -m '{"temp":22.5}'

# Suscribirse
mosquitto_sub -h localhost -t "iot/sensors/#" -v
```

### ThingsBoard MQTT
```bash
# Telemetría (requiere access token)
mosquitto_pub -h localhost -t "v1/devices/me/telemetry" \
  -u "<ACCESS_TOKEN>" -m '{"temperature":22.5}'
```

## 🔧 Comandos Útiles

### Logs Docker
```bash
# ThingsBoard
docker logs -f thingsboard-server

# Leshan
docker logs -f leshan-server

# Todos
docker-compose -f infrastructure/thingsboard/server/docker-compose.yml logs -f
```

### Build Firmware ESP32-C6
```bash
cd devices/end_nodes/esp32c6_lwm2m_temp
source $HOME/esp/esp-idf/export.sh
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Testing
```bash
# Compliance ISO 30141
cd testing/compliance
python iso30141_validator.py --node ESP32C6-ABC123

# Performance benchmarks
cd testing/performance/latency_benchmarks
python e2e_latency.py --protocol lwm2m --samples 100
```

## 📊 Monitoreo

### Estado Servicios
```bash
docker ps --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"
```

### Captura Tráfico
```bash
cd tools/network
sudo ./packet_analyzer.py --interface lo --protocol coap --duration 60
```

## 🆘 Troubleshooting

### Reiniciar Stack Completo
```bash
# Parar todo
docker-compose -f infrastructure/thingsboard/server/docker-compose.yml down
docker-compose -f protocols/lwm2m/server_leshan/docker-compose.yml down

# Limpiar volúmenes (CUIDADO: borra datos)
docker-compose -f infrastructure/thingsboard/server/docker-compose.yml down -v

# Iniciar
./scripts/quick_start.sh
```

### Verificar Logs
```bash
# Ver últimas 100 líneas
docker logs --tail 100 thingsboard-server

# Seguir logs en tiempo real
docker logs -f leshan-server
```

## 📚 Documentación

- **Guía maestra**: `WORKSPACE_GUIDE.md`
- **Arquitectura**: `ARCHITECTURE_OVERVIEW.md`
- **Getting Started**: `docs/guides/getting_started.md`
- **Changelog**: `CHANGELOG.md`

---
**Generado por**: quick_start.sh | **Fecha**: $(date +%Y-%m-%d)
EOFACCESS

echo "✅ Guía de acceso creada: QUICK_ACCESS.md"

echo ""
echo "========================================================================="
echo -e "${GREEN}✅ SETUP COMPLETADO EXITOSAMENTE${NC}"
echo "========================================================================="
echo ""
echo "🎯 Accesos disponibles:"
echo "   • ThingsBoard:  http://localhost:8080 (tenant@thingsboard.org / tenant)"
echo "   • Leshan:       http://localhost:8081"
echo ""
echo "📖 Próximos pasos:"
echo "   1. Revisar:  cat QUICK_ACCESS.md"
echo "   2. Leer:     cat WORKSPACE_GUIDE.md"
echo "   3. Firmware: cd devices/end_nodes/esp32c6_lwm2m_temp && idf.py build"
echo "   4. Guías:    ls docs/guides/"
echo ""
echo "🔧 Comandos útiles:"
echo "   • Ver estructura:  ./scripts/show_structure.sh"
echo "   • Logs TB:         docker logs -f thingsboard-server"
echo "   • Logs Leshan:     docker logs -f leshan-server"
echo ""
echo "========================================================================="
