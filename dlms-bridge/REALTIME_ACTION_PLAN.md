# 🎯 Plan de Acción: Sistema Realtime con Baja Tasa de Error

## 📊 Análisis de Problemas Detectados

### Problemas Críticos Identificados

1. **❌ Supervisor no puede ejecutar systemctl/sudo**
   - Error: `[Errno 2] No such file or directory: 'systemctl'`
   - Causa: Servicio systemd sin PATH configurado
   - Impacto: Supervisor no puede reiniciar servicios automáticamente
   - Prioridad: **CRÍTICA**

2. **⚠️ Errores HDLC frecuentes**
   - Error: `Invalid HDLC frame boundary`
   - Frecuencia: ~1 cada 30 segundos
   - Causa: Timeout DLMS insuficiente o problemas de red
   - Impacto: Pérdida de telemetría, reinicios constantes
   - Prioridad: **ALTA**

3. **🐌 Latencia alta (>30s end-to-end)**
   - Polling interval: 5 segundos
   - Check interval supervisor: 30 segundos
   - Max age telemetry: 60 segundos
   - Causa: Intervalos muy conservadores
   - Impacto: Sistema lejos de realtime
   - Prioridad: **ALTA**

4. **🔄 Telemetría estancada detectada frecuentemente**
   - Error: `Telemetry stale (mismo timestamp)`
   - Frecuencia: ~1 cada 2-4 minutos
   - Causa: Errores HDLC + reinicios lentos
   - Impacto: Datos no llegan a ThingsBoard
   - Prioridad: **MEDIA**

---

## 🎯 Objetivos del Plan

### Métricas Objetivo

| Métrica | Actual | Objetivo | Mejora |
|---------|--------|----------|--------|
| **Latencia end-to-end** | ~30-60s | <3s | 90-95% |
| **Tasa de error** | ~40-50% | <5% | 90% |
| **Disponibilidad** | ~-333% (bug) | >95% | N/A |
| **Frecuencia de polling** | 5s | 2s | 60% |
| **Detección de fallos** | 30s | 10s | 66% |
| **Tiempo de recuperación** | 30-60s | 10-15s | 66-75% |

### Definición de Realtime

- **Latencia objetivo**: <3 segundos desde lectura DLMS hasta ThingsBoard
- **Throughput**: 0.5 Hz (1 muestra cada 2 segundos)
- **Disponibilidad**: >95% (máximo 36 minutos downtime por semana)
- **Tasa de error**: <5% (máximo 1 error cada 20 lecturas)

---

## 📋 Plan de Acción Detallado

### FASE 1: Validación y Baseline (15 minutos)

#### 1.1 Diagnóstico Estado Actual
```bash
# Validar servicios
./qos-diagnostics.sh status
sudo systemctl status mosquitto dlms-mosquitto-bridge thingsboard-gateway

# Medir latencia DLMS directa
time python3 -c "from dlms_reader import DLMSClient; 
c = DLMSClient('192.168.1.127', 3333); 
c.connect(); 
print(c.read_obis('1-1:32.7.0')); 
c.disconnect()"

# Contar errores recientes
journalctl -u dlms-mosquitto-bridge.service --since "10 minutes ago" | grep -c "HDLC"

# Medir throughput actual
mosquitto_sub -h localhost -p 1884 -t 'v1/gateway/telemetry' -v | ts '[%Y-%m-%d %H:%M:%S]'
```

**Outputs esperados:**
- Latencia DLMS: 300-500ms por lectura
- Errores HDLC: 5-10 en 10 minutos
- Throughput: 1 mensaje cada 5-6 segundos

---

### FASE 2: Fixes Críticos (30 minutos)

#### 2.1 Fix PATH en Supervisor Service ⚡ CRÍTICO

**Problema:** Supervisor no puede ejecutar `systemctl` ni `sudo`

**Solución:**
```bash
# Editar service file
sudo nano /etc/systemd/system/qos-supervisor.service

# Agregar en la sección [Service]:
Environment="PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"

# Recargar y reiniciar
sudo systemctl daemon-reload
sudo systemctl restart qos-supervisor.service

# Validar en 2 minutos
sleep 120 && ./qos-diagnostics.sh errors 5
```

**Validación:** No más errores `No such file or directory: 'systemctl'`

#### 2.2 Mejorar Manejo de Errores HDLC

**Cambios en `dlms_to_mosquitto_bridge.py`:**

