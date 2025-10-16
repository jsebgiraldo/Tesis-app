# Guía de Inicio Rápido - SDK Tesis IoT

Esta guía te ayudará a configurar tu entorno de desarrollo y ejecutar tu primera demo en menos de 30 minutos.

## 📋 Prerrequisitos

### Hardware Necesario
- **Desarrollo**: PC con Ubuntu 20.04+ (o WSL2/macOS)
- **Dispositivo IoT**: ESP32-C6 DevKit
- **Cable**: USB-C para programación
- **Opcional**: Router HaLow 802.11ah (para tests mesh)

### Software Base
```bash
# Verificar versiones
docker --version          # >= 24.0
docker-compose --version  # >= 2.20
python3 --version         # >= 3.11
git --version            # >= 2.30
```

## 🚀 Instalación Paso a Paso

### 1. Clonar Repositorio
```bash
cd ~
git clone https://github.com/jsebgiraldo/Tesis-app.git
cd Tesis-app
```

### 2. Ejecutar Setup Automático
```bash
./scripts/setup_environment.sh
```

Este script instalará:
- Docker y Docker Compose (si no están instalados)
- Dependencias Python (`requirements.txt`)
- Opcionalmente: ESP-IDF v5.2+ para compilar firmware

### 3. Configurar ESP-IDF (para desarrollo firmware)
```bash
# Si no lo instaló el script anterior
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.2.3  # Versión estable
./install.sh esp32c6

# Añadir al PATH (agregar a ~/.bashrc)
alias get_idf='. $HOME/esp/esp-idf/export.sh'
```

Luego en cada terminal:
```bash
get_idf  # Activa entorno ESP-IDF
```

## 🎯 Primera Demo: Nodo LwM2M + ThingsBoard

### Paso 1: Levantar Infraestructura
```bash
cd ~/Tesis-app

# ThingsBoard Server
cd infrastructure/thingsboard/server
docker-compose up -d

# Esperar 30 segundos para que arranque
sleep 30

# Verificar
docker-compose ps
curl -s http://localhost:8080 | grep -i "thingsboard"
```

Acceso web:
- URL: http://localhost:8080
- Usuario: `tenant@thingsboard.org`
- Password: `tenant`

### Paso 2: Levantar Servidor LwM2M
```bash
cd ~/Tesis-app/protocols/lwm2m/server_leshan
docker-compose up -d

# Verificar
curl http://localhost:8081/api/clients
```

Acceso web:
- URL: http://localhost:8081
- No requiere autenticación (development)

### Paso 3: Compilar Firmware LwM2M
```bash
cd ~/Tesis-app/devices/end_nodes/esp32c6_lwm2m_temp

# Activar ESP-IDF
get_idf

# Configurar target
idf.py set-target esp32c6

# Menú configuración
idf.py menuconfig
# Navegar a:
#   - LwM2M Configuration → Server URI: coap://192.168.1.100:5683
#     (cambiar IP por la de tu PC)
#   - WiFi Configuration → SSID y Password

# Compilar
idf.py build
```

### Paso 4: Flashear ESP32-C6
```bash
# Conectar ESP32-C6 via USB
# Verificar puerto (usualmente /dev/ttyUSB0)
ls /dev/ttyUSB*

# Flash
idf.py -p /dev/ttyUSB0 flash

# Monitor serial
idf.py -p /dev/ttyUSB0 monitor
# Para salir: Ctrl+]
```

