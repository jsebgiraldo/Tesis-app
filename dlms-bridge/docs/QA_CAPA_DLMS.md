# 🔬 Análisis QA - Capa DLMS del Sistema

**Fecha:** 4 de Noviembre de 2025  
**Objetivo:** Entender a fondo el funcionamiento de la capa DLMS y reducir warnings

---

## 📊 Análisis de Logs - Patrones Identificados

### ✅ Estado General
- **Success Rate:** 100.0% en la mayoría de ciclos
- **MQTT Delivery:** 85% (52/61 mensajes en último reporte)
- **Latencia por lectura:** 3-4 segundos
- **Ciclos completados:** 61 en 545s (~8.9s por ciclo)

### ⚠️ Warnings Observados

#### 1. Lecturas DLMS que Fallan Ocasionalmente
```
Nov 04 19:17:31 - [dlms_optimized_reader] - WARNING - Failed to read value for 1-1:14.7.0
Nov 04 19:17:31 - [dlms_client_robust] - WARNING - ⚠️ Lectura falló para frequency (1-1:14.7.0): result=None
Nov 04 19:17:31 - [dlms_optimized_reader] - WARNING - Failed to read value for 1-1:1.7.0
Nov 04 19:17:32 - [dlms_optimized_reader] - WARNING - Failed to read value for 1-1:1.8.0
Nov 04 19:17:32 - [dlms_client_robust] - WARNING - ⚠️ 3/5 lecturas fallaron (parcial, NO reconectando)
```

**Patrón:** Las lecturas fallan en grupos, pero solo ocasionalmente

#### 2. Reconexión Automática
```
Nov 04 19:17:39 - WARNING - ⚠ Demasiados errores (5/5), reconectando...
Nov 04 19:17:40 - INFO - 🔌 Intentando conectar a 192.168.1.127:3333
Nov 04 19:17:42 - INFO - ✓ Conexión DLMS establecida
Nov 04 19:17:42 - INFO - OptimizedDLMSReader initialized (batch=False)
Nov 04 19:17:46 - INFO - | V:  137.07 V | C:  1.35 A | ... | (4.505s)
```

**Observación:** La reconexión funciona correctamente y recupera el sistema

---

## 🏗️ Arquitectura de la Capa DLMS

```
┌─────────────────────────────────────────────────────────────────┐
│  dlms_multi_meter_bridge.py (Orquestador Principal)            │
│  - Maneja múltiples medidores                                   │
│  - Coordina MeterWorker instances                               │
│  - Gestiona publicación MQTT                                     │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 │ Crea MeterWorker para cada medidor
                 │
┌────────────────▼────────────────────────────────────────────────┐
│  MeterWorker (Clase interna)                                   │
│  - Un worker por medidor                                        │
│  - Llama a poll_and_publish() en loop asyncio                  │
│  - Gestiona estadísticas y circuit breaker                      │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 │ Usa ProductionDLMSPoller
                 │
┌────────────────▼────────────────────────────────────────────────┐
│  dlms_poller_production.py (ProductionDLMSPoller)              │
│  - Lógica de polling robusto                                    │
│  - Maneja reconexiones automáticas                              │
│  - Implementa retry logic                                       │
│  - Método principal: poll_once()                                │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 │ Usa RobustDLMSClient wrapper
                 │
┌────────────────▼────────────────────────────────────────────────┐
│  dlms_client_robust.py (RobustDLMSClient)                      │
│  - Wrapper con auto-recuperación                                │
│  - Gestiona estado de conexión                                  │
│  - Reintentos con exponential backoff                           │
│  - Método: read_register()                                      │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 │ Usa OptimizedDLMSReader
                 │
┌────────────────▼────────────────────────────────────────────────┐
│  dlms_optimized_reader.py (OptimizedDLMSReader)                │
│  - Caché de scalers (Fase 2)                                   │
│  - Reduce queries al medidor en 50%                             │
│  - Método: read_register_optimized()                            │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 │ Usa DLMSClient base
                 │
┌────────────────▼────────────────────────────────────────────────┐
│  dlms_reader.py (DLMSClient)                                   │
│  - Implementación base del protocolo DLMS                       │
│  - Maneja HDLC frames                                           │
│  - Comunicación TCP/IP con el medidor                           │
│  - Métodos: connect(), read_register(), disconnect()           │
└─────────────────────────────────────────────────────────────────┘
                 │
                 │ TCP/IP + Protocolo DLMS/COSEM
                 │
┌────────────────▼────────────────────────────────────────────────┐
│  MEDIDOR DLMS (192.168.1.127:3333)                            │
│  - Hardware físico del medidor                                  │
│  - Responde a queries DLMS/HDLC                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 🔄 Flujo de Una Lectura DLMS

### Paso 1: Inicio del Ciclo
```python
# MeterWorker.poll_and_publish()
readings = self.poller.poll_once()  # Llama a ProductionDLMSPoller
```

### Paso 2: Poll Once
```python
# ProductionDLMSPoller.poll_once()
for measurement in self.measurements:  # voltage_l1, current_l1, frequency, etc.
    obis = MEASUREMENTS[measurement][0]  # "1-1:32.7.0"
    result = self.optimized_reader.read_register_optimized(obis)
