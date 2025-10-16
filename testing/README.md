# Testing y Validación

Suite completa de tests para validar conformidad, performance e interoperabilidad de la arquitectura IoT.

## 📂 Estructura

```
testing/
├── integration/              # Tests end-to-end (E2E)
│   ├── end_to_end_lwm2m.py
│   ├── mqtt_broker_load.py
│   └── gateway_failover.py
│
├── performance/              # Benchmarks
│   ├── latency_benchmarks/
│   │   ├── e2e_latency.py
│   │   └── compare_protocols.py
│   ├── throughput_tests/
│   │   └── max_throughput.py
│   └── power_consumption/
│       └── energy_profiler.py
│
├── interoperability/         # Tests multi-vendor
│   ├── multi_vendor_lwm2m.py
│   └── protocol_coexistence.py
│
└── compliance/               # Validación estándares
    ├── iso30141_validator.py
    ├── lwm2m_conformance_tests.py
    └── test_suites/
```

## 🧪 Tests Principales

### 1. Conformidad ISO/IEC 30141:2024

**Script**: `compliance/iso30141_validator.py`

Valida que la arquitectura cumple con los requisitos ISO 30141.

**Viewpoints validados**:
- ✅ Business: ROI, interoperabilidad
- ✅ Functional: Device management, telemetría
- ✅ Information: Modelos datos (IPSO, SenML)
- ✅ Communication: Protocolos, seguridad
- ✅ Deployment: Aprovisionamiento, orquestación
- ✅ Security: DTLS/TLS, autenticación
- ✅ Privacy: GDPR compliance

**Ejecutar**:
```bash
cd testing/compliance
python3 -m venv venv
source venv/bin/activate
pip install -r ../../requirements.txt

# Validar nodo específico
python iso30141_validator.py --node ESP32C6-ABC123

# Generar reporte PDF
python iso30141_validator.py --node ESP32C6-ABC123 --generate-report

# Output: iso30141_compliance_report.pdf
```

**Checklist manual** (complemento):
```bash
# Revisar matriz Excel
libreoffice ../../docs/thesis/compliance/iso30141_matrix.xlsx
```

---

### 2. Conformidad LwM2M

**Script**: `compliance/lwm2m_conformance_tests.py`

Tests de conformidad OMA LwM2M v1.2.

**Tests incluidos**:
- ✅ Register/Update/Deregister
- ✅ Read/Write/Execute operations
- ✅ Observe/Notify mechanism
- ✅ Bootstrap sequence
- ✅ Firmware Update (FOTA)
- ✅ Queue Mode
- ✅ DTLS handshake

**Ejecutar**:
```bash
cd testing/compliance

# Suite completa
pytest lwm2m_conformance_tests.py -v

# Test específico
pytest lwm2m_conformance_tests.py::test_register -v

# Con reporte HTML
pytest lwm2m_conformance_tests.py --html=report.html --self-contained-html
```

**Configuración**:
```python
# compliance/config.py
LESHAN_SERVER = "http://localhost:8081"
DEVICE_ENDPOINT = "ESP32C6-ABC123"
```

---

### 3. Performance: Latencia E2E

**Script**: `performance/latency_benchmarks/e2e_latency.py`

Mide latencia end-to-end desde sensor hasta dashboard.

**Métricas**:
- Percentiles: p50, p90, p95, p99
- Jitter
- Timeouts
- Packet loss

**Ejecutar**:
```bash
cd testing/performance/latency_benchmarks

# LwM2M
python e2e_latency.py --protocol lwm2m --samples 1000 --interval 1.0

# MQTT
python e2e_latency.py --protocol mqtt --samples 1000 --interval 1.0

# Comparación
./compare_protocols.sh --lwm2m-nodes 10 --mqtt-nodes 10 --duration 3600
```

**Output**:
```
Protocol: LwM2M (CoAP/UDP)
Samples: 1000
---------------------------------------
p50:  45.3 ms
p90:  78.2 ms
p95:  92.1 ms
p99: 125.4 ms
Jitter: 12.3 ms
Loss: 0.2%
```

