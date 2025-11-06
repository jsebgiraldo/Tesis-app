# Arquitectura del Gateway ThingsBoard - Integración DLMS

## Comparación: Sistema Actual vs Gateway Pattern

### Sistema Actual (dlms_multi_meter_bridge.py)

```
┌─────────────────────────────────────────────────┐
│ ThingsBoard Platform                            │
│                                                 │
│ Device 1 (Token: ABC)                           │
│ Device 2 (Token: DEF)                           │
│ Device 3 (Token: GHI)                           │
└──────────▲──────────▲──────────▲────────────────┘
           │          │          │
           │ MQTT     │ MQTT     │ MQTT
           │ QoS=1    │ QoS=1    │ QoS=1
           │          │          │
┌──────────┴──────────┴──────────┴────────────────┐
│ dlms_multi_meter_bridge.py                      │
│                                                 │
│ ┌─────────────┐ ┌─────────────┐ ┌────────────┐ │
│ │ MeterWorker │ │ MeterWorker │ │MeterWorker │ │
│ │ + MQTT      │ │ + MQTT      │ │+ MQTT      │ │
│ │   Client    │ │   Client    │ │  Client    │ │
│ └──────┬──────┘ └──────┬──────┘ └──────┬─────┘ │
│        │                │                │       │
└────────┼────────────────┼────────────────┼───────┘
         │                │                │
         │ DLMS           │ DLMS           │ DLMS
         │                │                │
    ┌────▼───┐       ┌────▼───┐       ┌────▼───┐
    │Meter 1 │       │Meter 2 │       │Meter 3 │
    │.1.127  │       │.1.128  │       │.1.129  │
    └────────┘       └────────┘       └────────┘
```

**Características:**
- ✅ Cada medidor tiene su propio token MQTT
- ✅ Control directo sobre QoS y reconexión
- ❌ Gestión de tokens más compleja
- ❌ Sin buffering automático
- ❌ Cada worker mantiene su propia conexión MQTT

### Nuevo Sistema Gateway

```
┌─────────────────────────────────────────────────┐
│ ThingsBoard Platform                            │
│                                                 │
│ DLMS Gateway (Token: XYZ) ◄── 1 solo token     │
│   ├── Device 1 (child)                          │
│   ├── Device 2 (child)                          │
│   └── Device 3 (child)                          │
└──────────▲──────────────────────────────────────┘
           │
           │ MQTT (QoS=1)
           │ 1 sola conexión
           │
┌──────────┴──────────────────────────────────────┐
│ ThingsBoard IoT Gateway                         │
│ ┌─────────────────────────────────────────────┐ │
│ │ Gateway Core                                │ │
│ │ - Device Registry                           │ │
│ │ - Message Router                            │ │
│ │ - Storage & Buffering                       │ │
│ │ - Reconnection Logic                        │ │
│ └──────────┬──────────────────────────────────┘ │
│            │                                     │
│ ┌──────────┴──────────────────────────────────┐ │
│ │ DLMS Connector (Custom)                     │ │
│ │ ┌──────────┐ ┌──────────┐ ┌──────────┐     │ │
│ │ │ Device 1 │ │ Device 2 │ │ Device 3 │     │ │
│ │ │  Poller  │ │  Poller  │ │  Poller  │     │ │
│ │ └────┬─────┘ └────┬─────┘ └────┬─────┘     │ │
│ └──────┼────────────┼────────────┼───────────┘ │
└────────┼────────────┼────────────┼─────────────┘
         │            │            │
         │ DLMS       │ DLMS       │ DLMS
         │            │            │
    ┌────▼───┐   ┌────▼───┐   ┌────▼───┐
    │Meter 1 │   │Meter 2 │   │Meter 3 │
    │.1.127  │   │.1.128  │   │.1.129  │
    └────────┘   └────────┘   └────────┘
```

**Características:**
- ✅ Un solo token para todos los medidores
- ✅ Buffering automático por ThingsBoard
- ✅ Gestión centralizada de dispositivos
- ✅ Jerarquía: Gateway → Devices
- ✅ Arquitectura estándar ThingsBoard

---

## Ventajas y Desventajas

### Sistema Actual (dlms_multi_meter_bridge.py)

**Ventajas:**
- ✅ Control total sobre MQTT
- ✅ Implementación personalizada probada
- ✅ Watchdog y circuit breaker integrados
- ✅ Métricas de red detalladas
- ✅ Integración con admin API

**Desventajas:**
- ❌ Gestión manual de tokens
- ❌ Sin buffering automático
- ❌ Cada worker = 1 conexión MQTT
- ❌ Más código de infraestructura
- ❌ Posibles conflictos de token

### Gateway Pattern