```

### Paso 3: Lectura Optimizada
```python
# OptimizedDLMSReader.read_register_optimized()
if obis_code in self._scaler_cache:
    # CACHE HIT - Solo lee valor
    raw_value = self._read_value_only(obis_code)
    scaled_value = self._apply_scaler(raw_value, cached_scaler)
else:
    # CACHE MISS - Lee valor + scaler
    result = self._read_full_register(obis_code)
    self._scaler_cache[obis_code] = (scaler, unit)  # Cachea el scaler
```

### Paso 4: Lectura Base
```python
# DLMSClient.read_register()
# 1. Lee scaler_unit (attribute 3)
scaler_structure = self._send_get_request(class_id, logical_name, 3)

# 2. Lee valor (attribute 2)
value_payload = self._send_get_request(class_id, logical_name, 2)

# 3. Aplica scaler y retorna
value = Decimal(value_raw) * (Decimal(10) ** scaler)
return value, unit_code, value_raw
```

### Paso 5: Envío MQTT
```python
# MeterWorker.poll_and_publish()
if readings and any(readings.values()):
    await self.mqtt_client.publish_telemetry(readings)
```

---

## 🐛 Análisis de Warnings DLMS

### Warning 1: "Failed to read value for X"

**Dónde ocurre:**
```python
# dlms_optimized_reader.py - línea ~90
def _read_value_only(self, obis_code: str) -> Optional[Any]:
    try:
        value = self.client.read_register(obis_code)
        return value
    except Exception as e:
        logger.debug(f"Error reading value for {obis_code}: {e}")
        return None  # ❌ Aquí se genera el warning
```

**Causa raíz:** El `read_register()` del `DLMSClient` puede fallar por:
1. **Timeout en TCP** - El medidor no responde a tiempo
2. **Error en frame HDLC** - Frame boundary inválido
3. **Secuencia incorrecta** - Números de secuencia desincronizados
4. **Buffer con basura** - Datos residuales del TCP

**Impacto:**
- Lectura se marca como `None`
- Si 3/5 lecturas fallan → Warning pero NO reconecta
- Si 5/5 lecturas fallan → Reconexión automática

---

## 📈 Métricas de Rendimiento

### Timing Breakdown (por ciclo completo)

| Operación | Tiempo Estimado | % del Total |
|-----------|-----------------|-------------|
| **5x Lecturas DLMS** | 2.5-3.5s | 70-85% |
| - Voltage (cache hit) | ~0.5s | |
| - Current (cache hit) | ~0.5s | |
| - Frequency (cache hit) | ~0.5s | |
| - Active Power (cache hit) | ~0.5s | |
| - Active Energy (cache hit) | ~0.5s | |
| **Publicación MQTT** | 0.01-0.05s | <2% |
| **Procesamiento Python** | 0.05-0.1s | <3% |
| **Espera (interval 5s)** | 1-2s | 15-30% |
| **TOTAL por ciclo** | 3.5-4.5s | 100% |

### Optimizaciones Implementadas

1. **Caché de Scalers (Fase 2)**
   - Primera lectura: 2 queries (valor + scaler)
   - Lecturas siguientes: 1 query (solo valor)
   - **Reducción: 50% en queries**

2. **Sin Batch Reading (Fase 3)**
   - No implementado porque el medidor no lo soporta
   - Requeriría modificar firmware del medidor

---

## 🎯 Causas Probables de los Warnings

### 1. Latencia de Red

**Evidencia:**
```
| V:  137.07 V | ... | (3.296s)  ← Rápido
| V:  137.70 V | ... | (4.330s)  ← Más lento
| V:  137.77 V | ... | (3.681s)  ← Normal
| V:  136.98 V | ... | (3.302s)  ← Rápido
```

**Observación:** La latencia varía entre 3-4.5 segundos

**Posible causa:**
- El medidor está procesando otras tareas
- Congestión en la red local
- Buffer TCP lleno en el medidor

### 2. Estado del Medidor

**Evidencia:** Los fallos ocurren en "ráfagas"
```
19:17:31 - Failed to read frequency    ← Fallo
19:17:31 - Failed to read active_power ← Fallo
19:17:32 - Failed to read active_energy← Fallo
[reconexión]
19:17:46 - | V:  137.07 V | ... ✅      ← Éxito después de reconectar
```

**Hipótesis:**
- El medidor entra en un estado "ocupado"
- Posiblemente está haciendo cálculos internos
- O procesando comandos de otro cliente

### 3. Timeout Configuration

**Configuración actual:**
```python
# dlms_client_robust.py
timeout: float = 5.0  # 5 segundos
```

**Problema potencial:**
- Si una lectura toma 5.1s → Timeout → Warning
- Con 5 lecturas, hay más probabilidad de timeout

---

## 💡 Recomendaciones para Reducir Warnings

### Recomendación 1: Aumentar Timeout

**Cambio propuesto:**
```python
# dlms_client_robust.py - DLMSConfig
timeout: float = 7.0  # Aumentar de 5.0 a 7.0 segundos
```

**Justificación:**
- Latencias observadas: 3-4.5s normalmente
- Picos pueden llegar a 5+s
- 7s da margen sin afectar mucho el intervalo

**Impacto:**
- ✅ Menos timeouts → Menos warnings
- ⚠️ Ciclos más lentos en caso de problemas reales
- ⚠️ Podría enmascarar problemas verdaderos

### Recomendación 2: Retry Inteligente por Lectura

**Implementación propuesta:**
```python
# dlms_optimized_reader.py
def _read_value_only(self, obis_code: str, retries: int = 1) -> Optional[Any]:
    for attempt in range(retries + 1):
        try:
            value = self.client.read_register(obis_code)
            if value is not None:
                return value
        except Exception as e:
            if attempt < retries:
                logger.debug(f"Retry {attempt+1}/{retries} for {obis_code}")
                time.sleep(0.5)  # Pausa pequeña antes de reintentar
            else:
                logger.warning(f"Failed to read value for {obis_code}")
                return None
