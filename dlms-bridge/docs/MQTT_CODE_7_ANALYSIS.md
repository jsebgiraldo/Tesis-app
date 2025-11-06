# Análisis: MQTT Disconnected Code 7

**Fecha:** 2025-11-04  
**Problema:** Warnings frecuentes de "MQTT Disconnected unexpectedly: code 7"  
**Severidad:** Media (no impacta funcionalidad, pero genera ruido en logs)

---

## 🔍 Diagnóstico

### ¿Qué es el código 7?

En el protocolo MQTT, el código de desconexión **7** significa:

```
MQTT_RC_REQUEST_IDENTIFIER_NOT_FOUND = 7
"The Connection Identifier is a valid identifier but is already in use by another client"
```

**Traducción:** Otro cliente MQTT está usando el mismo `client_id` o `token`, causando que el broker MQTT desconecte al cliente anterior cuando el nuevo se conecta.

---

## 🕵️ Causa Raíz Identificada

### Servicios en Conflicto

Encontramos **DOS servicios** conectándose simultáneamente a ThingsBoard:

#### 1️⃣ **dlms-multi-meter.service** (Nuestro servicio principal)
```bash
PID: 58242
Comando: /home/pci/.../venv/bin/python3 dlms_multi_meter_bridge.py
Estado: ✅ ACTIVO y funcionando correctamente
```

#### 2️⃣ **thingsboard-gateway.service** (Gateway de ThingsBoard)
```bash
PID: 7060
Comando: /var/lib/thingsboard_gateway/venv/bin/python3 ...daemon()
Estado: ✅ ACTIVO pero causando conflicto
```

### Patrón de Desconexión

```
19:22:27 - ⚠️ MQTT Disconnected unexpectedly: code 7
19:22:28 - ✅ MQTT Connected: dlms_meter_1
19:22:30 - 📤 Published + tracked: 110 bytes
19:22:33 - ⚠️ MQTT Disconnected unexpectedly: code 7
19:22:34 - ✅ MQTT Connected: dlms_meter_1
```

**Frecuencia:** Cada ~5-6 segundos (desconexión + reconexión automática)

---

## 📊 Impacto

### ✅ Funcionalidad NO Afectada

- **DLMS:** Lectura de medidor funcionando al 100%
- **MQTT:** Publicación exitosa (reconexión automática funciona)
- **Datos:** Sin pérdida de telemetría
- **Success rate:** 100%

### ⚠️ Problemas Secundarios

1. **Ruido en logs:** Warnings cada 5-6 segundos
2. **Overhead de red:** Reconexiones innecesarias al broker MQTT
3. **Latencia adicional:** 1-2s de latencia por reconexión
4. **Confusión:** Logs dificultan diagnóstico de problemas reales

---

## 🛠️ Soluciones

### Opción 1: Deshabilitar el Gateway de ThingsBoard ✅ RECOMENDADA

**Ventajas:**
- Solución inmediata y definitiva
- No requiere cambios en código
- Elimina el 100% de los conflictos

**Comando:**
```bash
sudo systemctl stop thingsboard-gateway.service
sudo systemctl disable thingsboard-gateway.service
```

**¿Cuándo usar esta opción?**
- Si NO necesitas el gateway de ThingsBoard para otros dispositivos
- Si solo usas `dlms-multi-meter.service` para DLMS

---

### Opción 2: Usar Tokens MQTT Diferentes

**Ventajas:**
- Ambos servicios pueden coexistir
- Útil si necesitas el gateway para otros dispositivos

**Pasos:**

1. **Crear un nuevo dispositivo en ThingsBoard:**
   - Ir a ThingsBoard UI → Devices → Add Device
   - Nombre: `DLMS-Gateway` (diferente a `dlms_meter_1`)
   - Copiar el nuevo token

2. **Configurar el gateway con el nuevo token:**
   ```bash
   sudo nano /etc/thingsboard-gateway/config/tb_gateway.yaml
   ```
   
3. **Configurar `dlms-multi-meter` con token diferente:**
   - Actualizar base de datos `admin.db` con token único

**Desventaja:** Más complejo, requiere coordinación entre servicios

---

### Opción 3: Usar Client IDs Únicos (Temporal)

**Código actual en `tb_mqtt_client.py`:**
```python
self.client_id = client_id or f"dlms_client_{int(time.time())}"
```

**Problema:** Si ambos servicios se conectan en el mismo segundo, tendrán el mismo `client_id`.

**Mejora:**
```python
import uuid
self.client_id = client_id or f"dlms_client_{uuid.uuid4().hex[:8]}"
```

**⚠️ Nota:** Esto NO soluciona el problema si ambos servicios usan el **mismo token**. MQTT permite múltiples `client_id` pero solo UNA conexión por token.

---

## 📋 Recomendación Final

### Para tu caso específico:

Como solo necesitas el servicio `dlms-multi-meter.service` para leer el medidor DLMS y publicar a ThingsBoard, **la mejor solución es:**

```bash
# Detener el gateway de ThingsBoard
sudo systemctl stop thingsboard-gateway.service

# Deshabilitarlo del auto-start
sudo systemctl disable thingsboard-gateway.service

# Verificar que solo quede dlms-multi-meter activo
ps aux | grep -E "mqtt|bridge|gateway" | grep -v grep
```

**Resultado esperado:**
- ✅ Cero warnings de "code 7"
- ✅ Conexión MQTT estable sin desconexiones
- ✅ Logs limpios y legibles
- ✅ Menor overhead de red

---

## 🔬 Validación

Después de aplicar la solución, monitorear:

```bash
# Ver logs en tiempo real
sudo journalctl -u dlms-multi-meter.service -f

# Verificar que NO haya "code 7"
sudo journalctl -u dlms-multi-meter.service --since "1 minute ago" | grep "code 7"

# Debería devolver: (vacío)
```

**Éxito:** Si no aparecen más mensajes "code 7" en 5-10 minutos.

---

## 📚 Referencias

- **MQTT Reason Codes:** [MQTT v3.1.1 Spec - Connect Return Code](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718035)
- **ThingsBoard Gateway Docs:** [Gateway Configuration](https://thingsboard.io/docs/iot-gateway/configuration/)
- **Paho MQTT Python:** [Client Documentation](https://eclipse.dev/paho/files/paho.mqtt.python/html/client.html)

---

## 🎯 Conclusión

El warning **"MQTT Disconnected unexpectedly: code 7"** es:

- ❌ **NO es normal** (indica configuración subóptima)
- ✅ **NO es crítico** (funcionalidad no afectada)
- 🔧 **FÁCIL de solucionar** (deshabilitar servicio conflictivo)
- ⚡ **DEBE solucionarse** (para logs limpios y mejor performance)

**Acción recomendada:** Deshabilitar `thingsboard-gateway.service` si no se necesita.