```python
# Configuración de retry
MAX_RETRIES = 3
RETRY_DELAYS = [0.1, 0.2, 0.4]  # Exponential backoff (100ms, 200ms, 400ms)

def read_dlms_with_retry(self, obis_code: str) -> Optional[float]:
    """Lee OBIS con reintentos rápidos en caso de error HDLC"""
    for attempt in range(MAX_RETRIES):
        try:
            value = self.dlms_client.read_obis(obis_code)
            if attempt > 0:
                logger.info(f"✅ Recuperado en intento {attempt + 1}")
            return value
        except Exception as e:
            if "HDLC" in str(e) and attempt < MAX_RETRIES - 1:
                delay = RETRY_DELAYS[attempt]
                logger.warning(f"⚠️ Error HDLC intento {attempt + 1}/{MAX_RETRIES}, retry en {delay}s")
                time.sleep(delay)
                # Reconectar rápido
                try:
                    self.dlms_client.disconnect()
                    time.sleep(0.05)
                    self.dlms_client.connect()
                except:
                    pass
                continue
            else:
                logger.error(f"❌ Error DLMS: {e}")
                raise
    return None
```

**Impacto esperado:** 
- Reducir errores HDLC de ~40% a <10%
- Recuperación automática en 100-400ms
- Menos reinicios de servicio completo

---

### FASE 3: Optimizaciones de Performance (30 minutos)

#### 3.1 Reducir Intervalo de Polling (5s → 2s)

```bash
# Editar service
sudo nano /etc/systemd/system/dlms-mosquitto-bridge.service

# Cambiar ExecStart:
ExecStart=/home/pci/Documents/sebas_giraldo/Tesis-app/dlms-bridge/venv/bin/python3 \
    dlms_to_mosquitto_bridge.py --meter-id 1 --interval 2.0

sudo systemctl daemon-reload
sudo systemctl restart dlms-mosquitto-bridge.service
```

**Impacto:** Latencia de datos se reduce de 5s a 2s (~60% mejora)

#### 3.2 Optimizar Timeout DLMS

**Cambios en `dlms_reader.py` (DLMSClient):**

```python
# En __init__ o connect()
self.client.set_timeout(2000)  # 2 segundos en lugar de default (probablemente 5s)
```

**Impacto:** Reducir tiempo de espera en lecturas, detectar fallos más rápido

#### 3.3 Usar QoS 0 para Realtime

**Cambios en `dlms_to_mosquitto_bridge.py`:**

```python
# Publicación MQTT
def publish_telemetry(self, data: Dict):
    payload = json.dumps(data)
    
    # QoS 0 para realtime (fire-and-forget)
    info = self.mqtt_client.publish(
        self.topic,
        payload,
        qos=0,  # Cambiar de 1 a 0 para menor latencia
        retain=False
    )
    
    # Log sin esperar confirmación
    logger.info(f"📤 Published {len(data)} points")
```

**Impacto:** 
- Reduce latencia MQTT de ~100-200ms a <50ms
- Sin confirmación de entrega (aceptable para telemetría realtime)

#### 3.4 Connection Pooling DLMS

**Cambios en `dlms_to_mosquitto_bridge.py`:**

```python
def poll_forever(self):
    """Mantener conexión DLMS persistente"""
    
    # Conectar una sola vez
    self.dlms_client.connect()
    logger.info("🔌 Conexión DLMS persistente establecida")
    
    reconnect_counter = 0
    MAX_READS_BEFORE_RECONNECT = 100  # Reconectar cada 100 lecturas (~200s)
    
    while running:
        try:
            # Leer sin desconectar
            telemetry = self.read_all_measurements()
            
            # Publicar
            if telemetry:
                self.publish_telemetry(telemetry)
                self.last_successful_read = time.time()
                self.consecutive_errors = 0
            
            reconnect_counter += 1
            
            # Reconexión preventiva periódica
            if reconnect_counter >= MAX_READS_BEFORE_RECONNECT:
                logger.info("🔄 Reconexión preventiva DLMS")
                self.dlms_client.disconnect()
                time.sleep(0.2)
                self.dlms_client.connect()
                reconnect_counter = 0
            
            time.sleep(self.interval)
            
        except Exception as e:
            logger.error(f"❌ Error: {e}")
            self.consecutive_errors += 1
            
            # Reconectar en error
            try:
                self.dlms_client.disconnect()
                time.sleep(0.5)
                self.dlms_client.connect()
                reconnect_counter = 0
            except:
                pass
            
            # Circuit breaker
            if self.consecutive_errors >= 10:
                logger.error("💔 Circuit breaker activado")
                break
    
    # Desconectar al finalizar
    try:
        self.dlms_client.disconnect()
    except:
        pass
```

