# CHANGELOG - Tesis IoT SDK

Registro de cambios importantes en la estructura y funcionalidad del repositorio.

## [2.0.0] - 2025-10-14 🎉 Reestructuración Completa SDK

### 🏗️ Cambios Mayores - Nueva Arquitectura
- **BREAKING**: Reorganización completa del workspace para conformidad ISO/IEC 30141:2024
- Migración de estructura plana a SDK jerárquico modular
- Separación clara: devices / infrastructure / protocols / testing / data

### ✨ Nuevas Funcionalidades

#### Documentación
- ✅ `WORKSPACE_GUIDE.md`: Guía maestra completa del SDK
- ✅ `ARCHITECTURE_OVERVIEW.md`: Vista detallada de arquitectura ISO 30141
- ✅ `docs/guides/getting_started.md`: Tutorial paso a paso
- ✅ `docs/thesis/`: Estructura LaTeX para documento formal
- ✅ READMEs específicos por módulo (devices, testing, protocols)

#### Infraestructura
- ✅ `infrastructure/halow_mesh/`: Configuraciones OpenWRT mesh 802.11s
- ✅ `infrastructure/thingsboard/`: Server + Edge con docker-compose
- ✅ `infrastructure/gateways/`: Modbus y DLMS gateways containerizados

#### Dispositivos
- ✅ Reorganización ESP32-C6 firmware:
  - `devices/end_nodes/esp32c6_lwm2m_smart_meter/` (ex LwM2M_SMART_METER_OBJ_ESP32C6)
  - `devices/end_nodes/esp32c6_lwm2m_temp/` (ex LwM2M_TEMP_OBJ_ESP32C6)
  - `devices/end_nodes/esp32c6_mqtt/` (ex mqtt_node)
- ✅ `devices/end_nodes/common/`: Componentes reutilizables

#### Testing
- ✅ `testing/compliance/`: Validadores ISO 30141 y LwM2M
- ✅ `testing/performance/`: Benchmarks latencia, throughput, energía
- ✅ `testing/interoperability/`: Tests multi-vendor
- ✅ `testing/integration/`: Escenarios E2E completos

#### Protocolos
- ✅ `protocols/lwm2m/`: Leshan server + tests conformidad
- ✅ `protocols/mqtt/`: Mosquitto broker + validación QoS
- ✅ `protocols/modbus/`: Simulador + gateway tests
- ✅ `protocols/dlms/`: DLMS reader + adaptador LwM2M

#### Herramientas
- ✅ `tools/network/`: Análisis mesh, packet capture
- ✅ `tools/deployment/`: Scripts flasheo masivo, provisioning
- ✅ `tools/monitoring/`: Health checks, alertas
- ✅ `tools/validation/`: Conformidad estándares

#### Scripts
- ✅ `scripts/setup_environment.sh`: Instalación automática dependencias
- ✅ `migrate_to_new_structure.sh`: Script de migración ejecutado

### 🔄 Migraciones

| Ruta Antigua | Ruta Nueva |
|--------------|------------|
| `projects/LwM2M_SMART_METER_OBJ_ESP32C6/` | `devices/end_nodes/esp32c6_lwm2m_smart_meter/` |
| `projects/LwM2M_TEMP_OBJ_ESP32C6/` | `devices/end_nodes/esp32c6_lwm2m_temp/` |
| `projects/mqtt_node/` | `devices/end_nodes/esp32c6_mqtt/` |
| `projects/common/` | `devices/end_nodes/common/` |
| `docker/thingsboard/` | `infrastructure/thingsboard/server/` |
| `docker/leshan/` | `protocols/lwm2m/server_leshan/` |
| `docker/openwisp/` | `infrastructure/network_management/` |
| `docs/notes/` | `docs/guides/` |
| `tools/tshark_capture.sh` | `tools/network/tshark_capture.sh` |

### 📦 Dependencias Actualizadas
- `requirements.txt`: Consolidado con todas las dependencias Python
  - `tb-mqtt-client>=1.9.0`
  - `paho-mqtt>=1.6.1`
  - `pymodbus>=3.5.0`
  - `gurux-dlms>=1.0.155`
  - `pandas>=2.1.0`, `matplotlib>=3.8.0`, `jupyter>=1.0.0`
  - `pytest>=7.4.0`

### 🔧 Mejoras
- `.gitignore` actualizado con patrones para nueva estructura
- Permisos ajustados correctamente en todo el workspace
- Scripts con `#!/bin/bash` y `set -euo pipefail` para robustez

### 📚 Documentación
- Guías técnicas en `docs/guides/`:
  - Getting Started
  - HaLow Mesh Setup
  - LwM2M Deployment
  - MQTT Integration
  - ThingsBoard Edge Config
  - Gateway Protocols
- Estructura tesis LaTeX en `docs/thesis/architecture/`
- Matriz conformidad ISO 30141 en `docs/thesis/compliance/`

### 🎯 Objetivos Logrados
- ✅ Estructura clara y escalable
- ✅ Separación de responsabilidades (devices/infra/protocols/testing)
- ✅ Documentación exhaustiva
- ✅ Preparado para validación ISO 30141
- ✅ Scripts automatización para desarrollo ágil
- ✅ Evidencias experimentales organizadas en `data/`

---

## [1.0.0] - 2025-10-11 - Versión Base

### Funcionalidades Iniciales
- ✅ Cliente LwM2M ESP32-C6 (Smart Meter, Temperature)
- ✅ Cliente MQTT ESP32-C6 con SDK ThingsBoard
- ✅ Docker Compose para ThingsBoard, Leshan, OpenWisp
- ✅ Scripts básicos: `tshark_capture.sh`
- ✅ Documentación inicial en `docs/notes/`

### Estructura Original
```
Tesis-app/
├── projects/           # Firmware ESP-IDF
├── docker/             # Stacks Docker
├── protocols/          # Broker MQTT
├── docs/               # Notas
└── tools/              # Scripts red
```

---

## Roadmap Futuro

### v2.1.0 (Noviembre 2025)
- [ ] Implementar gateway DLMS completo
- [ ] Ansible playbooks para despliegue mesh HaLow
- [ ] CI/CD con GitHub Actions (build firmware, tests)
- [ ] Dashboards Grafana pre-configurados

### v2.2.0 (Diciembre 2025)
- [ ] Tests conformidad ISO 30141 automatizados (Python)
- [ ] Experimentos comparativos LwM2M vs MQTT completos
- [ ] Digital twin de arquitectura (simulación ns-3)
- [ ] Documentación LaTeX tesis (80%)

### v3.0.0 (Enero 2026)
- [ ] Mesh HaLow funcional con hardware real
- [ ] Despliegue ThingsBoard Edge en 3+ puntos
- [ ] 100+ dispositivos en red operativa
- [ ] Tesis defendida ✨

---

**Formato**: [Semantic Versioning](https://semver.org/)  
**Categorías**: Added, Changed, Deprecated, Removed, Fixed, Security