**Ventajas:**
- ✅ Arquitectura estándar
- ✅ 1 token para N dispositivos
- ✅ Buffering automático
- ✅ Menos código de infraestructura
- ✅ Jerarquía clara de dispositivos
- ✅ Compatible con otros conectores (Modbus, OPC-UA, etc.)
- ✅ Remote Configuration desde ThingsBoard

**Desventajas:**
- ❌ Dependencia de ThingsBoard Gateway
- ❌ Menos control sobre MQTT
- ❌ Capa adicional de abstracción
- ❌ Learning curve de TB Gateway

---

## Casos de Uso Recomendados

### Usar Sistema Actual (dlms_multi_meter_bridge.py)

✅ **Cuando:**
- Necesitas control total sobre MQTT
- Ya tienes tokens asignados por medidor
- Requieres métricas de red muy detalladas
- Integración estrecha con admin API
- Pocos medidores (1-10)

### Usar Gateway Pattern

✅ **Cuando:**
- Muchos medidores (10+)
- Quieres gestión centralizada
- Necesitas buffering automático
- Planeas agregar otros protocolos (Modbus, OPC-UA)
- Prefieres arquitectura estándar
- Remote configuration desde ThingsBoard

---

## Estrategia de Migración

### Opción 1: Migración Total

```bash
# 1. Detener sistema actual
sudo systemctl stop dlms-multi-meter.service

# 2. Instalar gateway
cd gateway/
sudo ./setup_gateway.sh install

# 3. Migrar configuración
# Copiar devices de database a dlms_connector.json

# 4. Iniciar gateway
sudo systemctl start tb-gateway.service
```

### Opción 2: Coexistencia (Recomendado)

**Fase 1: Pruebas paralelas**
```bash
# Sistema actual: medidores 1-3 (tokens individuales)
# Gateway: medidores 4-5 (token gateway)

# Ambos sistemas activos para comparar
```

**Fase 2: Migración gradual**
```bash
# Semana 1: Migrar medidor 1 al gateway
# Semana 2: Migrar medidor 2 al gateway
# Semana N: Migrar todos
```

**Fase 3: Consolidación**
```bash
# Detener sistema actual
# Dejar solo gateway activo
```

---

## Configuración de Coexistencia

### ThingsBoard Side

```
Devices:
├── medidor_1 (token: ABC)  ◄── dlms_multi_meter_bridge.py
├── medidor_2 (token: DEF)  ◄── dlms_multi_meter_bridge.py
├── medidor_3 (token: GHI)  ◄── dlms_multi_meter_bridge.py
│
└── DLMS_Gateway (token: XYZ)  ◄── Gateway
    ├── medidor_4 (child)
    └── medidor_5 (child)
```

### Sistema Side

```bash
# Sistema actual (systemd)
sudo systemctl start dlms-multi-meter.service

# Gateway (systemd)
sudo systemctl start tb-gateway.service

# Ambos pueden correr simultáneamente
# No hay conflicto porque usan tokens diferentes
```

---

## Métricas de Rendimiento

### Sistema Actual

| Métrica | Valor |
|---------|-------|
| Medidores simultáneos | 50+ |
| MQTT connections | N (1 por medidor) |
| Latencia típica | <1s |
| Memoria por worker | ~50MB |
| CPU por worker | ~5% |

### Gateway Pattern

| Métrica | Valor |
|---------|-------|
| Medidores simultáneos | 100+ |
| MQTT connections | 1 (compartida) |
| Latencia típica | <2s |
| Memoria total | ~200MB |
| CPU total | ~10% |

---

## Decisión Recomendada

### Para tu proyecto actual:

**Recomendación: Coexistencia → Migración gradual**

**Razones:**
1. ✅ Sistema actual funciona bien (98.5% success rate)
2. ✅ Gateway proporciona mejor escalabilidad
3. ✅ Coexistencia permite migración sin riesgo
4. ✅ Puedes comparar ambos sistemas
5. ✅ Flexibilidad para elegir lo mejor de ambos

**Plan propuesto:**
```
Fase 1 (1 semana): 
  - Instalar gateway en paralelo
  - Conectar 1-2 medidores de prueba
  - Comparar telemetría

Fase 2 (2-3 semanas):
  - Migrar 50% de medidores al gateway
  - Monitorear estabilidad
  - Ajustar configuración

Fase 3 (1 mes):
  - Decidir: ¿todo gateway o híbrido?
  - Consolidar arquitectura final
```

---

## Conclusión

Ambos sistemas tienen su lugar:

- **Sistema Actual**: Excelente para control fino y pocos medidores
- **Gateway Pattern**: Mejor para escalabilidad y gestión

**Mejor de ambos mundos**: 
- Usa Gateway para medidores de producción estables
- Usa sistema actual para medidores que requieren debugging o control especial