**Impacto:**
- Elimina overhead de connect/disconnect (300-500ms por ciclo)
- Reduce latencia total de ~5.5s a ~2.3s
- Menor carga en el medidor DLMS

---

### FASE 4: Monitoreo Mejorado (20 minutos)

#### 4.1 Ajustar Intervalos del Supervisor

**Cambios en `qos_supervisor_service.py`:**

```python
# Intervalos para realtime
CHECK_INTERVAL = 10          # 30s → 10s (detección 3x más rápida)
TELEMETRY_MAX_AGE = 20       # 60s → 20s (más sensible a estancamiento)
CYCLE_DURATION = 30 * 60     # Mantener 30 min
REST_DURATION = 2 * 60       # 5min → 2min (menos downtime de monitoreo)

# Umbral más agresivo para reinicio
def take_corrective_action(self, issue: str):
    if "stale" in issue.lower() or "obsoleta" in issue.lower():
        # Reiniciar inmediatamente (no esperar 2+ fallos)
        if self.failed_checks >= 1:  # Era >= 1, mantener pero reintentar más rápido
            logger.warning(f"⚡ Acción correctiva por: {issue}")
            self.restart_service("dlms-mosquitto-bridge.service")
```

**Reiniciar supervisor:**
```bash
sudo systemctl restart qos-supervisor.service
```

**Impacto:**
- Detección de fallos en 10s en lugar de 30s
- Corrección automática en 10-15s en lugar de 30-60s

#### 4.2 Agregar Métricas de Performance

**Nuevo archivo: `realtime_metrics.py`**

```python
#!/usr/bin/env python3
"""
Script para medir métricas de performance en realtime
"""
import time
import requests
import json
from datetime import datetime

TB_URL = "http://localhost:8080"
TB_USERNAME = "tenant@thingsboard.org"
TB_PASSWORD = "tenant"
DEVICE_NAME = "DLMS-Meter-01"

def measure_latency():
    """Medir latencia end-to-end"""
    
    # Login ThingsBoard
    login_payload = {"username": TB_USERNAME, "password": TB_PASSWORD}
    response = requests.post(f"{TB_URL}/api/auth/login", json=login_payload)
    token = response.json().get("token")
    
    headers = {"X-Authorization": f"Bearer {token}"}
    
    # Obtener device ID
    device_response = requests.get(
        f"{TB_URL}/api/tenant/devices?deviceName={DEVICE_NAME}",
        headers=headers
    )
    device_id = device_response.json().get("id", {}).get("id")
    
    samples = []
    errors = 0
    
    print("📊 Midiendo latencia realtime (60 segundos)...\n")
    
    start_time = time.time()
    last_ts = None
    
    while time.time() - start_time < 60:
        try:
            # Obtener última telemetría
            telemetry_response = requests.get(
                f"{TB_URL}/api/plugins/telemetry/DEVICE/{device_id}/values/timeseries?keys=voltage_l1,timestamp",
                headers=headers
            )
            
            data = telemetry_response.json()
            
            if "voltage_l1" in data and data["voltage_l1"]:
                ts = data["voltage_l1"][0]["ts"]
                
                if ts != last_ts:
                    # Nueva lectura
                    age = (time.time() * 1000 - ts) / 1000
                    samples.append(age)
                    
                    print(f"⏱️  Latencia: {age:.2f}s | Lecturas: {len(samples)} | Errores: {errors}")
                    
                    last_ts = ts
            
            time.sleep(2)
            
        except Exception as e:
            errors += 1
            print(f"❌ Error: {e}")
            time.sleep(2)
    
    # Estadísticas
    if samples:
        avg_latency = sum(samples) / len(samples)
        min_latency = min(samples)
        max_latency = max(samples)
        error_rate = (errors / (errors + len(samples))) * 100
        
        print("\n" + "="*50)
        print("📈 RESULTADOS")
        print("="*50)
        print(f"Muestras recibidas: {len(samples)}")
        print(f"Errores: {errors}")
        print(f"Tasa de error: {error_rate:.2f}%")
        print(f"Latencia promedio: {avg_latency:.2f}s")
        print(f"Latencia mínima: {min_latency:.2f}s")
        print(f"Latencia máxima: {max_latency:.2f}s")
        print(f"Throughput: {len(samples)/60:.2f} Hz")
        print("="*50)
    else:
        print("❌ No se recibieron muestras")

if __name__ == "__main__":
    measure_latency()
```

---

### FASE 5: Validación Final (10 minutos)

#### 5.1 Test de Performance

```bash
# Ejecutar medición
python3 realtime_metrics.py

# Monitorear logs en paralelo
./qos-diagnostics.sh live

# En otra terminal, ver telemetría
mosquitto_sub -h localhost -p 1884 -t 'v1/gateway/telemetry' -v | ts
```

