# 🏭 Guía de Producción: Sistema DLMS Estable

**Fecha:** 31 de Octubre de 2025  
**Versión:** 2.1 (Producción Estable)  
**Estado:** IMPLEMENTADO

---

## 🎯 Objetivo

Garantizar operación continua y estable del sistema de lectura DLMS con:
- ✅ 99%+ de éxito en publicación MQTT
- ✅ Auto-recuperación ante fallos
- ✅ Sin conflictos de recursos
- ✅ Monitoreo en tiempo real

---

## 🏗️ Arquitectura de Producción

### Diseño Final (Estable)

```
┌─────────────────────────────────────────────────────────────────┐
│  CAPA DE DISPOSITIVOS                                           │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │ Medidor 1    │  │ Medidor 2    │  │ Medidor N    │          │
│  │ DLMS/COSEM   │  │ DLMS/COSEM   │  │ DLMS/COSEM   │          │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘          │
└─────────┼──────────────────┼──────────────────┼─────────────────┘
          │                  │                  │
          │ TCP 3333         │ TCP 3333         │ TCP 3333
          │                  │                  │
┌─────────▼──────────────────▼──────────────────▼─────────────────┐
│  CAPA DE SERVICIO (1 PROCESO ÚNICO)                             │
│                                                                  │
│  dlms-multi-meter.service                                       │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  MultiMeterBridge                                          │ │
│  │  • Client ID único: dlms_multi_meter_bridge_XXXXX         │ │
│  │  • Token MQTT: QrKMI1jxYkK8hnDm3OD4                        │ │
│  │  • Clean session: True                                     │ │
│  │  • Auto-reconnect habilitado                               │ │
│  │                                                            │ │
│  │  Workers Asíncronos:                                       │ │
│  │  ├─ MeterWorker(1) → Medidor 1                            │ │
│  │  ├─ MeterWorker(2) → Medidor 2                            │ │
│  │  └─ MeterWorker(N) → Medidor N                            │ │
│  │                                                            │ │
│  │  Monitor Loop:                                             │ │
│  │  └─ Reportes cada 60s                                      │ │
│  └────────────────────────────────────────────────────────────┘ │
│                           │                                      │
│                           │ MQTT (1 conexión compartida)         │
│                           │                                      │
└───────────────────────────┼──────────────────────────────────────┘
                            │
                            │ localhost:1883
                            │
┌───────────────────────────▼──────────────────────────────────────┐
│  CAPA DE IOT                                                     │
│                                                                  │
│  ThingsBoard MQTT Broker                                        │
│  • 1 conexión única (sin conflictos)                            │
│  • Token validation                                             │
│  • Time-series storage                                          │
└──────────────────────────────────────────────────────────────────┘
```

### Servicios en Producción

| Servicio | Estado | Propósito | MQTT |
|----------|--------|-----------|------|
| `dlms-multi-meter.service` | ✅ ACTIVO | Lectura de medidores | SÍ (token único) |
| `dlms-dashboard.service` | ✅ ACTIVO | Web UI (puerto 8501) | NO |
| `dlms-admin-api.service` | ❌ DETENIDO | REST API | ~~SÍ~~ CONFLICTO |

**Decisión de Arquitectura:** 
- Solo `dlms-multi-meter.service` publica a MQTT
- API y Dashboard consultan base de datos SQLite
- Evita conflictos de token MQTT

---

## ✅ Mejoras Implementadas

### 1. Client ID Único en MQTT

**Problema anterior:**
```python
# Sin client_id → broker genera uno aleatorio
self.mqtt_client = mqtt.Client()
```

**Solución implementada:**
```python
# Client ID único basado en ID del objeto
client_id = f"dlms_multi_meter_bridge_{id(self)}"

self.mqtt_client = mqtt.Client(
    client_id=client_id,
    clean_session=True,  # Limpiar sesión previa
    protocol=mqtt.MQTTv311
)
```

**Beneficios:**
- ✅ Identificación única del cliente
- ✅ No hay conflictos con otros procesos
- ✅ Clean session elimina estado previo corrupto

### 2. Detección de Conflictos MQTT

