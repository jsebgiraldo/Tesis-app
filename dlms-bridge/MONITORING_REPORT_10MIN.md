# 📊 Reporte de Monitoreo - 10 Minutos de Observación

**Fecha:** 2025-11-04 16:58-17:08  
**Duración:** 10 minutos  
**Sistema:** QoS Supervisor + DLMS Bridge

---

## 🔍 Hallazgos Principales

### 1. ⚠️ PROBLEMA CRÍTICO DETECTADO

**Patrón de telemetría estancada cada ~1 minuto:**

```
16:58:24 → ❌ Telemetry stale → Reinicio → ✅ Recuperado
16:59:31 → ❌ Telemetry stale → Reinicio → ✅ Recuperado  (67s después)
17:00:37 → ❌ Telemetry stale → Reinicio → ✅ Recuperado  (66s después)
17:01:12 → ❌ Telemetry stale → Reinicio → ✅ Recuperado  (35s después)
17:02:20 → ❌ Telemetry stale → Reinicio → ✅ Recuperado  (68s después)
17:03:26 → ❌ Telemetry stale → Reinicio → ✅ Recuperado  (66s después)
17:04:32 → ❌ Telemetry stale → Reinicio → ✅ Recuperado  (66s después)
17:06:03 → ❌ Telemetry stale → Reinicio → ✅ Recuperado  (91s después)
```

**Frecuencia:** ~1 fallo cada 60-90 segundos  
**Causa raíz:** Conexión DLMS se pierde o se estanca después de múltiples lecturas

### 2. 📈 Estadísticas del Período

| Métrica | Valor | Estado |
|---------|-------|--------|
| **Checks realizados** | 244 | ✅ Funcionando |
| **Problemas detectados** | 60 | ⚠️ Alto |
| **Acciones correctivas** | 55 | ⚡ Muy activo |
| **Bridge reiniciado** | 55 veces | 🔴 Crítico |
| **Disponibilidad** | 75.41% | ⚠️ Bajo objetivo |
| **Frecuencia de reinicios** | 1 cada ~70s | 🔴 Muy frecuente |

### 3. 🕐 Cronología del Monitoreo

```
16:58:47 - Inicio monitoreo
16:58:24 - Check #95  → Telemetry stale → Reinicio
16:58:50 - Check #96  → ✅ OK (edad: 1.9s)
16:59:00 - Check #97  → ✅ OK (edad: 1.9s)
16:59:11 - Check #98  → ✅ OK (edad: 1.1s)
16:59:21 - Check #99  → ✅ OK (edad: 2.9s)
16:59:31 - Check #100 → Telemetry stale → Reinicio
[... patrón se repite ...]
17:07:06 - CICLO #2 COMPLETADO (30 min, 132 checks, 29 problemas)
```

### 4. 🔴 Error Actual en el Bridge

**Al momento del análisis (17:13):**
```
❌ Error voltage_l1 después de 3 intentos: Not connected
❌ Error current_l1 después de 3 intentos: Not connected
❌ Error frequency después de 3 intentos: Not connected
❌ Error active_power después de 3 intentos: Not connected
❌ Error active_energy después de 3 intentos: Not connected
```

**Diagnóstico:** Bridge perdió conexión DLMS y entró en circuit breaker

---

## 🎯 Análisis de Causa Raíz

### Problema: Conexión DLMS se estanca después de ~15-20 lecturas

**Evidencias:**
1. Lecturas exitosas por 3-5 checks (~30-50s)
2. Luego telemetría se estanca (mismo timestamp)
3. Supervisor detecta y reinicia bridge
4. Bridge reconecta exitosamente
5. Ciclo se repite

**Hipótesis de causa raíz:**

#### 1. **Timeout de sesión DLMS (MÁS PROBABLE)**
- El medidor DLMS cierra la sesión después de X segundos de inactividad
- Nuestras lecturas toman ~3-4s cada una
- Después de 15-20 lecturas (~60-80s), la sesión expira
- Siguiente lectura falla porque la conexión está muerta

#### 2. **Buffer overflow en medidor**
- Medidor tiene buffer limitado de comunicación
- Después de múltiples lecturas, buffer se llena
- Medidor deja de responder hasta reset

#### 3. **Bug en cliente DLMS**
- Cliente mantiene conexión "zombie" 
- No detecta que conexión está muerta
- Necesita reconexión periódica

---

## 💡 Soluciones Propuestas

### Solución A: Reconexión Preventiva Periódica (RECOMENDADA)

**Implementación:**
```python
# En dlms_to_mosquitto_bridge.py

def poll_forever(self):
    """Loop principal con reconexión preventiva"""
    
    reconnect_counter = 0
    MAX_READS_BEFORE_RECONNECT = 15  # Reconectar cada 15 lecturas (~60s)
    
    self.dlms.connect()
    
    while running:
        try:
            # Leer y publicar
            telemetry = self.read_all_measurements()
            if telemetry:
                self.publish(telemetry)
                reconnect_counter += 1
            
            # RECONEXIÓN PREVENTIVA
            if reconnect_counter >= MAX_READS_BEFORE_RECONNECT:
                logger.info("🔄 Reconexión preventiva DLMS")
                self.dlms.disconnect()
                time.sleep(0.5)
                self.dlms.connect()
                reconnect_counter = 0
            
            time.sleep(self.interval)
            
        except Exception as e:
            # Manejo de errores...
```

**Pros:**
- ✅ Previene que conexión se estanque
- ✅ Evita reinicios del servicio completo
- ✅ Fácil de implementar
- ✅ Overhead mínimo (~500ms cada 60s)