#### 5.2 Criterios de Éxito

✅ **MÍNIMO ACEPTABLE:**
- Latencia promedio: <5s
- Tasa de error: <10%
- Disponibilidad: >90%
- Throughput: 0.4 Hz (1 cada 2.5s)

✅ **OBJETIVO IDEAL:**
- Latencia promedio: <3s
- Tasa de error: <5%
- Disponibilidad: >95%
- Throughput: 0.5 Hz (1 cada 2s)

🎯 **REALTIME COMPLETO:**
- Latencia promedio: <2s
- Tasa de error: <2%
- Disponibilidad: >98%
- Throughput: 0.5 Hz estable

---

## 🔄 Implementación por Prioridad

### 🔥 PRIORIDAD CRÍTICA (implementar YA)

1. ✅ **Fix PATH supervisor** - Sin esto, el sistema no se auto-repara
2. ✅ **Retry en errores HDLC** - Reduce errores de 40% a <10%

### ⚡ PRIORIDAD ALTA (implementar hoy)

3. ✅ **Connection pooling DLMS** - Mayor impacto en latencia (~50% mejora)
4. ✅ **Reducir intervalo a 2s** - Acercarse a realtime
5. ✅ **Mejorar monitoreo (10s checks)** - Detección y recuperación más rápida

### 📊 PRIORIDAD MEDIA (implementar esta semana)

6. ✅ **QoS 0 para telemetría** - Optimización menor pero útil
7. ✅ **Timeout DLMS optimizado** - Detectar fallos más rápido
8. ✅ **Métricas de performance** - Visibilidad del sistema

---

## 📈 Mejoras Esperadas

### Antes de Implementación

```
┌─────────────────────────────────────────┐
│  ESTADO ACTUAL                          │
├─────────────────────────────────────────┤
│  Latencia:        30-60s                │
│  Tasa de error:   40-50%                │
│  Polling:         cada 5s               │
│  Detección fallos: 30s                  │
│  Recuperación:    30-60s                │
│  Disponibilidad:  ~50-60%               │
└─────────────────────────────────────────┘
```

### Después de Implementación

```
┌─────────────────────────────────────────┐
│  ESTADO OBJETIVO                        │
├─────────────────────────────────────────┤
│  Latencia:        2-3s      ✅ -95%     │
│  Tasa de error:   <5%       ✅ -90%     │
│  Polling:         cada 2s   ✅ -60%     │
│  Detección fallos: 10s      ✅ -66%     │
│  Recuperación:    10-15s    ✅ -75%     │
│  Disponibilidad:  >95%      ✅ +35pp    │
└─────────────────────────────────────────┘
```

---

## 🚀 Próximos Pasos

1. **Ejecutar FASE 1** - Establecer baseline
2. **Implementar fixes críticos** - FASE 2 completa
3. **Aplicar optimizaciones** - FASE 3 por prioridad
4. **Validar mejoras** - FASE 5 cada cambio
5. **Ajuste fino** - Iterar según métricas

---

## 📞 Troubleshooting

### Si latencia sigue alta (>5s)

```bash
# Verificar red
ping -c 10 192.168.1.127

# Medir tiempo de lectura DLMS directa
time python3 -c "from dlms_reader import DLMSClient; c = DLMSClient('192.168.1.127', 3333); c.connect(); print(c.read_obis('1-1:32.7.0')); c.disconnect()"

# Si >2s, problema en medidor o red
```

### Si errores HDLC persisten (>10%)

```bash
# Verificar calidad de red
mtr -r -c 100 192.168.1.127

# Considerar aumentar timeout
# En dlms_reader.py: self.client.set_timeout(3000)
```

### Si supervisor no detecta problemas

```bash
# Verificar autenticación ThingsBoard
curl -X POST http://localhost:8080/api/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"tenant@thingsboard.org", "password":"tenant"}'

# Verificar device ID
# En qos_supervisor_service.py logs
```

---

## 📚 Referencias

- `QOS_SUPERVISOR_MANUAL.md` - Manual completo del supervisor
- `dlms_to_mosquitto_bridge.py` - Bridge DLMS-MQTT
- `qos_supervisor_service.py` - Servicio de monitoreo
- `/docs/GUIA_PRODUCCION.md` - Guía de producción
- `/docs/SOLUCION_HDLC_ERRORS.md` - Solución errores HDLC

---

**Última actualización:** 2025-11-04
**Versión:** 1.0
**Autor:** Sistema de monitoreo automatizado
