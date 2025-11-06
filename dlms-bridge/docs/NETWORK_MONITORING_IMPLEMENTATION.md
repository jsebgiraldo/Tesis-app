# Network Monitoring Implementation

**Fecha**: 2025-10-31  
**Autor**: AI Assistant  
**Estado**: ✅ Completado y funcional

## 📋 Resumen Ejecutivo

Se implementó un sistema completo de monitoreo de red que captura y visualiza estadísticas de consumo de red a nivel de protocolo DLMS y MQTT. El sistema rastrea bandwidth, payload sizes, throughput, y data consumption en tiempo real.

## 🎯 Objetivos Cumplidos

✅ Capturar estadísticas de red en tiempo real  
✅ Rastrear bytes enviados/recibidos por protocolo (DLMS/MQTT)  
✅ Calcular bandwidth y velocidad de transferencia  
✅ Mostrar tamaño promedio de payload  
✅ Visualizar consumo acumulado de datos  
✅ Gráficos históricos de bandwidth  
✅ Métricas de eficiencia de red

## 🏗️ Arquitectura Implementada

### 1. **Módulo de Monitoreo de Red** (`network_monitor.py`)

```python
class NetworkMonitor:
    - Captura estadísticas de interfaz de red (psutil)
    - Tracking de aplicación (DLMS/MQTT)
    - Historial de métricas (time series)
    - Cálculo de bandwidth/throughput
```

**Métricas capturadas:**
- Bandwidth TX/RX (bps, Kbps, Mbps)
- Packets per second (TX/RX)
- DLMS requests/responses sent/received
- DLMS bytes sent/received
- DLMS average payload size
- MQTT messages sent
- MQTT bytes sent

### 2. **Integración en DLMS Bridge**

**Tracking de DLMS (`dlms_reader.py`):**
```python
def _send_frame(self, frame: bytes):
    # Envía frame DLMS
    self._sock.sendall(frame)
    # Registra bytes enviados
    monitor.record_dlms_request(len(frame))

def _read_frame(self):
    # Lee respuesta DLMS
    frame = bytes(buffer)
    # Registra bytes recibidos
    monitor.record_dlms_response(len(frame))
    return frame
```

**Tracking de MQTT (`dlms_multi_meter_bridge.py`):**
```python
mqtt_bytes = len(payload.encode('utf-8'))
result = self.mqtt_client.publish(topic, payload, qos=1)
if result.rc == mqtt.MQTT_ERR_SUCCESS:
    network_monitor.record_mqtt_message(mqtt_bytes)
```

### 3. **Persistencia en Base de Datos**

**Nueva tabla `network_metrics`:**
```sql
CREATE TABLE network_metrics (
    id INTEGER PRIMARY KEY,
    meter_id INTEGER,
    timestamp DATETIME,
    dlms_requests_sent INTEGER,
    dlms_responses_recv INTEGER,
    dlms_bytes_sent INTEGER,
    dlms_bytes_recv INTEGER,
    dlms_avg_payload_size FLOAT,
    mqtt_messages_sent INTEGER,
    mqtt_bytes_sent INTEGER,
    bandwidth_tx_bps FLOAT,
    bandwidth_rx_bps FLOAT,
    packets_tx_ps FLOAT,
    packets_rx_ps FLOAT
)
```

**Guardado periódico (cada 60 segundos):**
```python
async def monitor_loop(self):
    while self.running:
        await asyncio.sleep(60)
        # Captura stats del network_monitor
        current_stats = network_monitor.get_current_stats()
        # Guarda en DB
        record_network_metric(session, meter_id, ...)
```

### 4. **API REST Endpoint**

**Endpoint**: `GET /meters/{meter_id}/network_stats`

**Response:**
```json
{
  "meter_id": 1,
  "meter_name": "medidor_dlms_principal",
  "timestamp": "2025-10-31T22:05:00",
  "current": {
    "bandwidth_tx_kbps": 10.5,
    "bandwidth_rx_kbps": 8.2,
    "bandwidth_total_kbps": 18.7,
    "packets_tx_ps": 12.3,
    "packets_rx_ps": 11.8
  },
  "application": {
    "dlms_requests_sent": 278,
    "dlms_responses_recv": 275,
    "dlms_avg_payload_size": 27.4,
    "dlms_total_bytes_sent": 7606,
    "dlms_total_bytes_recv": 6592,
    "mqtt_messages_sent": 47,
    "mqtt_total_bytes_sent": 5020
  },
  "averages": {
    "bandwidth_tx_mbps": 0.012,
    "bandwidth_rx_mbps": 0.009
  },
  "peaks": {
    "bandwidth_tx_mbps": 0.156,
    "bandwidth_rx_mbps": 0.142
  },
  "history": {
    "timestamp": ["2025-10-31T21:00:00", ...],
    "bandwidth_tx_mbps": [0.010, 0.015, ...],
    "bandwidth_rx_mbps": [0.008, 0.012, ...],
    "packets_tx_ps": [10.5, 12.3, ...],
    "packets_rx_ps": [9.8, 11.2, ...]
  }
}
```

