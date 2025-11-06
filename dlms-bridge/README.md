# DLMS Multi-Meter Bridge

**Solución profesional para adquisición de datos DLMS/COSEM y telemetría IoT hacia ThingsBoard**

[![Python](https://img.shields.io/badge/Python-3.12+-blue.svg)](https://www.python.org/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![ThingsBoard](https://img.shields.io/badge/ThingsBoard-Compatible-orange.svg)](https://thingsboard.io/)

---

## 📋 Descripción

Sistema de adquisición multi-medidor para dispositivos DLMS/COSEM con publicación en tiempo real a ThingsBoard. Arquitectura asíncrona, escalable y robusta diseñada para entornos de producción industrial.

### Características Principales

- ✅ **Multi-Meter Concurrent**: Gestión de múltiples medidores DLMS en paralelo
- ✅ **Publicación MQTT**: Telemetría en tiempo real a ThingsBoard (QoS 1)
- ✅ **ThingsBoard Gateway**: Conector oficial IoT Gateway compatible (NUEVO)
- ✅ **Arquitectura Robusta**: Watchdog, circuit breaker y reconexión automática
- ✅ **API REST**: Gestión administrativa vía FastAPI
- ✅ **Dashboard Web**: Monitoreo en tiempo real con Streamlit
- ✅ **Base de datos**: Configuración persistente con SQLite
- ✅ **Network Monitoring**: Tracking de uso de red (DLMS + MQTT)

---

## 🏗️ Arquitectura

```
┌─────────────────────────────────────────────────────────┐
│ ThingsBoard IoT Platform                                │
│ - Telemetría en tiempo real                             │
│ - Dashboards de visualización                           │
└────────────▲────────────────────────────────────────────┘
             │
             │ MQTT (QoS=1, localhost:1883)
             │
┌────────────┴────────────────────────────────────────────┐
│ DLMS Multi-Meter Bridge Service                         │
│ ┌─────────────────────────────────────────────────────┐ │
│ │ MeterWorker(1)  │ MeterWorker(2)  │ MeterWorker(N) │ │
│ │ - DLMS Poller   │ - DLMS Poller   │ - DLMS Poller  │ │
│ │ - MQTT Client   │ - MQTT Client   │ - MQTT Client  │ │
│ │ - Watchdog      │ - Watchdog      │ - Watchdog     │ │
│ └─────────────────────────────────────────────────────┘ │
└────────────▲────────────────────────────────────────────┘
             │
             │ Configuration (SQLite)
             │
┌────────────┴────────────────────────────────────────────┐
│ Admin API (FastAPI)                                     │
│ - CRUD de medidores                                     │
│ - Gestión de configuración                              │
│ - Métricas y alarmas                                    │
└────────────▲────────────────────────────────────────────┘
             │
             │ HTTP REST
             │
┌────────────┴────────────────────────────────────────────┐
│ Web Dashboard (Streamlit)                               │
│ - Visualización de estado                               │
│ - Gestión de medidores                                  │
│ - Logs y diagnósticos                                   │
└─────────────────────────────────────────────────────────┘
```

---

## 🚀 Inicio Rápido

### Prerequisitos

- Python 3.12+
- MQTT Broker (Mosquitto recomendado)
- ThingsBoard instance (Cloud o local)
- Medidor DLMS/COSEM en red TCP/IP

### Instalación

```bash
# 1. Clonar repositorio
git clone https://github.com/jsebgiraldo/Tesis-app.git
cd Tesis-app/dlms-bridge

# 2. Crear entorno virtual
python3 -m venv venv
source venv/bin/activate

# 3. Instalar dependencias
pip install -r requirements.txt
pip install -r requirements-admin.txt

# 4. Configurar base de datos
python3 -c "from admin.database import Database; Database('data/admin.db').initialize()"
```

### Configuración Básica

1. **Agregar medidor a la base de datos:**

```bash
python3 -c "
from admin.database import Database, create_meter
db = Database('data/admin.db')
session = db.get_session()
create_meter(
    session,
    name='medidor_principal',
    ip_address='192.168.1.127',
    port=3333,
    tb_token='YOUR_THINGSBOARD_TOKEN'
)
session.close()
"
```

2. **Iniciar servicio:**

**Opción A: Bridge Directo (Sistema Actual)**
```bash
# Modo manual (desarrollo)
python3 dlms_multi_meter_bridge.py

# Modo systemd (producción)
sudo systemctl start dlms-multi-meter.service
```

**Opción B: ThingsBoard Gateway (Recomendado para 10+ medidores)**
```bash
# Ver documentación completa en gateway/README.md
cd gateway/
./start_gateway.sh
```

Ver [gateway/README.md](gateway/README.md) para más detalles sobre el ThingsBoard Gateway.

---

## 📦 Estructura del Proyecto

```
dlms-bridge/
├── dlms_multi_meter_bridge.py    # Servicio principal multi-meter
├── dlms_poller_production.py     # Poller DLMS robusto
├── tb_mqtt_client.py              # Cliente ThingsBoard MQTT
├── network_monitor.py             # Monitor de red
├── mqtt_publisher.py              # Publicador MQTT genérico
│
├── admin/                         # Módulo administrativo
│   ├── api.py                     # FastAPI REST API
│   ├── database.py                # ORM SQLAlchemy
│   ├── dashboard.py               # Dashboard Streamlit
│   ├── orchestrator.py            # Orquestador de procesos
│   └── network_scanner.py         # Escáner de red DLMS
│
├── docs/                          # Documentación técnica
│   ├── ARQUITECTURA_SISTEMA.md    # Diseño del sistema
│   ├── ARQUITECTURA_FINAL.md      # Arquitectura implementada
│   ├── GUIA_PRODUCCION.md         # Guía de despliegue
│   ├── NETWORK_MONITORING_IMPLEMENTATION.md
│   ├── RESUMEN_EJECUTIVO.md       # Resumen técnico
│   └── SOLUCION_HDLC_ERRORS.md    # Troubleshooting HDLC
│
├── scripts/                       # Scripts auxiliares
├── data/                          # Base de datos SQLite
├── logs/                          # Logs del sistema
│
├── gateway/                       # ThingsBoard Gateway (NUEVO)
│   ├── config/                    # Configuraciones del gateway
│   ├── connectors/                # Conector DLMS personalizado
│   ├── setup_gateway.sh           # Script de instalación
│   ├── start_gateway.sh           # Script de inicio rápido
│   ├── test_config.py             # Test de configuración
│   ├── README.md                  # Documentación del gateway
│   └── ARCHITECTURE.md            # Arquitectura y comparación
│
├── requirements.txt               # Dependencias core
├── requirements-admin.txt         # Dependencias admin
├── service-manager.sh             # Gestor de servicios
├── start-admin.sh                 # Lanzador API+Dashboard
├── start_mqtt_polling.sh          # Lanzador multi-meter
│
└── systemd/                       # Servicios systemd
    ├── dlms-multi-meter.service
    └── dlms-admin-api.service
```

---

## 🔧 Configuración Avanzada

### Configuración de Medidor

Cada medidor se configura en la base de datos con:

```python
{
    'name': 'medidor_X',           # Nombre único
    'ip_address': '192.168.1.X',   # IP del medidor
    'port': 3333,                   # Puerto DLMS (default: 3333)
    'client_id': 1,                 # DLMS Client ID
    'server_id': 1,                 # DLMS Server ID
    'tb_enabled': True,             # Habilitar ThingsBoard
    'tb_host': 'localhost',         # MQTT broker
    'tb_port': 1883,                # Puerto MQTT
    'tb_token': 'YOUR_TOKEN',       # Token ThingsBoard
    'measurements': [               # Mediciones a leer
        'voltage_l1',
        'current_l1',
        'active_power',
        'frequency'
    ],
    'sampling_interval': 5.0        # Intervalo de polling (seg)
}
```

### Watchdog y Circuit Breaker

El sistema incluye protecciones automáticas:

- **Watchdog HDLC**: Reinicia conexión tras 15 errores HDLC consecutivos
- **Watchdog Read Failures**: Reinicia tras 10 lecturas fallidas consecutivas
- **Circuit Breaker**: Pausa reconexiones tras 10 intentos/hora (5 min pause)
- **Heartbeat**: Actualiza `last_seen` cada 60 ciclos

### Network Monitoring

Tracking automático de:
- Bytes enviados/recibidos por protocolo (DLMS, MQTT)
- Mensajes MQTT publicados
- Bandwidth utilizado
- Tasas de paquetes/segundo

---

## 🛠️ Operación

### Comandos Básicos

```bash
# Ver estado del servicio
sudo systemctl status dlms-multi-meter.service

# Ver logs en tiempo real
sudo journalctl -u dlms-multi-meter.service -f

# Reiniciar servicio
sudo systemctl restart dlms-multi-meter.service

# Ver logs con filtros
sudo journalctl -u dlms-multi-meter.service --since "1 hour ago" | grep "ERROR"
```

### API REST

El sistema expone una API REST en `http://localhost:8000`:

```bash
# Listar medidores
curl http://localhost:8000/api/meters

# Estado del sistema
curl http://localhost:8000/api/system/health

# Métricas de red
curl http://localhost:8000/api/network/metrics

# Alarmas activas
curl http://localhost:8000/api/alarms?unacknowledged=true
```

Ver documentación completa en: `http://localhost:8000/docs`

### Dashboard Web

Acceder al dashboard en: `http://localhost:8501`

Funcionalidades:
- ✅ Visualización de estado de medidores
- ✅ Configuración dinámica
- ✅ Logs en tiempo real
- ✅ Métricas de red
- ✅ Gestión de alarmas

---

## 📊 Monitoreo y Diagnóstico

### Health Check Script

```bash
./monitor_watchdog.sh
```

Reporta:
- Success rate de lecturas DLMS
- Mensajes MQTT publicados
- Errores HDLC detectados
- Activaciones del watchdog
- Estado de circuit breaker

### Métricas Clave

El servicio reporta cada 60 segundos:

```
📊 SYSTEM STATUS REPORT
==================================================================
Meter 1 (medidor_principal): 
  Cycles=1234, Success=89.5%, MQTT=1102, Runtime=6170s
  └─ Network: DLMS 2468 req, MQTT 1102 msg (120534 bytes)
==================================================================
```

---

## 🔍 Troubleshooting

### Error: "No readings returned"

**Causa**: Medidor no responde o errores HDLC.

**Solución**:
1. Verificar conectividad: `ping <IP_MEDIDOR>`
2. Verificar puerto DLMS: `nc -zv <IP_MEDIDOR> 3333`
3. Revisar logs: `journalctl -u dlms-multi-meter.service -n 100`

### Error: MQTT "code 7" (Conflicto de token)

**Causa**: Múltiples clientes usando el mismo token ThingsBoard.

**Solución**:
1. Verificar que API/Dashboard estén detenidos
2. Usar tokens únicos por medidor
3. Considerar migración a Gateway Pattern (ver docs)

### Circuit Breaker Activado

**Causa**: Más de 10 reconexiones en 1 hora.

**Solución**:
1. Revisar estabilidad de red
2. Verificar configuración DLMS del medidor
3. Aumentar timeouts si es necesario

Ver más en: [docs/SOLUCION_HDLC_ERRORS.md](docs/SOLUCION_HDLC_ERRORS.md)

---

## 📈 Rendimiento

### Capacidad

- **Medidores concurrentes**: Hasta 50+ por instancia
- **Throughput MQTT**: ~100 msg/s (configuración estándar)
- **Latencia**: <1s desde lectura DLMS hasta ThingsBoard
- **Disponibilidad**: >99.5% con watchdog habilitado

### Optimizaciones Aplicadas

- ✅ Polling interval aumentado (1s → 5s) para reducir carga
- ✅ DLMS timeout aumentado (3s → 5s) para enlaces lentos
- ✅ Buffer pre-cleaning para evitar errores de secuencia
- ✅ Logging a INFO para reducir I/O disk
- ✅ MQTT QoS=1 con keepalive=60s

---

## 🤝 Contribución

Para reportar bugs o solicitar features, abrir un issue en GitHub.

---

## 📄 Licencia

MIT License - ver [LICENSE](LICENSE) para detalles.

---

## 👥 Autores

- **Sebastián Giraldo** - [@jsebgiraldo](https://github.com/jsebgiraldo)

---

## 📚 Referencias

- [ThingsBoard Documentation](https://thingsboard.io/docs/)
- [ThingsBoard Gateway Documentation](https://thingsboard.io/docs/iot-gateway/)
- [DLMS/COSEM Green Book](https://www.dlms.com/)
- [Microstar DLMS Protocol Guide](docs/9.2.%20Microstar%20DLMS%20Protocol%20Guide.pdf)
- [Python DLMS Library](https://github.com/pwitab/dlms-cosem)

---

## 🔗 Enlaces Útiles

- [Guía de Producción](docs/GUIA_PRODUCCION.md)
- [Arquitectura del Sistema](docs/ARQUITECTURA_FINAL.md)
- [Resumen Ejecutivo](docs/RESUMEN_EJECUTIVO.md)
- [Network Monitoring](docs/NETWORK_MONITORING_IMPLEMENTATION.md)
- **[ThingsBoard Gateway - DLMS Connector](gateway/README.md)** ⭐ NUEVO
- **[Gateway Architecture Comparison](gateway/ARCHITECTURE.md)** ⭐ NUEVO

---

**Última actualización**: Noviembre 2025