**Callbacks mejorados:**
```python
def on_disconnect(client, userdata, rc):
    if rc != 0:
        logger.warning(f"⚠️  MQTT Disconnected: code {rc}")
        if rc == 7:
            logger.error("   🔴 CONFLICTO: Otro proceso usando mismo token")
            logger.error("   → Detener dlms-admin-api.service")
```

**Beneficios:**
- ✅ Identifica inmediatamente conflictos (código 7)
- ✅ Sugiere solución específica
- ✅ Logs claros para troubleshooting

### 3. Métricas de Salud MQTT

**Alerta automática:**
```python
mqtt_rate = (total_messages_sent / total_cycles * 100)

if mqtt_rate < 50 and total_cycles >= 20:
    logger.error(
        f"🔴 ALERTA: Solo {mqtt_rate:.1f}% de ciclos publican a MQTT"
    )
```

**Beneficios:**
- ✅ Detecta problemas en segundos (no horas)
- ✅ Alerta proactiva antes de pérdida masiva de datos
- ✅ Facilita diagnóstico rápido

### 4. Logging Detallado (Modo DEBUG)

**Nivel DEBUG habilitado:**
```python
logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s - [%(name)s] - %(levelname)s - %(message)s'
)
```

**Información adicional:**
- 🔍 Valores retornados por `poll_once()`
- 🔍 Estado de conexión MQTT en cada ciclo
- 🔍 Contenido de telemetry antes de publicar
- 🔍 Razones de fallas de publicación

---

## 🛠️ Herramientas de Monitoreo

### 1. Script de Verificación de Salud

**Archivo:** `check_system_health.sh`

**Verifica:**
- ✅ Estado de servicios systemd
- ✅ Conexiones MQTT activas (debe ser 1)
- ✅ Procesos Python sospechosos
- ✅ Errores MQTT código 7 recientes
- ✅ Tasa de publicación MQTT
- ✅ Conectividad con medidor

**Uso:**
```bash
chmod +x check_system_health.sh
./check_system_health.sh

# Output:
# ✅ Sistema saludable - No se detectaron problemas (exit 0)
# ⚠️  Se detectaron N problema(s) menores (exit 1)
# 🔴 Se detectaron N problemas críticos (exit 2)
```

**Automatización (cron):**
```bash
# Ejecutar cada 5 minutos
*/5 * * * * /path/to/check_system_health.sh >> /var/log/dlms_health.log 2>&1
```

### 2. Comandos de Monitoreo Rápido

```bash
# Ver estadísticas en tiempo real
./service-manager.sh watch

# Ver ratio Ciclos/MQTT
sudo journalctl -u dlms-multi-meter.service -f | grep "Cycles.*MQTT"

# Buscar conflictos MQTT
sudo journalctl -u dlms-multi-meter.service --since "5 minutes ago" | grep "code 7"

# Ver alertas de bajo rate
sudo journalctl -u dlms-multi-meter.service -f | grep "ALERTA"

# Estado completo del sistema
./check_system_health.sh
```

---

## 🚀 Procedimientos de Operación

### Inicio del Sistema (Primera Vez)

```bash
# 1. Detener servicios conflictivos
sudo systemctl stop dlms-admin-api.service

# 2. Asegurar que solo multi-meter esté activo
sudo systemctl enable dlms-multi-meter.service
sudo systemctl start dlms-multi-meter.service

# 3. Verificar salud
./check_system_health.sh

# 4. Monitorear primeros 2 minutos
sudo journalctl -u dlms-multi-meter.service -f

# Buscar:
# ✅ "✅ MQTT Connected to localhost:1883"
# ✅ "Cycles: 10 | MQTT: 9 msgs (90%+)"
# ❌ NO debe aparecer "Disconnected: 7"
```

### Reinicio del Sistema

```bash
# Opción 1: Reinicio limpio
sudo systemctl restart dlms-multi-meter.service

# Opción 2: Reinicio forzado (si hay problemas)
sudo systemctl stop dlms-multi-meter.service
sleep 5
sudo systemctl start dlms-multi-meter.service

# Verificar
./check_system_health.sh
```

### Agregar Nuevo Medidor