### Paso 5: Verificar Registro
**En Leshan (http://localhost:8081):**
- Deberías ver tu dispositivo en la lista de clientes
- Endpoint name: `ESP32C6-XXXXXX` (XXXXXX = últimos 6 dígitos MAC)
- Objetos disponibles: `/3` (Device), `/3303` (Temperature), `/3304` (Humidity)

**Leer temperatura:**
1. Click en el endpoint
2. Navegar a `/3303/0/5700` (Temperature Sensor Value)
3. Click "Read"
4. Verás valor simulado (~22-25°C)

### Paso 6: Conectar ThingsBoard (Opcional)
```bash
# Ir a ThingsBoard UI
# Devices → Add Device → Name: "ESP32C6-Temp-Sensor"
# Copy Access Token

# Reconfigurar nodo con token
cd ~/Tesis-app/devices/end_nodes/esp32c6_mqtt
idf.py menuconfig
# → ThingsBoard Token: <pegar token>
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## 🧪 Validar Instalación

### Test 1: Infraestructura Docker
```bash
cd ~/Tesis-app

# Verificar servicios activos
docker ps | grep -E "thingsboard|leshan"

# Logs ThingsBoard
docker logs -f $(docker ps -q --filter "name=thingsboard")

# Logs Leshan
docker logs -f $(docker ps -q --filter "name=leshan")
```

### Test 2: Conformidad LwM2M
```bash
cd ~/Tesis-app/testing/compliance

# Crear entorno Python
python3 -m venv venv
source venv/bin/activate
pip install -r ../../requirements.txt

# Ejecutar test (reemplazar ENDPOINT con tu dispositivo)
python iso30141_validator.py --node ESP32C6-XXXXXX

# Debería mostrar:
# ✓ Device domain: OK
# ✓ Information viewpoint: OK
# ✓ Security viewpoint: CHECK (si usas DTLS)
```

### Test 3: Captura Tráfico CoAP
```bash
cd ~/Tesis-app/tools/network

# Capturar tráfico LwM2M (CoAP puerto 5683)
sudo ./packet_analyzer.py --interface lo --protocol coap --duration 60

# Analizar PCAP generado
wireshark ~/Tesis-app/data/captures/pcaps/latest.pcapng
# Filtro Wireshark: coap
```

## 📊 Dashboards de Monitoreo

### ThingsBoard
1. Login: http://localhost:8080
2. Devices → Tu dispositivo → Latest Telemetry
3. Debería mostrar `temperature` y `humidity` actualizándose

### Leshan
1. Abrir: http://localhost:8081
2. Click en tu endpoint
3. Navegar objetos IPSO:
   - `/3/0` - Device info (manufacturer, model, serial)
   - `/3303/0/5700` - Temperature value
   - `/3304/0/5700` - Humidity value

### Prometheus + Grafana (Opcional)
```bash
cd ~/Tesis-app/infrastructure/monitoring
docker-compose up -d

# Grafana: http://localhost:3000
# Usuario: admin / Password: admin
# Importar dashboard: dashboards/iot_overview.json
```

## 🔧 Troubleshooting

### Problema: ESP32-C6 no se detecta
```bash
# Instalar drivers USB-serial
sudo apt install python3-serial

# Permisos usuario
sudo usermod -a -G dialout $USER
# Logout/login para aplicar

# Verificar
ls -l /dev/ttyUSB*
```

### Problema: ThingsBoard no arranca
```bash
# Ver logs detallados
cd infrastructure/thingsboard/server
docker-compose logs -f

# Reiniciar limpio
docker-compose down -v
docker-compose up -d
```

### Problema: LwM2M registration timeout
```bash
# 1. Verificar IP servidor en configuración ESP32-C6
idf.py menuconfig
# → LwM2M Server URI debe apuntar a IP de tu PC (no localhost)

# 2. Verificar firewall
sudo ufw status
sudo ufw allow 5683/udp  # CoAP sin DTLS
sudo ufw allow 5684/udp  # CoAP con DTLS

# 3. Test conectividad
ping <IP_ESP32C6>
nc -u -l 5683  # Escuchar UDP en 5683
```

### Problema: Compilación ESP-IDF falla
```bash
# Limpiar build
cd devices/end_nodes/esp32c6_lwm2m_temp
idf.py fullclean

# Re-instalar ESP-IDF
cd ~/esp/esp-idf
git pull
git submodule update --init --recursive
./install.sh esp32c6

# Re-exportar
. ./export.sh
```

## 📚 Próximos Pasos

1. **Explorar Objetos IPSO**: Ver `docs/guides/lwm2m_deployment.md`
2. **Configurar Mesh HaLow**: Ver `docs/guides/halow_mesh_setup.md`
3. **Gateways Modbus/DLMS**: Ver `docs/guides/gateway_protocols.md`
4. **Experimentos Performance**: Ver `testing/performance/README.md`
5. **Validación ISO 30141**: Ver `testing/compliance/README.md`

## 🆘 Soporte

- **Documentación**: `docs/guides/`
- **Issues**: https://github.com/jsebgiraldo/Tesis-app/issues
- **Ejemplos**: `examples/`

---

**Siguiente**: [Despliegue LwM2M Completo](lwm2m_deployment.md)