**Resultados guardados en**: `data/experiments/YYYY_MM_latency_test/`

---

### 4. Performance: Throughput Máximo

**Script**: `performance/throughput_tests/max_throughput.py`

Determina throughput máximo sostenible.

**Variables**:
- Número de nodos concurrentes
- Tamaño de payload
- Frecuencia de publicación
- QoS (MQTT) / CON/NON (CoAP)

**Ejecutar**:
```bash
cd testing/performance/throughput_tests

python max_throughput.py \
  --protocol lwm2m \
  --nodes 50 \
  --payload-size 128 \
  --duration 600

# Output: Mensajes/segundo sostenidos, CPU, memoria
```

**Benchmark objetivo**:
- LwM2M: >800 msg/s (50 nodos, payload 128 bytes)
- MQTT: >600 msg/s (50 nodos, payload 256 bytes)

---

### 5. Performance: Consumo Energético

**Script**: `performance/power_consumption/energy_profiler.py`

Perfila consumo energético de dispositivos.

**Métodos**:
1. **Nordic Power Profiler Kit II** (hardware externo)
2. **ESP-IDF built-in** (estimación por logs)

**Ejecutar**:
```bash
cd testing/performance/power_consumption

# Con NPPK2 (recomendado)
python energy_profiler.py \
  --device /dev/ttyACM0 \
  --duration 3600 \
  --mode lwm2m

# Output: mAh totales, Wh, gráfico timeline
```

**Configuración dispositivo**:
```c
// Habilitar power profiling en ESP32-C6
idf.py menuconfig
// → Power Management → Enable dynamic frequency scaling
```

**Modos de operación analizados**:
- Active (transmitting)
- Active (idle)
- Light sleep
- Deep sleep
- Promedio 24h

---

### 6. Interoperabilidad Multi-Vendor

**Script**: `interoperability/multi_vendor_lwm2m.py`

Valida interoperabilidad con múltiples servidores LwM2M.

**Servidores testeados**:
- Eclipse Leshan 2.0
- AVSystem Coiote 5.x
- ThingsBoard 3.6+
- (Opcional) AWS IoT Core

**Ejecutar**:
```bash
cd testing/interoperability

# Test todos los servidores
python multi_vendor_lwm2m.py \
  --servers leshan,coiote,thingsboard \
  --nodes esp32c6,nrf52,stm32

# Output: Matriz compatibilidad
```

**Matriz esperada**:
```
┌────────────┬─────────┬─────────┬──────────────┐
│ Client/Srv │ Leshan  │ Coiote  │ ThingsBoard  │
├────────────┼─────────┼─────────┼──────────────┤
│ ESP32-C6   │   ✅    │   ✅    │      ✅      │
│ nRF52      │   ✅    │   ✅    │      ⚠️      │
│ STM32      │   ✅    │   ⚠️    │      ✅      │
└────────────┴─────────┴─────────┴──────────────┘
```

---

### 7. Integration: End-to-End Scenario

**Script**: `integration/end_to_end_lwm2m.py`

Test completo del flujo de datos completo.

**Flujo validado**:
```
[Sensor] → [Mesh HaLow] → [Gateway] → [TB Edge] → [TB Server] → [Dashboard]
```

**Pasos automatizados**:
1. Provisionar dispositivo
2. Registrar en LwM2M server
3. Enviar telemetría (10 muestras)
4. Validar llegada a ThingsBoard
5. Ejecutar comando RPC (Write resource)
6. Validar actuación
7. Desregistrar limpiamente

**Ejecutar**:
```bash
cd testing/integration

python end_to_end_lwm2m.py \
  --device /dev/ttyUSB0 \
  --lwm2m-server coap://192.168.1.100:5683 \
  --thingsboard-host localhost:8080 \
  --duration 300

# Output: PASS/FAIL con logs detallados
```

---

### 8. Load Testing: MQTT Broker

**Script**: `integration/mqtt_broker_load.py`

Test de carga para Mosquitto/EMQX.