**Contras:**
- ⚠️ Breve interrupción cada 60s

**Resultado esperado:**
- Disponibilidad: 75% → 95%+
- Reinicios: 55/hora → <5/hora
- Latencia estable sin picos

### Solución B: Keepalive DLMS

**Implementación:**
```python
# Enviar comando keepalive cada 30s en thread separado

import threading

def dlms_keepalive_thread(self):
    while running:
        time.sleep(30)
        try:
            # Leer un parámetro ligero como keepalive
            self.dlms.read_register("1-1:0.9.1")  # Clock del medidor
        except:
            logger.warning("⚠️ Keepalive falló")
```

**Pros:**
- ✅ Mantiene sesión DLMS activa
- ✅ Sin interrupciones

**Contras:**
- ⚠️ Más complejo
- ⚠️ Posible conflicto con lecturas principales

### Solución C: Ajustar Timeout del Medidor

**Si el medidor tiene configuración de timeout:**
```bash
# Aumentar timeout de sesión DLMS en el medidor
# (requiere acceso a configuración del medidor)
Session Timeout: 300s → 600s
```

**Pros:**
- ✅ Solución definitiva

**Contras:**
- ❌ Requiere acceso al medidor
- ❌ Puede no ser posible

---

## 🚀 Plan de Implementación Inmediato

### Fase 1: Implementar Solución A (15 minutos)

1. **Modificar `dlms_to_mosquitto_bridge.py`:**
   - Agregar contador de lecturas
   - Implementar reconexión preventiva cada 15 lecturas
   - Agregar logs de reconexión

2. **Reiniciar bridge:**
   ```bash
   sudo systemctl restart dlms-mosquitto-bridge.service
   ```

3. **Monitorear por 30 minutos:**
   ```bash
   ./qos-diagnostics.sh live
   ```

4. **Validar mejora:**
   ```bash
   ./qos-diagnostics.sh stats 1
   # Esperado: <5 reinicios, >90% disponibilidad
   ```

### Fase 2: Ajustar Parámetros (si necesario)

Si 15 lecturas es muy frecuente:
- Probar con 20 lecturas (~80s)
- Probar con 25 lecturas (~100s)

Si 15 lecturas es insuficiente:
- Reducir a 12 lecturas (~48s)
- Reducir a 10 lecturas (~40s)

### Fase 3: Validación Final (1 hora)

Ejecutar test extendido:
```bash
python3 realtime_metrics.py --duration 3600
```

**Criterios de éxito:**
- ✅ Disponibilidad >95%
- ✅ <5 reinicios por hora
- ✅ Latencia estable <3s
- ✅ Throughput >0.25 Hz

---

## 📊 Comparativa Esperada

### Antes (Estado Actual)
```
Disponibilidad:    75.41%
Reinicios/hora:    55
Problemas/hora:    60
Latencia:          1.35s (con picos en reinicios)
Throughput:        0.27 Hz (interrumpido frecuentemente)
```

### Después (Con Reconexión Preventiva)
```
Disponibilidad:    >95%
Reinicios/hora:    <5
Problemas/hora:    <8
Latencia:          1.35s (estable)
Throughput:        0.27 Hz (continuo)
```

---

## 🔍 Observaciones Adicionales

### Comportamiento del Supervisor

✅ **Funcionando correctamente:**
- Detecta telemetría estancada en 10s
- Reinicia servicios automáticamente
- Recuperación exitosa en 5-10s
- Logs detallados y claros

⚠️ **Áreas de mejora:**
- Demasiados reinicios (síntoma del problema DLMS)
- Disponibilidad baja (75% vs objetivo 95%)

### Comportamiento del Bridge

✅ **Funcionando correctamente:**
- Retry en errores HDLC funciona
- QoS 0 reduce latencia
- Connection pooling implementado
- Logs informativos

🔴 **Problema crítico:**
- Conexión DLMS se pierde periódicamente
- Circuit breaker se activa correctamente
- Requiere reconexión preventiva

---

## 📋 Recomendaciones

### Inmediatas (HOY)

1. ✅ **Implementar reconexión preventiva** (Solución A)
   - Prioridad: CRÍTICA
   - Tiempo: 15 minutos
   - Impacto esperado: +20% disponibilidad

2. ✅ **Monitorear por 1 hora**
   - Validar que reinicios disminuyen
   - Ajustar parámetros si necesario

### Corto Plazo (Esta Semana)

3. ✅ **Optimizar intervalo de reconexión**
   - Encontrar sweet spot entre 10-25 lecturas
   - Minimizar interrupciones

4. ✅ **Test de carga extendido**
   - Ejecutar por 24 horas
   - Validar estabilidad

### Medio Plazo (Este Mes)

5. ⚠️ **Investigar configuración del medidor**
   - Revisar documentación
   - Ajustar timeout si es posible

6. ⚠️ **Considerar actualización firmware**
   - Si hay versión más estable disponible
   - Coordinar con proveedor del medidor

---

## 🎯 Conclusión

El monitoreo de 10 minutos reveló un **patrón claro y predecible** de fallos:

- ✅ Sistema de monitoreo funciona perfectamente
- ✅ Sistema de auto-reparación funciona perfectamente
- 🔴 Conexión DLMS se estanca cada ~60-90 segundos
- 💡 Solución: **Reconexión preventiva cada 15 lecturas**

**Próximo paso:** Implementar Solución A y validar mejora en disponibilidad de 75% → 95%+

---

**Autor:** Análisis automatizado del sistema de monitoreo  
**Fecha:** 2025-11-04 17:15  
**Estado:** REQUIERE ACCIÓN INMEDIATA