```bash
# 1. Agregar medidor en dashboard (http://localhost:8501)
#    - Nombre: medidor_nuevo
#    - IP: 192.168.1.XXX
#    - Puerto: 3333
#    - Configurar mediciones

# 2. Reiniciar servicio para cargar nuevo medidor
sudo systemctl restart dlms-multi-meter.service

# 3. Verificar en logs
sudo journalctl -u dlms-multi-meter.service -f | grep "medidor_nuevo"

# Buscar:
# ✅ "✓ Poller created for 192.168.1.XXX:3333"
# ✅ "✅ Connected to DLMS meter"
# ✅ "🚀 Starting polling loop"
```

### Resolución de Conflictos MQTT

**Síntoma:**
```
⚠️  MQTT Disconnected: 7
🔴 CONFLICTO: Otro proceso usando mismo token
```

**Solución:**
```bash
# 1. Identificar proceso conflictivo
./check_system_health.sh

# 2. Detener servicio conflictivo
sudo systemctl stop dlms-admin-api.service

# 3. Reiniciar servicio principal
sudo systemctl restart dlms-multi-meter.service

# 4. Verificar resolución (debe estar en 100%)
sleep 30
./check_system_health.sh
```

---

## 📊 Métricas de Producción

### KPIs Objetivo

| Métrica | Objetivo | Crítico si < |
|---------|----------|--------------|
| Tasa éxito lecturas | 99%+ | 95% |
| Tasa publicación MQTT | 95%+ | 90% |
| Latencia por lectura | < 3s | > 5s |
| Desconexiones MQTT/hora | 0 | > 2 |
| Uptime del servicio | 99.9%+ | 99% |

### Monitoreo Continuo

**Dashboard recomendado:**
```
┌─────────────────────────────────────────────────────────────┐
│ DLMS Multi-Meter System - Production Status                │
├─────────────────────────────────────────────────────────────┤
│ Service Status:        ✅ Running (uptime: 48h 23m)        │
│ Active Meters:         3/3                                  │
│ Total Cycles (24h):    259,200                              │
│ Success Rate:          99.7%                                │
│ MQTT Publish Rate:     98.9%                                │
│ Avg Read Latency:      1.8s                                 │
│ Last MQTT Error:       None (72h ago)                       │
│ Memory Usage:          42 MB / 500 MB                       │
│ CPU Usage:             3.2%                                 │
└─────────────────────────────────────────────────────────────┘
```

**Implementación con Grafana:**
- Fuente: InfluxDB (métricas exportadas)
- Queries sobre tabla `meter_metrics`
- Alertas configuradas en Prometheus

---

## 🔒 Seguridad y Backups

### Backup de Configuración

```bash
# Script de backup diario
#!/bin/bash
BACKUP_DIR="/var/backups/dlms"
DATE=$(date +%Y%m%d_%H%M%S)

# Backup de base de datos
cp data/admin.db "$BACKUP_DIR/admin_${DATE}.db"

# Backup de configuración de servicios
cp /etc/systemd/system/dlms-*.service "$BACKUP_DIR/"

# Rotar backups (mantener últimos 30 días)
find "$BACKUP_DIR" -name "admin_*.db" -mtime +30 -delete
```

### Monitoreo de Seguridad

```bash
# Verificar que solo proceso autorizado use MQTT
ps aux | grep python | grep -v grep | grep -v dlms-multi-meter

# Verificar conexiones externas (solo debe haber a medidores)
sudo netstat -tupan | grep python

# Revisar intentos fallidos de autenticación DLMS
sudo journalctl -u dlms-multi-meter.service | grep "authentication failed"
```

---

## 🐛 Troubleshooting de Producción

### Problema 1: Baja Tasa de Publicación MQTT

**Síntoma:**
```
📊 Cycles: 100 | MQTT: 15 msgs (15%)
🔴 ALERTA: Solo 15% de ciclos publican a MQTT
```

**Diagnóstico:**
```bash
# 1. Verificar conflictos
./check_system_health.sh

# 2. Buscar código 7
sudo journalctl -u dlms-multi-meter.service --since "10 minutes ago" | grep "code 7"
```