**Escenarios**:
- 100 nodos @ 1 msg/min
- 500 nodos @ 1 msg/5min
- 1000 nodos @ 1 msg/10min

**Métricas**:
- CPU/RAM broker
- Latencia publicación → suscripción
- Mensajes perdidos
- Connection churn (connects/disconnects)

**Ejecutar**:
```bash
cd testing/integration

python mqtt_broker_load.py \
  --broker localhost:1883 \
  --clients 500 \
  --interval 300 \
  --duration 3600

# Monitoreo paralelo con Prometheus
# Ver: http://localhost:3000/d/mqtt-broker
```

---

## 📊 Reporting

### Generar Reporte Consolidado
```bash
cd testing

# Ejecutar suite completa
./run_all_tests.sh

# Output:
# - data/benchmarks/summary_YYYY_MM_DD.csv
# - testing/reports/full_report_YYYY_MM_DD.pdf
```

### Integración con Tesis
```bash
# Copiar resultados a LaTeX
cp data/benchmarks/summary_*.csv \
   docs/thesis/architecture/tables/results.csv

# Regenerar gráficos
cd docs/thesis/architecture/figures
python generate_plots.py
```

---

## 🔧 Configuración

### Entorno Virtual Python
```bash
cd testing
python3 -m venv venv
source venv/bin/activate
pip install -r ../requirements.txt
```

### Variables de Entorno
```bash
# testing/.env
LESHAN_SERVER_URL=http://localhost:8081
THINGSBOARD_URL=http://localhost:8080
THINGSBOARD_TOKEN=your_device_token
MQTT_BROKER=localhost:1883
TEST_DEVICE_PORT=/dev/ttyUSB0
```

### Configuración Tests
```python
# testing/config.py
class TestConfig:
    # Timeouts
    REGISTER_TIMEOUT = 30  # segundos
    OBSERVE_TIMEOUT = 60
    
    # Retry
    MAX_RETRIES = 3
    RETRY_DELAY = 5
    
    # Performance thresholds
    MAX_LATENCY_P95 = 100  # ms
    MIN_THROUGHPUT = 500   # msg/s
    MAX_PACKET_LOSS = 1.0  # %
```

---

## 📈 Benchmarks de Referencia

### Hardware: ESP32-C6 @ 160MHz, WiFi 2.4GHz

| Métrica | LwM2M | MQTT | Delta |
|---------|-------|------|-------|
| **Latencia E2E (p50)** | 45 ms | 78 ms | -42% |
| **Latencia E2E (p95)** | 92 ms | 143 ms | -36% |
| **Throughput (50 nodos)** | 850 msg/s | 620 msg/s | +37% |
| **Overhead payload** | 127 bytes | 245 bytes | -48% |
| **Consumo activo** | 95 mA | 115 mA | -17% |
| **Consumo promedio 24h** | 7.5 mAh | 10.2 mAh | -26% |
| **Memoria RAM** | 45 KB | 62 KB | -27% |
| **Flash** | 520 KB | 485 KB | +7% |

---

## 📚 Recursos

### Standards
- [ISO/IEC 30141:2024](https://www.iso.org/standard/81091.html)
- [OMA LwM2M Test Spec](https://www.openmobilealliance.org/release/LightweightM2M/)
- [MQTT Conformance](https://www.eclipse.org/paho/clients/testing/)

### Tools
- [pytest](https://docs.pytest.org/)
- [locust](https://locust.io/) - Load testing
- [Wireshark](https://www.wireshark.org/) - Packet analysis
- [Nordic PPK2](https://www.nordicsemi.com/Products/Development-hardware/Power-Profiler-Kit-2)

### Guías
- [Getting Started](../docs/guides/getting_started.md)
- [Performance Testing Best Practices](../docs/guides/performance_testing.md)
- [ISO 30141 Compliance Guide](../docs/guides/iso30141_compliance.md)

---

**Mantenedor**: JSebGiraldo  
**CI/CD**: GitHub Actions (`.github/workflows/integration_tests.yml`)  
**Última actualización**: Octubre 2025