```

**Justificación:**
- Fallos ocasionales pueden ser transitorios
- Un retry rápido puede tener éxito
- Evita reconexión completa por un fallo temporal

**Impacto:**
- ✅ Reduce warnings por fallos transitorios
- ✅ Mantiene success rate alto
- ⚠️ Aumenta latencia en caso de fallo real (0.5s extra)

### Recomendación 3: Logging Más Granular

**Cambio propuesto:**
```python
# Cambiar nivel de logging basado en contexto
if attempt == 0:
    logger.debug(f"First attempt failed for {obis_code}: {e}")  # DEBUG, no WARNING
elif attempt == last_retry:
    logger.warning(f"All attempts failed for {obis_code}")  # Solo WARNING al final
```

**Justificación:**
- Un fallo en primer intento es normal (ruido de red)
- Solo merece WARNING si todos los reintentos fallan

**Impacto:**
- ✅ Logs más limpios
- ✅ Warnings solo para problemas reales
- ✅ Más fácil identificar problemas críticos

### Recomendación 4: Buffer Cleaner más Agresivo

**Observación:** Ya existe `buffer_cleaner.py` pero puede no estar siendo usado

**Verificar si se usa:**
```python
# dlms_reader.py - ¿Se llama aggressive_drain()?
# ¿Se limpia el buffer antes de cada lectura?
```

**Propuesta:**
- Limpiar buffer antes de CADA lectura
- No solo al conectar
- Especialmente después de un timeout

---

## 🧪 Plan de QA y Mejoras

### Fase 1: Mediciones (No invasivo)
1. ✅ Analizar logs actuales (COMPLETADO)
2. 🔄 Medir distribución de latencias
3. 🔄 Identificar patrones temporales de fallos
4. 🔄 Correlacionar fallos con hora del día

### Fase 2: Optimizaciones Conservadoras
1. Aumentar timeout de 5s a 7s
2. Mejorar logging (DEBUG vs WARNING)
3. Implementar retry por lectura (1 reintento)

### Fase 3: Mejoras Avanzadas
1. Buffer cleaner antes de cada lectura
2. Detección de patrones de fallo
3. Backoff exponencial en retries

### Fase 4: Monitoreo Mejorado
1. Dashboard con distribución de latencias
2. Alertas solo para fallos críticos
3. Métricas de cache hit rate

---

## 📝 Próximos Pasos

1. **Implementar timeout más largo** (7s)
2. **Agregar retry en lecturas individuales**
3. **Mejorar logging** (DEBUG para primer fallo)
4. **Verificar uso de buffer_cleaner**
5. **Monitorear mejoras por 24 horas**

---

**Conclusión:** El sistema funciona bien (100% success rate) pero tiene warnings ocasionales por timeouts/fallos transitorios en lecturas DLMS. Las mejoras propuestas reducirán estos warnings sin comprometer la robustez del sistema.