**Solución:**
```bash
# Detener servicios conflictivos
sudo systemctl stop dlms-admin-api.service
sudo systemctl restart dlms-multi-meter.service
```

### Problema 2: Medidor No Responde

**Síntoma:**
```
⚠️  No readings returned
```

**Diagnóstico:**
```bash
# Test directo del medidor
python3 test_meter_health.py
```

**Soluciones posibles:**
- Reiniciar medidor físicamente
- Verificar red (ping 192.168.1.127)
- Revisar buffer TCP (puede estar sucio)
- Incrementar timeout en configuración

### Problema 3: Memory Leak

**Síntoma:**
```
Memory usage: 450 MB / 500 MB (90%)
```

**Diagnóstico:**
```bash
# Ver consumo de memoria
systemctl status dlms-multi-meter.service | grep Memory

# Ver objetos Python en memoria
sudo py-spy dump --pid $(pgrep -f dlms_multi_meter_bridge)
```

**Solución:**
```bash
# Reinicio periódico programado (si es necesario)
# En crontab:
0 3 * * * /usr/bin/systemctl restart dlms-multi-meter.service
```

---

## 📚 Checklist de Producción

### Pre-Deployment

- [ ] Base de datos inicializada con medidores
- [ ] Servicios systemd instalados y configurados
- [ ] Solo `dlms-multi-meter.service` habilitado para MQTT
- [ ] `dlms-admin-api.service` detenido y deshabilitado
- [ ] Token MQTT válido configurado
- [ ] Script `check_system_health.sh` ejecutable
- [ ] Medidores accesibles en red
- [ ] ThingsBoard broker funcionando

### Post-Deployment

- [ ] Servicio inicia correctamente
- [ ] MQTT conecta sin código 7
- [ ] Tasa de publicación > 95%
- [ ] Lecturas de medidor correctas
- [ ] Logs sin errores críticos
- [ ] Dashboard muestra datos en tiempo real
- [ ] Monitoreo automatizado configurado
- [ ] Backups programados

### Mantenimiento Mensual

- [ ] Revisar logs de últimos 30 días
- [ ] Verificar estadísticas de rendimiento
- [ ] Actualizar dependencias Python
- [ ] Rotar logs antiguos
- [ ] Verificar espacio en disco
- [ ] Test de failover
- [ ] Revisar backups

---

## 🎓 Lecciones Aprendidas

### 1. Conflicto de Token MQTT (Crítico)

**Problema:** Dos servicios usando mismo token → 99% pérdida de datos

**Solución:** 
- Client ID único
- Solo un servicio con MQTT
- Detección temprana con alertas

**Prevención:**
- Documentar claramente qué servicio publica
- Deshabilitar MQTT en servicios no críticos
- Monitoreo automático de conflictos

### 2. Buffer TCP Sucio en Medidor

**Problema:** Frame boundaries inválidos → reconexiones frecuentes

**Solución:**
- Drenaje preventivo cada 45s
- SO_LINGER en cierre de socket
- Reset de secuencias DLMS

**Prevención:**
- Siempre cerrar conexiones limpiamente
- Timeout adecuado (3s)
- Logs detallados de errores HDLC

### 3. Logging Inadecuado

**Problema:** No se detectaban problemas hasta horas después

**Solución:**
- Logs DEBUG en desarrollo
- Alertas automáticas en producción
- Métricas en cada reporte de ciclo

**Prevención:**
- Logging estructurado desde el inicio
- Métricas clave siempre visibles
- Dashboards en tiempo real

---

## 📞 Contacto y Soporte

**Mantenedor:** Sebastian Giraldo  
**Repositorio:** https://github.com/jsebgiraldo/Tesis-app  
**Documentación:** Ver `/docs`

**En caso de problemas:**
1. Ejecutar `./check_system_health.sh`
2. Revisar logs: `sudo journalctl -u dlms-multi-meter.service -n 100`
3. Consultar esta guía de troubleshooting
4. Reportar issue en GitHub con logs completos

---

**Última actualización:** 31 de Octubre de 2025  
**Versión del sistema:** 2.1 (Producción Estable)
