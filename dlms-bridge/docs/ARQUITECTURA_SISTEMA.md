# 🏗️ Arquitectura del Sistema DLMS Multi-Meter

**Fecha:** 31 de Octubre de 2025  
**Versión:** 2.0 (Escalable)  
**Estado:** Producción

---

## 📋 Tabla de Contenidos

1. [Visión General](#visión-general)
2. [Arquitectura de Alto Nivel](#arquitectura-de-alto-nivel)
3. [Componentes del Sistema](#componentes-del-sistema)
4. [Flujo de Datos](#flujo-de-datos)
5. [Servicios SystemD](#servicios-systemd)
6. [Base de Datos](#base-de-datos)
7. [Protocolos y Comunicaciones](#protocolos-y-comunicaciones)
8. [Seguridad](#seguridad)
9. [Escalabilidad](#escalabilidad)
10. [Monitoreo y Logs](#monitoreo-y-logs)
11. [Deployment](#deployment)
12. [Troubleshooting](#troubleshooting)

---

## 🌐 Visión General

Sistema escalable para lectura de medidores eléctricos DLMS y publicación de telemetría a ThingsBoard IoT Platform.

### Características Principales

- ✅ **Escalable:** Un solo servicio para N medidores
- ✅ **Asíncrono:** Workers concurrentes por medidor
- ✅ **Optimizado:** Caché de scalers (50% más rápido)
- ✅ **Resiliente:** Auto-recuperación ante errores
- ✅ **Centralizado:** Gestión desde dashboard web
- ✅ **Configuración dinámica:** Base de datos SQLite

### Stack Tecnológico

```
Python 3.12
├─ asyncio          (Concurrencia)
├─ paho-mqtt        (Publicación IoT)
├─ SQLAlchemy       (ORM)
├─ FastAPI          (REST API)
├─ Streamlit        (Dashboard)
└─ SystemD          (Gestión de servicios)
```

---

## 🏗️ Arquitectura de Alto Nivel

```
┌─────────────────────────────────────────────────────────────────────┐
│                        CAPA DE PRESENTACIÓN                         │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  Dashboard Web (Streamlit)                                   │  │
│  │  http://localhost:8501                                       │  │
│  │  • Visualización en tiempo real                             │  │
│  │  • Configuración de medidores                               │  │
│  │  • Gestión de alarmas                                       │  │
│  │  • Gráficos y reportes                                      │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                              ↓ HTTP                                 │
└─────────────────────────────────────────────────────────────────────┘
                               ↓
┌─────────────────────────────────────────────────────────────────────┐
│                        CAPA DE APLICACIÓN                           │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  REST API (FastAPI)                                          │  │
│  │  http://localhost:8000                                       │  │
│  │  Endpoints:                                                  │  │
│  │  • GET  /meters          → Lista de medidores               │  │
│  │  • POST /meters          → Crear medidor                    │  │
│  │  • GET  /meters/{id}     → Detalle de medidor               │  │
│  │  • GET  /metrics         → Métricas del sistema             │  │
│  │  • GET  /alarms          → Alarmas activas                  │  │
│  │  • GET  /health          → Estado de salud                  │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                              ↓ SQLAlchemy                           │
└─────────────────────────────────────────────────────────────────────┘
                               ↓
┌─────────────────────────────────────────────────────────────────────┐
│                        CAPA DE PERSISTENCIA                         │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  Base de Datos SQLite                                        │  │
│  │  data/admin.db                                               │  │
│  │                                                              │  │
│  │  Tablas:                                                     │  │
│  │  • meters          (Configuración de medidores)             │  │
│  │  • meter_configs   (Mediciones habilitadas)                 │  │
│  │  • meter_metrics   (Estadísticas de rendimiento)            │  │
│  │  • alarms          (Alarmas del sistema)                    │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                              ↑ ↓                                    │
└─────────────────────────────────────────────────────────────────────┘
                               ↑ ↓
┌─────────────────────────────────────────────────────────────────────┐
│                        CAPA DE SERVICIO CORE                        │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  Multi-Meter Bridge Service                                  │  │
│  │  dlms_multi_meter_bridge.py                                  │  │
│  │                                                              │  │
│  │  ┌────────────────────────────────────────────────────────┐ │  │
│  │  │  MultiMeterBridge (Coordinador Principal)             │ │  │
│  │  │  • Lee configuración desde DB                          │ │  │
│  │  │  • Inicializa MQTT compartido                          │ │  │
│  │  │  • Crea workers por medidor                            │ │  │
│  │  │  • Monitor de salud (cada 60s)                         │ │  │
│  │  └────────────────────────────────────────────────────────┘ │  │
│  │                                                              │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │  │
│  │  │  Worker 1   │  │  Worker 2   │  │  Worker N   │         │  │
│  │  │  (async)    │  │  (async)    │  │  (async)    │         │  │
│  │  │             │  │             │  │             │         │  │
│  │  │ Medidor 1   │  │ Medidor 2   │  │ Medidor N   │         │  │
│  │  │ 192.168.1.X │  │ 192.168.1.Y │  │ 192.168.1.Z │         │  │
│  │  └─────────────┘  └─────────────┘  └─────────────┘         │  │
│  │         ↓                 ↓                 ↓               │  │
│  │         └─────────────────┴─────────────────┘               │  │
│  │                           ↓                                 │  │
│  │                  Shared MQTT Client                         │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                              ↓ MQTT                                 │
└─────────────────────────────────────────────────────────────────────┘
                               ↓
┌─────────────────────────────────────────────────────────────────────┐
│                        CAPA DE INTEGRACIÓN IoT                      │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  ThingsBoard IoT Platform                                    │  │
│  │  localhost:1883 (MQTT Broker)                                │  │
│  │  • Recepción de telemetría                                   │  │
│  │  • Almacenamiento time-series                                │  │
│  │  • Visualización de dashboards                               │  │
│  │  • Reglas y alarmas                                          │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
                               ↑
┌─────────────────────────────────────────────────────────────────────┐
│                        CAPA DE DISPOSITIVOS                         │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  🔌 Medidores DLMS (Protocolo IEC 62056)                           │
│                                                                     │
│  [Medidor 1]         [Medidor 2]         [Medidor N]               │
│  192.168.1.127:3333  192.168.1.128:3333  192.168.1.X:3333         │
│                                                                     │
│  Mediciones disponibles por medidor:                                │
│  • voltage_l1, voltage_l2, voltage_l3    (Voltaje por fase)       │
│  • current_l1, current_l2, current_l3    (Corriente por fase)     │
│  • frequency                              (Frecuencia de red)      │
│  • active_power                           (Potencia activa)        │
│  • active_energy                          (Energía acumulada)      │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 🧩 Componentes del Sistema

### 1. Multi-Meter Bridge Service (Core)

**Archivo:** `dlms_multi_meter_bridge.py`  
**Función:** Servicio principal que coordina la lectura de múltiples medidores

#### Subcomponentes

```python
MultiMeterBridge (Clase Principal)
├── __init__()
│   ├─ Inicializa configuración
│   ├─ Conecta a base de datos
│   └─ Prepara estructuras de datos
│
├── setup_mqtt()
│   ├─ Crea cliente MQTT compartido
│   ├─ Configura callbacks
│   └─ Conecta a broker
│
├── load_meters_from_db()
│   ├─ Lee tabla 'meters'
│   ├─ Filtra medidores activos
│   └─ Prepara configuraciones
│
├── start_workers()
│   ├─ Crea MeterWorker por medidor
│   ├─ Inicia tasks asyncio
│   └─ Gestiona concurrencia
│
├── monitor_loop()
│   ├─ Reporta cada 60 segundos
│   ├─ Estadísticas por medidor
│   └─ Detección de problemas
│
└── run()
    └─ Loop principal del servicio

MeterWorker (Clase Worker)
├── create_poller()
│   └─ Inicializa ProductionDLMSPoller
│
├── poll_and_publish()
│   ├─ Lee datos del medidor (asyncio.to_thread)
│   ├─ Publica a MQTT si hay datos
│   └─ Maneja errores y estadísticas
│
└── get_stats()
    └─ Retorna métricas del worker
```

### 2. Production DLMS Poller

**Archivo:** `dlms_poller_production.py`  
**Función:** Cliente DLMS optimizado con auto-recuperación

#### Características

- **Caché de Scalers (Fase 2):** Reduce latencia 50%
- **Auto-recuperación:** Reconecta automáticamente ante fallos
- **Batch Reading:** Lee múltiples registros en una petición
- **Drenaje preventivo:** Limpia buffer cada 45 segundos
- **Circuit breaker:** Pausa ante fallos masivos

```python
ProductionDLMSPoller
├── _connect_with_recovery()
│   ├─ Múltiples intentos de conexión
│   ├─ Manejo de errores de frame
│   └─ Reset de secuencias DLMS
│
├── poll_once()
│   ├─ Lectura batch optimizada
│   ├─ Procesamiento de resultados
│   └─ Logging de métricas
│
└── run()
    ├─ Loop continuo de polling
    ├─ Drenaje preventivo periódico
    └─ Gestión de errores consecutivos
```

### 3. Optimized DLMS Reader

**Archivo:** `dlms_optimized_reader.py`  
**Función:** Capa de optimización sobre cliente DLMS básico

#### Fases de Optimización

```
Fase 1: Cliente Básico
└─ Lee valor + scaler por separado = 2 peticiones

Fase 2: Caché de Scalers ✅ ACTIVA
├─ Primera lectura: valor + scaler
├─ Cache el scaler
└─ Siguientes lecturas: solo valor = 50% más rápido

Fase 3: Batch Reading (Experimental)
└─ Lee múltiples registros en una petición
    (No soportado por todos los medidores)
```

### 4. REST API Backend

**Archivo:** `admin/api.py`  
**Framework:** FastAPI  
**Puerto:** 8000

#### Endpoints

```
GET  /health
├─ Status: 200 OK
└─ Response: {"status": "healthy"}

GET  /meters
├─ Lista todos los medidores
└─ Response: [{"id": 1, "name": "...", "status": "..."}]

POST /meters
├─ Crea nuevo medidor
├─ Body: {"name": "...", "ip_address": "...", "port": 3333}
└─ Response: {"id": 2, "name": "...", ...}

GET  /meters/{id}
├─ Detalle de medidor específico
└─ Response: {"id": 1, "name": "...", "configs": [...]}

GET  /metrics
├─ Métricas de rendimiento
└─ Response: {"total_reads": 1000, "success_rate": 99.5, ...}

GET  /alarms
├─ Alarmas activas
└─ Response: [{"id": 1, "severity": "warning", ...}]

GET  /config
├─ Configuración del sistema
└─ Response: {"mqtt_host": "localhost", ...}
```

### 5. Dashboard Web

**Archivo:** `admin/dashboard.py`  
**Framework:** Streamlit  
**Puerto:** 8501

#### Páginas

```
Home
├─ Resumen del sistema
├─ Medidores activos
└─ Gráficos de estado

Monitoring
├─ Datos en tiempo real
├─ Gráficos de tendencias
└─ Indicadores por medidor

Configuration
├─ Gestión de medidores
├─ Configuración de mediciones
└─ Parámetros de ThingsBoard

Alarms
├─ Lista de alarmas
├─ Filtros por severidad
└─ Reconocimiento de alarmas
```

### 6. Base de Datos

**Archivo:** `admin/database.py`  
**Motor:** SQLite  
**Ubicación:** `data/admin.db`

#### Schema

```sql
-- Tabla de medidores
CREATE TABLE meters (
    id INTEGER PRIMARY KEY,
    name VARCHAR(100) UNIQUE NOT NULL,
    ip_address VARCHAR(45) NOT NULL,
    port INTEGER DEFAULT 3333,
    status VARCHAR(20) DEFAULT 'inactive',
    tb_enabled BOOLEAN DEFAULT TRUE,
    tb_host VARCHAR(255) DEFAULT 'thingsboard.cloud',
    tb_port INTEGER DEFAULT 1883,
    tb_token VARCHAR(100),
    created_at DATETIME,
    updated_at DATETIME
);

-- Tabla de configuración de mediciones
CREATE TABLE meter_configs (
    id INTEGER PRIMARY KEY,
    meter_id INTEGER REFERENCES meters(id),
    measurement_name VARCHAR(50) NOT NULL,
    obis_code VARCHAR(20) NOT NULL,
    enabled BOOLEAN DEFAULT TRUE,
    tb_key VARCHAR(50)
);

-- Tabla de métricas de rendimiento
CREATE TABLE meter_metrics (
    id INTEGER PRIMARY KEY,
    meter_id INTEGER REFERENCES meters(id),
    timestamp DATETIME,
    avg_read_time FLOAT,
    total_reads INTEGER,
    successful_reads INTEGER,
    success_rate FLOAT,
    messages_sent INTEGER
);

-- Tabla de alarmas
CREATE TABLE alarms (
    id INTEGER PRIMARY KEY,
    meter_id INTEGER REFERENCES meters(id),
    severity VARCHAR(20) NOT NULL,
    category VARCHAR(50) NOT NULL,
    message TEXT NOT NULL,
    acknowledged BOOLEAN DEFAULT FALSE,
    timestamp DATETIME
);
```

---

## 🔄 Flujo de Datos

### Flujo Principal: Lectura y Publicación

```
┌─────────────────────────────────────────────────────────────────────┐
│ 1. INICIO DEL SISTEMA                                               │
└─────────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 2. MultiMeterBridge.run()                                           │
│    • Lee configuración desde data/admin.db                          │
│    • Encuentra N medidores habilitados                              │
│    • Conecta a MQTT broker (localhost:1883)                         │
└─────────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 3. Inicialización de Workers                                        │
│    Para cada medidor:                                               │
│    • Crea MeterWorker(meter_id, config, mqtt_client)               │
│    • Worker.create_poller() → ProductionDLMSPoller                  │
│    • Worker._connect_with_recovery() → Conecta a medidor DLMS      │
│    • Worker.poll_and_publish() → Inicia loop asíncrono             │
└─────────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 4. LOOP PRINCIPAL (por cada worker, concurrente)                    │
└─────────────────────────────────────────────────────────────────────┘
          ↓                           ↓                        ↓
    ┌─────────┐              ┌─────────────┐         ┌─────────────┐
    │Worker 1 │              │  Worker 2   │         │  Worker N   │
    └─────────┘              └─────────────┘         └─────────────┘
          ↓                           ↓                        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 5. Ciclo de Lectura (cada worker independiente)                    │
│                                                                     │
│  a) await asyncio.to_thread(poller.poll_once)                      │
│     ├─ ProductionDLMSPoller.poll_once()                            │
│     ├─ OptimizedDLMSReader.read_multiple_registers()               │
│     │  ├─ Batch request DLMS                                       │
│     │  ├─ [voltage_l1, current_l1, frequency, power, energy]      │
│     │  └─ Usa caché de scalers (50% más rápido)                   │
│     └─ Retorna: {"voltage_l1": 137.5, "current_l1": 1.34, ...}    │
│                                                                     │
│  b) Procesar readings                                              │
│     ├─ total_cycles += 1                                           │
│     └─ if readings: successful_cycles += 1                         │
│                                                                     │
│  c) Publicar a MQTT                                                │
│     ├─ if mqtt_client.is_connected():                              │
│     ├─   topic = "v1/devices/me/telemetry"                         │
│     ├─   payload = '{"voltage_l1": 137.5, ...}'                    │
│     ├─   mqtt_client.publish(topic, payload, qos=1)                │
│     └─   total_messages_sent += 1                                  │
│                                                                     │
│  d) await asyncio.sleep(interval)  # Default: 1.0s                 │
│                                                                     │
│  e) Repetir desde (a)                                              │
└─────────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 6. Monitor Loop (paralelo cada 60s)                                │
│    • Recolecta stats de todos los workers                          │
│    • Log: Cycles, Success Rate, MQTT msgs, Runtime                 │
│    • Detecta anomalías                                             │
└─────────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 7. ThingsBoard                                                      │
│    • Recibe telemetría vía MQTT                                    │
│    • Almacena en time-series DB                                    │
│    • Actualiza dashboards en tiempo real                           │
│    • Ejecuta reglas y alarmas                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### Flujo de Configuración (Dashboard → DB → Service)

```
[Usuario en Dashboard]
         ↓
[Modifica configuración]
         ↓
[API POST/PUT request]
         ↓
[SQLite admin.db actualizado]
         ↓
[Reiniciar servicio] ← Manual: sudo systemctl restart dlms-multi-meter
         ↓
[Service lee nueva config]
         ↓
[Aplica cambios]
```

---

## ⚙️ Servicios SystemD

### 1. dlms-multi-meter.service

**Archivo:** `/etc/systemd/system/dlms-multi-meter.service`

```ini
[Unit]
Description=DLMS Multi-Meter Bridge Service (Scalable)
After=network.target
Documentation=https://github.com/jsebgiraldo/Tesis-app

[Service]
Type=simple
User=pci
WorkingDirectory=/home/pci/Documents/sebas_giraldo/Tesis-app/dlms-bridge
Environment="PATH=/home/pci/.../venv/bin:/usr/bin:/bin"
ExecStart=/home/pci/.../venv/bin/python3 dlms_multi_meter_bridge.py --db-path data/admin.db

# Restart behavior
Restart=always
RestartSec=10

# Resource limits
MemoryMax=500M
CPUQuota=80%

# Logging
StandardOutput=journal
StandardError=journal
SyslogIdentifier=dlms-multi-meter

[Install]
WantedBy=multi-user.target
```

**Estado Actual:** ✅ Activo y Habilitado

### 2. dlms-admin-api.service

**Archivo:** `/etc/systemd/system/dlms-admin-api.service`

```ini
[Unit]
Description=DLMS Admin API - Dashboard Backend Service
After=network.target

[Service]
Type=simple
User=pci
WorkingDirectory=/home/pci/Documents/sebas_giraldo/Tesis-app/dlms-bridge
Environment="PATH=/home/pci/.../venv/bin:/usr/bin:/bin"
ExecStart=/home/pci/.../venv/bin/uvicorn admin.api:app --host 0.0.0.0 --port 8000

Restart=always
RestartSec=10

StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

**Estado Actual:** ⚠️ Detenido (causa conflicto MQTT)

### 3. dlms-dashboard.service

**Archivo:** `/etc/systemd/system/dlms-dashboard.service`

```ini
[Unit]
Description=DLMS Dashboard - Streamlit Web Interface
After=network.target

[Service]
Type=simple
User=pci
WorkingDirectory=/home/pci/Documents/sebas_giraldo/Tesis-app/dlms-bridge
Environment="PATH=/home/pci/.../venv/bin:/usr/bin:/bin"
ExecStart=/home/pci/.../venv/bin/streamlit run admin/dashboard.py --server.port 8501

Restart=always
RestartSec=10

StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

**Estado Actual:** ✅ Activo y Habilitado

---

## 💾 Base de Datos

### Modelo de Datos

```
meters (Medidores)
│
├─ PK: id
├─ name (único)
├─ ip_address
├─ port
├─ status (active/inactive/error)
├─ tb_enabled
├─ tb_host
├─ tb_port
├─ tb_token
└─ timestamps
     │
     ├───> meter_configs (1:N)
     │     ├─ PK: id
     │     ├─ FK: meter_id
     │     ├─ measurement_name
     │     ├─ obis_code
     │     ├─ enabled
     │     └─ tb_key (opcional)
     │
     ├───> meter_metrics (1:N)
     │     ├─ PK: id
     │     ├─ FK: meter_id
     │     ├─ timestamp
     │     ├─ avg_read_time
     │     ├─ total_reads
     │     ├─ success_rate
     │     └─ messages_sent
     │
     └───> alarms (1:N)
           ├─ PK: id
           ├─ FK: meter_id
           ├─ severity
           ├─ category
           ├─ message
           ├─ acknowledged
           └─ timestamp
```

### Mediciones DLMS Disponibles

| Medición | OBIS Code | Descripción | Unidad |
|----------|-----------|-------------|--------|
| voltage_l1 | 1-1:32.7.0 | Voltaje Fase A | V |
| voltage_l2 | 1-1:52.7.0 | Voltaje Fase B | V |
| voltage_l3 | 1-1:72.7.0 | Voltaje Fase C | V |
| current_l1 | 1-1:31.7.0 | Corriente Fase A | A |
| current_l2 | 1-1:51.7.0 | Corriente Fase B | A |
| current_l3 | 1-1:71.7.0 | Corriente Fase C | A |
| frequency | 1-1:14.7.0 | Frecuencia de red | Hz |
| active_power | 1-1:1.7.0 | Potencia activa | W |
| active_energy | 1-1:1.8.0 | Energía activa acumulada | Wh |

---

## 📡 Protocolos y Comunicaciones

### 1. DLMS/COSEM (IEC 62056)

**Puerto:** TCP 3333  
**Protocolo:** HDLC sobre TCP/IP  
**Cliente:** Optimizado con caché

```
Handshake DLMS:
1. SNRM (Set Normal Response Mode)
   ├─ Cliente → Medidor
   └─ TX: 7E A0 07 03 03 93 8C 11 7E

2. UA (Unnumbered Acknowledgment)
   ├─ Medidor → Cliente
   └─ RX: 7E A0 1E 03 03 73 40 CC ... 7E

3. AARQ (Application Association Request)
   ├─ Cliente → Medidor (con contraseña)
   └─ TX: 7E A0 44 03 03 10 65 94 E6 ... 7E

4. AARE (Application Association Response)
   ├─ Medidor → Cliente (autenticación OK)
   └─ RX: 7E A0 37 03 03 30 EF CA E6 ... 7E

5. GET Request (lectura de registros)
   ├─ Cliente → Medidor
   └─ TX: 7E A0 19 03 03 12 EE E9 ... 7E

6. GET Response (datos + scaler)
   ├─ Medidor → Cliente
   └─ RX: 7E A0 16 03 03 52 13 19 ... 7E

7. DISC (Disconnect)
   ├─ Cliente → Medidor
   └─ TX: 7E A0 07 03 03 53 80 D7 7E
```

### 2. MQTT (Message Queue Telemetry Transport)

**Broker:** localhost:1883  
**Protocolo:** MQTT v3.1.1  
**QoS:** 1 (At least once)

```
Topic: v1/devices/me/telemetry
Payload Format: JSON

Ejemplo:
{
  "voltage_l1": 137.68,
  "current_l1": 1.34,
  "frequency": 59.97,
  "active_power": 0.60,
  "active_energy": 56295.00
}

Autenticación: Token en username
Token: QrKMI1jxYkK8hnDm3OD4
```

### 3. HTTP REST API

**Puerto:** 8000  
**Protocolo:** HTTP/1.1  
**Formato:** JSON

```
Headers:
Content-Type: application/json
Accept: application/json

Ejemplo Request:
POST /meters
{
  "name": "Medidor_Nuevo",
  "ip_address": "192.168.1.128",
  "port": 3333
}

Ejemplo Response:
200 OK
{
  "id": 2,
  "name": "Medidor_Nuevo",
  "ip_address": "192.168.1.128",
  "port": 3333,
  "status": "inactive"
}
```

---

## 🔐 Seguridad

### Autenticación DLMS

```
Método: Low Level Security (LLS)
Password: "22222222" (8 caracteres ASCII)
Nivel: Cliente SAP 1 → Servidor Logical 0, Physical 1
```

### Autenticación ThingsBoard

```
Método: Token-based
Token: QrKMI1jxYkK8hnDm3OD4
Ubicación: MQTT username field
```

### Consideraciones

- ⚠️ Password DLMS en texto plano en configuración
- ⚠️ Token MQTT sin cifrado (localhost)
- ✅ Base de datos local (no expuesta)
- ⚠️ API sin autenticación (solo localhost)

---

## 📈 Escalabilidad

### Capacidad Actual

```
Configuración Actual:
├─ 1 medidor activo
├─ 34 MB RAM utilizada
├─ Lectura cada 1-2 segundos
└─ 100% tasa de éxito

Capacidad Estimada:
├─ 10 medidores:  ~150 MB RAM
├─ 50 medidores:  ~250 MB RAM
├─ 100 medidores: ~400 MB RAM
└─ Límite systemd: 500 MB
```

### Arquitectura Escalable

```
Ventajas del diseño actual:

1. Un solo proceso Python
   ├─ Memoria compartida
   ├─ Conexión MQTT única
   └─ Gestión centralizada

2. Workers asíncronos
   ├─ Concurrencia sin threads
   ├─ asyncio event loop
   └─ No GIL contention

3. Configuración dinámica
   ├─ Agregar medidor = INSERT en DB
   ├─ Reiniciar servicio
   └─ Automático scaling

4. Pooling de recursos
   ├─ MQTT client compartido
   ├─ Database session pool
   └─ Optimizaciones compartidas
```

### Comparación: Antes vs Ahora

| Aspecto | Antes (1 servicio/medidor) | Ahora (Multi-meter) |
|---------|----------------------------|---------------------|
| 10 medidores | 800 MB RAM | 150 MB RAM |
| Servicios systemd | 10 | 1 |
| Conexiones MQTT | 10 | 1 |
| Complejidad | Alta | Baja |
| Tiempo deploy | 10 min | 1 min |
| Escalabilidad | Limitada | Hasta 100+ |

---

## 📊 Monitoreo y Logs

### SystemD Journalctl

```bash
# Ver logs en tiempo real
sudo journalctl -u dlms-multi-meter.service -f

# Últimos 100 logs
sudo journalctl -u dlms-multi-meter.service -n 100

# Filtrar por tiempo
sudo journalctl -u dlms-multi-meter.service --since "1 hour ago"

# Filtrar por medidor específico
sudo journalctl -u dlms-multi-meter.service | grep "Meter\[1:"

# Ver solo errores
sudo journalctl -u dlms-multi-meter.service -p err
```

### Service Manager Script

```bash
# Ver polling en tiempo real
./service-manager.sh watch

# Ver estadísticas cada 60s
./service-manager.sh stats

# Ver logs de medidor específico
./service-manager.sh meter 1

# Estado de todos los servicios
./service-manager.sh all
```

### Formato de Logs

```
Lecturas del medidor:
[2025-10-31 16:50:40] | V: 137.53 V | C: 1.34 A | F: 60.01 Hz | A: 0.60 W | A: 56295.00 Wh | (1.895s)

Reportes cada 10 ciclos:
[2025-10-31 16:49:14] 📊 Cycles: 10 | Success: 100.0% | MQTT: 10 msgs

Reportes del sistema (cada 60s):
[2025-10-31 16:49:45] 📊 SYSTEM STATUS REPORT
[2025-10-31 16:49:45]   Meter 1 (medidor_dlms_principal): Cycles=16, Success=100.0%, MQTT=16, Runtime=65s
```

---

## 🚀 Deployment

### Instalación Inicial

```bash
# 1. Clonar repositorio
git clone https://github.com/jsebgiraldo/Tesis-app.git
cd Tesis-app/dlms-bridge

# 2. Crear entorno virtual
python3 -m venv venv
source venv/bin/activate

# 3. Instalar dependencias
pip install -r requirements.txt

# 4. Inicializar base de datos
python3 admin/database.py

# 5. Configurar primer medidor
python3 << EOF
from admin.database import Database, create_meter
db = Database('data/admin.db')
db.initialize()
with db.get_session() as session:
    meter = create_meter(
        session,
        name="medidor_dlms_principal",
        ip_address="192.168.1.127",
        port=3333
    )
    print(f"Creado: {meter}")
EOF

# 6. Instalar servicios
sudo cp /tmp/dlms-multi-meter.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable dlms-multi-meter.service
sudo systemctl start dlms-multi-meter.service
```

### Actualización

```bash
# 1. Detener servicios
sudo systemctl stop dlms-multi-meter.service

# 2. Actualizar código
git pull origin main

# 3. Actualizar dependencias
source venv/bin/activate
pip install -r requirements.txt --upgrade

# 4. Reiniciar servicios
sudo systemctl start dlms-multi-meter.service

# 5. Verificar
sudo systemctl status dlms-multi-meter.service
```

---

## 🔧 Troubleshooting

### Problemas Comunes

#### 1. Conflicto MQTT (Código 7: NOT_AUTHORIZED)

**Síntoma:**
```
⚠️ MQTT Disconnected: 7
✅ MQTT Connected
⚠️ MQTT Disconnected: 7
```

**Causa:** Dos servicios usando mismo token MQTT

**Solución:**
```bash
sudo systemctl stop dlms-admin-api.service
sudo systemctl restart dlms-multi-meter.service
```

#### 2. Medidor no responde

**Síntoma:**
```
❌ Failed to connect to meter
⚠️ No readings returned
```

**Diagnóstico:**
```bash
# Test de ping
ping 192.168.1.127

# Test directo
python3 test_meter_health.py

# Ver logs detallados
sudo journalctl -u dlms-multi-meter.service -f
```

#### 3. Buffer TCP sucio

**Síntoma:**
```
Invalid HDLC frame boundary
Checksum mismatch
```

**Solución automática:**
- Drenaje preventivo cada 45s
- Auto-limpieza en reconnect

**Solución manual:**
```bash
sudo systemctl restart dlms-multi-meter.service
```

#### 4. Base de datos bloqueada

**Síntoma:**
```
database is locked
```

**Solución:**
```bash
# Cerrar procesos que usan la DB
sudo systemctl stop dlms-admin-api.service
sudo systemctl stop dlms-dashboard.service

# Reiniciar servicio principal
sudo systemctl restart dlms-multi-meter.service
```

---

## 📚 Referencias y Documentación

### Documentos del Proyecto

- `COMANDOS_RAPIDOS.md` - Guía de comandos
- `docs/SCALABILITY_COMPARISON.md` - Comparación de arquitecturas
- `README.md` - Documentación general
- `service-manager.sh` - Script de gestión

### Protocolos

- IEC 62056 (DLMS/COSEM)
- MQTT v3.1.1
- HTTP/REST

### Tecnologías

- Python 3.12
- SQLite
- FastAPI
- Streamlit
- SystemD

---

## 📝 Changelog

### Versión 2.0 (31/10/2025)

- ✅ Migración a arquitectura escalable
- ✅ Multi-meter bridge service
- ✅ Workers asíncronos
- ✅ MQTT compartido
- ✅ Optimización con caché (50% más rápido)
- ✅ Dashboard web integrado
- ✅ Base de datos centralizada

### Versión 1.0 (Anterior)

- ⚠️ Un servicio por medidor
- ⚠️ No escalable
- ⚠️ Alto consumo de recursos

---

**Última actualización:** 31 de Octubre de 2025  
**Mantenedor:** Sebastian Giraldo  
**Repositorio:** https://github.com/jsebgiraldo/Tesis-app