### 5. **Dashboard de Monitoreo**

**Ubicación**: Dashboard → 📊 Monitoring → 📡 Network Statistics

**Secciones del Dashboard:**

#### A. Real-Time Bandwidth
- Upload Speed (KB/s)
- Download Speed (KB/s)
- Total Bandwidth (KB/s)
- Packets/sec

#### B. DLMS Protocol Statistics
- DLMS Requests
- DLMS Responses
- Avg Payload Size
- MQTT Messages

#### C. Data Consumption
- DLMS Data Sent
- DLMS Data Received
- MQTT Data Sent
- Total Data

#### D. Network Efficiency
- Avg Upload (Mbps)
- Avg Download (Mbps)
- Peak Upload (Mbps)
- Peak Download (Mbps)

#### E. Network Issues (si aplica)
- Network Errors
- Packet Drops

#### F. Gráficos Históricos
- **Bandwidth History**: Upload/Download over time (line chart)
- **Packet Rate**: TX/RX packets per second (line chart)

## 📊 Ejemplo de Métricas Reales

**Capturadas durante testing:**

```
DLMS Protocol:
- Requests: 278
- Responses: 275
- Bytes Sent: 7,606 bytes (7.4 KB)
- Bytes Received: 6,592 bytes (6.4 KB)
- Avg Payload: 27.4 bytes

MQTT Protocol:
- Messages: 47
- Bytes Sent: 5,020 bytes (4.9 KB)
- Avg Message Size: 106.8 bytes

Bandwidth:
- TX: ~10 Kbps
- RX: ~8 Kbps
- Total: ~18 Kbps
```

## 🔧 Archivos Modificados/Creados

### Nuevos Archivos:
1. `network_monitor.py` - Módulo de monitoreo (336 líneas)
2. `docs/NETWORK_MONITORING_IMPLEMENTATION.md` - Este documento

### Archivos Modificados:
1. `dlms_reader.py`
   - `_send_frame()`: Tracking de bytes enviados
   - `_read_frame()`: Tracking de bytes recibidos

2. `dlms_multi_meter_bridge.py`
   - Import de `network_monitor`
   - Tracking de MQTT en `MeterWorker.poll_loop()`
   - Guardado de métricas en `monitor_loop()`

3. `admin/database.py`
   - Nueva tabla `NetworkMetric`
   - Función `record_network_metric()`

4. `admin/api.py`
   - Endpoint `GET /meters/{meter_id}/network_stats`
   - Lee desde DB en lugar de memoria

5. `admin/dashboard.py`
   - Nueva sección "📡 Network Statistics" en Monitoring page
   - 4 bloques de métricas
   - 2 gráficos históricos (bandwidth, packets)

## 🚀 Deployment

**Servicios actualizados:**
```bash
# DLMS Bridge (captura métricas)
sudo systemctl restart dlms-multi-meter.service
PID: 1746166
Status: ✅ Running

# API (sirve métricas)
sudo systemctl restart dlms-admin-api.service
PID: 1746149
Status: ✅ Running

# Dashboard (visualiza métricas)
pkill -f streamlit && streamlit run admin/dashboard.py
PID: 1752869
Status: ✅ Running
URL: http://0.0.0.0:8501
```

## 📈 Flujo de Datos

```
┌─────────────────┐
│ Medidor DLMS    │
│ 192.168.1.127   │
└────────┬────────┘
         │ DLMS frames
         ↓
┌─────────────────────────────┐
│ dlms_reader.py              │
│ - _send_frame()   →  Track  │ ← network_monitor.record_dlms_request()
│ - _read_frame()   →  Track  │ ← network_monitor.record_dlms_response()
└──────────┬──────────────────┘
           │ Readings
           ↓
┌─────────────────────────────┐
│ dlms_multi_meter_bridge.py  │
│ - poll_loop()     →  Track  │ ← network_monitor.record_mqtt_message()
│ - monitor_loop()  →  Save   │ → record_network_metric(DB)
└──────────┬──────────────────┘
           │ MQTT telemetry
           ↓
┌─────────────────┐
│ ThingsBoard     │
│ MQTT Broker     │
└─────────────────┘

           ↓ (DB storage)
┌──────────────────────────────┐
│ admin.db (SQLite)            │
│ Table: network_metrics       │
│ - dlms_requests_sent         │
│ - mqtt_messages_sent         │
│ - bandwidth_tx_bps           │
│ - ...                        │
└──────────┬───────────────────┘
           │ (API query)
           ↓
┌──────────────────────────────┐
│ FastAPI (:8000)              │
│ GET /meters/1/network_stats  │
└──────────┬───────────────────┘
           │ (HTTP request)
           ↓
┌──────────────────────────────┐
│ Streamlit Dashboard (:8501)  │
│ Page: 📊 Monitoring          │
│ Section: 📡 Network Stats    │
│ - Metrics display            │
│ - Historical charts          │
└──────────────────────────────┘
```

## ⚙️ Configuración

