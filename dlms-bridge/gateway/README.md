# ThingsBoard Gateway - DLMS Connector

Sistema de gateway IoT para conectar medidores DLMS/COSEM a ThingsBoard utilizando la arquitectura oficial de ThingsBoard IoT Gateway.

[![ThingsBoard](https://img.shields.io/badge/ThingsBoard-Gateway-orange.svg)](https://thingsboard.io/docs/iot-gateway/)
[![Python](https://img.shields.io/badge/Python-3.12+-blue.svg)](https://www.python.org/)
[![DLMS](https://img.shields.io/badge/Protocol-DLMS%2FCOSEM-green.svg)](https://www.dlms.com/)

---

## 📋 Descripción

Este proyecto implementa un **conector personalizado** para ThingsBoard IoT Gateway que permite conectar múltiples medidores DLMS/COSEM a una plataforma ThingsBoard.

### ¿Qué es ThingsBoard Gateway?

ThingsBoard Gateway es un componente oficial de ThingsBoard que actúa como intermediario entre dispositivos IoT (que no pueden conectarse directamente a ThingsBoard) y la plataforma ThingsBoard. El gateway:

- ✅ Conecta dispositivos de diferentes protocolos (Modbus, OPC-UA, BLE, etc.)
- ✅ Traduce los datos de los dispositivos a formato ThingsBoard
- ✅ Gestiona múltiples dispositivos desde un único punto
- ✅ Proporciona funcionalidades de buffering y reconexión automática
- ✅ Soporta RPC (Remote Procedure Calls) bidireccionales

### Arquitectura del Gateway

```
┌──────────────────────────────────────────────────┐
│ ThingsBoard Platform (Cloud/On-Premise)         │
│ - Device Management                              │
│ - Dashboards & Visualizations                    │
│ - Rule Engine                                    │
└────────────▲─────────────────────────────────────┘
             │
             │ MQTT (Gateway Device)
             │
┌────────────┴─────────────────────────────────────┐
│ ThingsBoard IoT Gateway                          │
│ ┌──────────────────────────────────────────────┐ │
│ │ Gateway Core                                 │ │
│ │ - Connection Management                      │ │
│ │ - Device Registry                            │ │
│ │ - Message Routing                            │ │
│ │ - Storage & Buffering                        │ │
│ └──────┬───────────────────────────────────────┘ │
│        │                                          │
│        ├─── MQTT Connector (Built-in)            │
│        ├─── Modbus Connector (Built-in)          │
│        ├─── OPC-UA Connector (Built-in)          │
│        └─── DLMS Connector (Custom) ◄── ESTE     │
│                                                   │
└────────────┬─────────────────────────────────────┘
             │
             │ DLMS/COSEM Protocol
             │
┌────────────┴─────────────────────────────────────┐
│ DLMS Devices (Medidores)                         │
│ ┌──────────┐  ┌──────────┐  ┌──────────┐        │
│ │ Meter 1  │  │ Meter 2  │  │ Meter N  │        │
│ │192.168.1 │  │192.168.1 │  │192.168.1 │        │
│ │    .127  │  │    .128  │  │    .xxx  │        │
│ └──────────┘  └──────────┘  └──────────┘        │
└──────────────────────────────────────────────────┘
```

### Ventajas del Gateway Pattern

✅ **Centralización**: Un único gateway gestiona múltiples medidores  
✅ **Seguridad**: Solo el gateway necesita credenciales de ThingsBoard  
✅ **Escalabilidad**: Fácil agregar/remover dispositivos sin cambiar tokens  
✅ **Buffering**: El gateway almacena datos si pierde conexión con ThingsBoard  
✅ **Gestión Simplificada**: Configuración centralizada en archivos JSON/YAML  

---

## 🏗️ Estructura del Proyecto

```
gateway/
├── config/                          # Configuraciones
│   ├── tb_gateway.yaml              # Config principal del gateway
│   └── dlms_connector.json          # Config del conector DLMS
│
├── connectors/                      # Conectores personalizados
│   └── dlms_connector.py            # Conector DLMS (este proyecto)
│
├── logs/                            # Logs del gateway
│
├── setup_gateway.sh                 # Script de instalación completa
├── start_gateway.sh                 # Script de inicio rápido (dev)
└── README.md                        # Esta documentación
```

---

## 🚀 Instalación

### Opción 1: Instalación Completa (Producción)

Para instalar el gateway como servicio systemd:

```bash
cd gateway/
sudo ./setup_gateway.sh install
```

Este script:
1. ✅ Instala dependencias del sistema (python3-dev, libglib2.0-dev)
2. ✅ Instala ThingsBoard Gateway via pip
3. ✅ Crea directorios en `/etc/thingsboard-gateway/`
4. ✅ Copia configuraciones y connectors
5. ✅ Crea servicio systemd `tb-gateway.service`

### Opción 2: Instalación Manual (Desarrollo)

Para pruebas y desarrollo sin systemd:

```bash
# 1. Instalar ThingsBoard Gateway
pip install thingsboard-gateway

# 2. Instalar dependencias del proyecto
cd ..
pip install -r requirements.txt

# 3. Iniciar gateway en modo desarrollo
cd gateway/
./start_gateway.sh
```

---

## ⚙️ Configuración

### 1. Crear Gateway en ThingsBoard

Primero, necesitas crear un dispositivo Gateway en tu instancia de ThingsBoard:

1. **Accede a ThingsBoard**
   - Cloud: https://demo.thingsboard.io
   - Local: http://localhost:8080

2. **Crea un Gateway Device**
   - Ve a **Devices** → **Add Device** (+)
   - Name: `DLMS Gateway`
   - Device Profile: `default` (o crea uno específico)
   - **Es gateway**: ✅ Marcar esta opción
   - Click **Add**

3. **Obtén el Access Token**
   - Abre el dispositivo creado
   - Ve a la pestaña **Details**
   - Copia el **Access Token** (ej: `A1B2C3D4E5F6G7H8I9J0`)

### 2. Configurar Gateway (`config/tb_gateway.yaml`)

Edita el archivo de configuración principal:

```yaml
thingsboard:
  host: localhost          # Cambiar a tu servidor ThingsBoard
  port: 1883
  
  security:
    type: accessToken
    accessToken: "TU_GATEWAY_TOKEN_AQUI"  # ◄── Pegar token del paso anterior
  
  qos: 1

connectors:
  - name: "DLMS Connector"
    type: custom
    class: DLMSConnector
    module: dlms_connector
    enabled: true
    configuration: "dlms_connector.json"
```

**Configuraciones Importantes:**

- `host`: Dirección de tu servidor ThingsBoard
  - Cloud: `demo.thingsboard.io`
  - Local: `localhost`
- `accessToken`: Token del gateway (NO de los medidores individuales)
- `qos`: Quality of Service MQTT (0, 1, o 2)

### 3. Configurar Medidores DLMS (`config/dlms_connector.json`)

Configura tus medidores DLMS:

```json
{
  "devices": [
    {
      "name": "medidor_principal",
      "deviceType": "DLMS_Energy_Meter",
      "host": "192.168.1.127",
      "port": 3333,
      "pollingInterval": 5000,
      
      "measurements": [
        "voltage_l1",
        "current_l1",
        "active_power",
        "frequency"
      ],
      
      "attributesMapping": {
        "host": "${host}",
        "port": "${port}",
        "meter_type": "DLMS/COSEM"
      },
      
      "timeseriesMapping": {
        "voltage_l1": "${voltage_l1}",
        "current_l1": "${current_l1}",
        "active_power": "${active_power}",
        "frequency": "${frequency}"
      }
    }
  ]
}
```

**Parámetros de Device:**

| Parámetro | Descripción | Ejemplo |
|-----------|-------------|---------|
| `name` | Nombre único del medidor (será visible en ThingsBoard) | `"medidor_principal"` |
| `deviceType` | Tipo de dispositivo en ThingsBoard | `"DLMS_Energy_Meter"` |
| `host` | Dirección IP del medidor DLMS | `"192.168.1.127"` |
| `port` | Puerto DLMS del medidor | `3333` |
| `pollingInterval` | Intervalo de lectura en milisegundos | `5000` (5 segundos) |
| `measurements` | Lista de mediciones DLMS a leer | Ver tabla abajo |

**Mediciones Disponibles:**

| Measurement | Descripción | Unidad |
|-------------|-------------|--------|
| `voltage_l1`, `voltage_l2`, `voltage_l3` | Voltaje por fase | V |
| `current_l1`, `current_l2`, `current_l3` | Corriente por fase | A |
| `active_power` | Potencia activa total | W |
| `reactive_power` | Potencia reactiva | VAr |
| `apparent_power` | Potencia aparente | VA |
| `power_factor` | Factor de potencia | - |
| `frequency` | Frecuencia de red | Hz |
| `active_energy` | Energía activa acumulada | kWh |

---

## 🎮 Uso

### Iniciar Gateway

**Modo Producción (systemd):**
```bash
sudo systemctl start tb-gateway.service
sudo systemctl status tb-gateway.service
```

**Modo Desarrollo:**
```bash
cd gateway/
./start_gateway.sh
```

### Comandos Útiles

```bash
# Ver estado del servicio
sudo systemctl status tb-gateway.service

# Ver logs en tiempo real
sudo journalctl -u tb-gateway.service -f

# Reiniciar gateway
sudo systemctl restart tb-gateway.service

# Detener gateway
sudo systemctl stop tb-gateway.service

# Habilitar inicio automático
sudo systemctl enable tb-gateway.service

# Actualizar configuración
sudo ./setup_gateway.sh update-config
sudo systemctl restart tb-gateway.service
```

### Verificar Funcionamiento

1. **En los logs del gateway:**
   ```
   [DLMSConnector] Opening DLMS Connector...
   [DLMSConnector] Connected 2/2 devices
   [DLMSDevice[medidor_principal]] Connected to 192.168.1.127:3333
   [DLMSConnector] Device 'medidor_principal' added to ThingsBoard
   [DLMSConnector] Sent telemetry for 'medidor_principal': {...}
   ```

2. **En ThingsBoard UI:**
   - Ve a **Devices**
   - Deberías ver:
     - ✅ `DLMS Gateway` (el gateway)
     - ✅ `medidor_principal` (el medidor, hijo del gateway)
   - Abre `medidor_principal` → **Latest telemetry**
   - Deberías ver datos actualizándose cada 5 segundos

---

## 🔧 Integración con Sistema Existente

Este gateway puede coexistir con tu sistema actual (`dlms_multi_meter_bridge.py`):

### Opción A: Migración Completa al Gateway

**Ventajas:**
- ✅ Arquitectura estándar de ThingsBoard
- ✅ Gestión centralizada de dispositivos
- ✅ Sin conflictos de tokens MQTT
- ✅ Buffering automático

**Pasos:**
1. Detener `dlms_multi_meter_bridge.py`
2. Configurar medidores en `dlms_connector.json`
3. Iniciar gateway

### Opción B: Uso Paralelo

**Ventajas:**
- ✅ Migración gradual
- ✅ Comparación de datos
- ✅ Redundancia

**Configuración:**
```bash
# Sistema actual: usa tokens individuales por medidor
# Gateway: usa un único token de gateway

# NO conflicto porque:
# - Sistema actual: cada medidor = token individual
# - Gateway: todos los medidores = 1 token de gateway
```

**Ejemplo:**
```
ThingsBoard:
├── Medidor_A (token: ABC123)  ◄── dlms_multi_meter_bridge.py
├── Medidor_B (token: DEF456)  ◄── dlms_multi_meter_bridge.py
└── DLMS_Gateway (token: XYZ789)
    ├── medidor_C  ◄── Gateway
    └── medidor_D  ◄── Gateway
```

---

## 📊 Monitoreo y Diagnóstico

### Métricas del Gateway

El gateway reporta métricas a ThingsBoard automáticamente:

```json
{
  "devicesTotal": 2,
  "devicesUp": 2,
  "devicesDown": 0,
  "bytesReceived": 12345,
  "bytesSent": 67890,
  "messagesReceived": 100,
  "messagesSent": 200
}
```

### Logs del Conector

Los logs del conector DLMS incluyen:

```
[DLMSDevice[medidor_principal]] Total polls: 1234
[DLMSDevice[medidor_principal]] Success rate: 98.5%
[DLMSDevice[medidor_principal]] Last poll: 2025-11-04 10:30:45
```

### Troubleshooting

**Problema: Gateway no conecta a ThingsBoard**

```bash
# Verificar token
grep "accessToken" config/tb_gateway.yaml

# Verificar conectividad
ping demo.thingsboard.io
telnet demo.thingsboard.io 1883
```

**Problema: Medidor no aparece en ThingsBoard**

```bash
# Ver logs del conector
sudo journalctl -u tb-gateway.service | grep "DLMSConnector"

# Verificar configuración
cat config/dlms_connector.json

# Verificar conectividad DLMS
nc -zv 192.168.1.127 3333
```

**Problema: "ModuleNotFoundError: No module named 'dlms_connector'"**

```bash
# Verificar PYTHONPATH
echo $PYTHONPATH

# Debe incluir: /etc/thingsboard-gateway/connectors

# Actualizar configuración
sudo ./setup_gateway.sh update-config
sudo systemctl restart tb-gateway.service
```

---

## 📚 Referencias

### Documentación Oficial

- [ThingsBoard Gateway Documentation](https://thingsboard.io/docs/iot-gateway/)
- [ThingsBoard Gateway GitHub](https://github.com/thingsboard/thingsboard-gateway)
- [Custom Connector Development](https://thingsboard.io/docs/iot-gateway/custom/)
- [DLMS/COSEM Green Book](https://www.dlms.com/)

### Arquitectura Gateway vs Direct Connection

| Característica | Direct Connection | Gateway Pattern |
|----------------|------------------|-----------------|
| Tokens necesarios | 1 por dispositivo | 1 para todos |
| Gestión | Distribuida | Centralizada |
| Buffering | Manual | Automático |
| Escalabilidad | Media | Alta |
| Complejidad inicial | Baja | Media |
| Mantenimiento | Alto | Bajo |

---

## 🤝 Contribución

Para mejorar este conector:

1. Fork el repositorio
2. Crea una rama (`git checkout -b feature/mejora`)
3. Commit tus cambios (`git commit -am 'Add feature'`)
4. Push a la rama (`git push origin feature/mejora`)
5. Crea un Pull Request

---

## 📄 Licencia

MIT License - ver [LICENSE](../LICENSE) para detalles.

---

## 👥 Autores

- **Sebastián Giraldo** - [@jsebgiraldo](https://github.com/jsebgiraldo)

---

## 🔗 Enlaces Rápidos

- [Documentación Principal](../README.md)
- [Guía de Producción](../docs/GUIA_PRODUCCION.md)
- [Arquitectura del Sistema](../docs/ARQUITECTURA_FINAL.md)
- [ThingsBoard Cloud](https://demo.thingsboard.io)

---

**Última actualización**: Noviembre 2025