### Intervalo de Muestreo
- **Network monitor background loop**: 1.0 segundos
- **Guardado en DB**: 60 segundos
- **Polling DLMS**: 3.0 segundos

### Retención de Datos
- **Network monitor history**: 300 muestras (5 minutos @ 1s)
- **Database history**: Ilimitado (se puede agregar limpieza periódica)
- **API default limit**: 100 registros más recientes

## 🧪 Testing

**Verificar tracking DLMS:**
```python
# En logs del servicio
journalctl -u dlms-multi-meter.service -f | grep "Published + tracked"
# Output: 📤 Published + tracked: 110 bytes MQTT (total now: 47 msgs)
```

**Verificar guardado en DB:**
```python
python3 -c "
import sqlite3
conn = sqlite3.connect('data/admin.db')
rows = conn.execute('SELECT * FROM network_metrics ORDER BY timestamp DESC LIMIT 1').fetchall()
print(rows)
"
```

**Verificar API:**
```bash
curl -s http://localhost:8000/meters/1/network_stats | jq '.application'
```

**Verificar Dashboard:**
1. Abrir http://localhost:8501
2. Navegar a "📊 Monitoring"
3. Seleccionar un medidor
4. Scroll down hasta "📡 Network Statistics"
5. Verificar métricas en tiempo real y gráficos

## 📝 Notas Técnicas

### 1. **Singleton Pattern**
El `NetworkMonitor` usa un patrón singleton para compartir la misma instancia dentro del mismo proceso:
```python
_network_monitor: Optional[NetworkMonitor] = None

def get_network_monitor() -> NetworkMonitor:
    global _network_monitor
    if _network_monitor is None:
        _network_monitor = NetworkMonitor()
        _network_monitor.start_monitoring()
    return _network_monitor
```

### 2. **Inter-Process Communication**
Como el servicio DLMS y el API corren en procesos separados, NO pueden compartir memoria. Solución: **Base de datos como intermediario**.

### 3. **Overhead de Tracking**
El overhead de registrar cada frame DLMS es mínimo:
```python
try:
    monitor.record_dlms_request(len(frame))
except Exception:
    pass  # No falla si monitor no disponible
```

### 4. **Precision de Bandwidth**
- System-level bandwidth: Capturado por `psutil` (preciso, incluye TODO el tráfico)
- Application-level: Solo DLMS/MQTT (más relevante para análisis)

## 🐛 Debugging

**Si las métricas están en cero:**
1. Verificar que el servicio DLMS esté corriendo: `systemctl status dlms-multi-meter.service`
2. Verificar logs de tracking: `journalctl -u dlms-multi-meter.service | grep "Published + tracked"`
3. Verificar que la DB tenga datos: `python3 -c "import sqlite3; print(sqlite3.connect('data/admin.db').execute('SELECT COUNT(*) FROM network_metrics').fetchone())"`
4. Esperar al menos 60 segundos para que monitor_loop ejecute

**Si el dashboard no muestra datos:**
1. Verificar que el API responde: `curl http://localhost:8000/meters/1/network_stats`
2. Verificar logs del dashboard: `tail -f logs/dashboard.log`
3. Refrescar la página del navegador
4. Verificar que no haya errores en la consola del navegador (F12)

## ✅ Checklist de Validación

- [x] `network_monitor.py` creado y funcional
- [x] Tracking de DLMS en `dlms_reader.py`
- [x] Tracking de MQTT en `dlms_multi_meter_bridge.py`
- [x] Tabla `network_metrics` en base de datos
- [x] Función `record_network_metric()` implementada
- [x] Monitor loop guarda métricas cada 60s
- [x] Endpoint API `/meters/{meter_id}/network_stats` funcional
- [x] Dashboard muestra sección "📡 Network Statistics"
- [x] Métricas en tiempo real (bandwidth, packets)
- [x] Estadísticas de protocolo (DLMS, MQTT)
- [x] Consumo de datos (bytes sent/received)
- [x] Eficiencia de red (averages, peaks)
- [x] Gráficos históricos (bandwidth over time)
- [x] Gráficos de packet rate
- [x] Testing exitoso con datos reales
- [x] Servicios corriendo estables

## 🎉 Resultados

**Sistema completamente funcional** que captura, almacena y visualiza estadísticas de red en tiempo real, cumpliendo todos los objetivos del usuario:

> "estadisticas de red cuanta red esta consumiendo cuanta velocidad cuanto payload velocidad datos de red y mas datos que nos peudan interesar"

✅ **Velocidad**: Bandwidth TX/RX en Kbps y Mbps  
✅ **Payload**: Tamaño promedio de mensajes DLMS (27.4 bytes) y MQTT (106.8 bytes)  
✅ **Consumo**: Bytes totales enviados/recibidos por protocolo  
✅ **Velocidad de datos**: Packets/second, bandwidth en tiempo real  
✅ **Datos adicionales**: Eficiencia, peaks, histogramas, trends

---

**Documento generado**: 2025-10-31 22:05:00  
**Sistema**: DLMS Multi-Meter Bridge v1.0  
**Status**: ✅ Production Ready
