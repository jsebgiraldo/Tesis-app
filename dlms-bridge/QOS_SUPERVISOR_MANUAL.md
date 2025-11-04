# 🛡️ QoS Supervisor - Sistema de Monitoreo y Diagnóstico

## Descripción

El QoS Supervisor es un servicio systemd que monitorea continuamente la salud del sistema DLMS-to-ThingsBoard y toma acciones correctivas automáticas cuando detecta problemas.

## Arquitectura

```
┌─────────────────────────────────────────────────────────────┐
│                   QoS Supervisor Service                    │
│                  (qos-supervisor.service)                   │
└────────────┬────────────────────────────────────────────────┘
             │
             ├─ Monitorea cada 30s:
             │  ├─ Estado de servicios (mosquitto, bridge, gateway)
             │  ├─ Frescura de telemetría (<60s)
             │  └─ Detección de estancamiento
             │
             ├─ Toma acciones automáticas:
             │  ├─ Reinicia bridge si telemetría estancada
             │  ├─ Reinicia mosquitto si broker caído
             │  └─ Reinicia gateway si servicio inactivo
             │
             └─ Registra todo en journald
                └─ Consulta vía qos-diagnostics.sh
```

## Instalación

El supervisor ya está instalado como servicio systemd:

```bash
# Ver estado
sudo systemctl status qos-supervisor.service

# Iniciar
sudo systemctl start qos-supervisor.service

# Detener
sudo systemctl stop qos-supervisor.service

# Reiniciar
sudo systemctl restart qos-supervisor.service

# Ver logs en vivo
sudo journalctl -u qos-supervisor.service -f
```

## Herramienta de Diagnóstico

### Uso básico

```bash
./qos-diagnostics.sh [comando] [opciones]
```

### Comandos disponibles

#### 1. Status - Estado actual
```bash
./qos-diagnostics.sh status
```
Muestra:
- Estado del servicio (activo/inactivo)
- Tiempo de inicio (uptime)
- Último check realizado
- Contadores de última hora (checks, errores, acciones)

#### 2. Logs - Ver logs completos
```bash
./qos-diagnostics.sh logs [minutos]

# Ejemplos:
./qos-diagnostics.sh logs 30   # Últimos 30 minutos
./qos-diagnostics.sh logs 120  # Últimas 2 horas
```

#### 3. Errors - Ver solo errores
```bash
./qos-diagnostics.sh errors [minutos]

# Ejemplos:
./qos-diagnostics.sh errors 60   # Errores de última hora
./qos-diagnostics.sh errors 480  # Errores de últimas 8 horas
```

#### 4. Actions - Ver acciones correctivas
```bash
./qos-diagnostics.sh actions [minutos]

# Ejemplos:
./qos-diagnostics.sh actions 30   # Acciones de últimos 30 min
./qos-diagnostics.sh actions 1440 # Acciones de últimas 24 horas
```

#### 5. Cycles - Ver ciclos completados
```bash
./qos-diagnostics.sh cycles [cantidad]

# Ejemplos:
./qos-diagnostics.sh cycles 5   # Últimos 5 ciclos
./qos-diagnostics.sh cycles 10  # Últimos 10 ciclos
```

#### 6. Live - Seguir logs en tiempo real
```bash
./qos-diagnostics.sh live
# Ctrl+C para salir
```

#### 7. Stats - Estadísticas
```bash
./qos-diagnostics.sh stats [horas]

# Ejemplos:
./qos-diagnostics.sh stats 24  # Últimas 24 horas
./qos-diagnostics.sh stats 48  # Últimas 48 horas
./qos-diagnostics.sh stats 168 # Última semana
```
Muestra:
- Total de checks realizados
- Ciclos completados
- Problemas detectados
- Acciones correctivas tomadas (desglosadas)
- Disponibilidad estimada (%)

#### 8. Restart/Start/Stop - Control del servicio
```bash
./qos-diagnostics.sh restart   # Reiniciar supervisor
./qos-diagnostics.sh start     # Iniciar supervisor
./qos-diagnostics.sh stop      # Detener supervisor
```

## Configuración

### Intervalos de monitoreo
Editar `/home/pci/Documents/sebas_giraldo/Tesis-app/dlms-bridge/qos_supervisor_service.py`:

```python
CHECK_INTERVAL = 30         # Segundos entre checks (default: 30)
TELEMETRY_MAX_AGE = 60      # Máximo edad de telemetría (default: 60s)
CYCLE_DURATION = 30 * 60    # Duración de cada ciclo (default: 30 min)
REST_DURATION = 5 * 60      # Descanso entre ciclos (default: 5 min)
```

Después de modificar, reiniciar:
```bash
sudo systemctl restart qos-supervisor.service
```

### Umbrales de acción correctiva

```python
# En qos_supervisor_service.py, método take_corrective_action():

if self.failed_checks >= 1:  # Cambiar para más/menos sensibilidad
    # Reiniciar bridge
```

## Detección de Problemas

El supervisor detecta automáticamente:

### 1. Servicios caídos
- ❌ Mosquitto inactivo
- ❌ DLMS Bridge inactivo  
- ❌ ThingsBoard Gateway inactivo

### 2. Problemas de telemetría
- ⚠️ Datos obsoletos (>60s sin actualizar)
- ⚠️ Datos estancados (mismo timestamp repetido)
- ❌ Error de conexión a ThingsBoard
- ❌ Dispositivo no encontrado

### 3. Problemas de red
- ⚠️ Latencia alta al medidor DLMS
- ❌ Medidor DLMS no alcanzable

## Acciones Correctivas Automáticas

### Telemetría estancada o obsoleta
```
Detección → Fallo #1 → Reinicia bridge → Espera 10s → Verifica
```

### Servicio caído
```
Detección → Intenta reinicio → Espera 5s → Verifica estado
```

### Broker MQTT caído
```
Detección → Reinicia Mosquitto → Reinicia Bridge → Verifica
```

## Logs y Journald

Todos los eventos se registran en journald con formato estructurado:

### Formato de logs
```
[timestamp] [emoji] mensaje
```

Emojis usados:
- ℹ️ Información
- ✅ Éxito
- ⚠️ Advertencia
- ❌ Error
- ⚡ Acción correctiva

### Consultas útiles

```bash
# Últimos 100 logs
sudo journalctl -u qos-supervisor.service -n 100

# Logs desde una fecha
sudo journalctl -u qos-supervisor.service --since "2025-11-04 10:00"

# Logs hasta una fecha
sudo journalctl -u qos-supervisor.service --until "2025-11-04 12:00"

# Logs entre fechas
sudo journalctl -u qos-supervisor.service --since "2025-11-04 10:00" --until "2025-11-04 12:00"

# Solo errores
sudo journalctl -u qos-supervisor.service | grep "❌"

# Solo acciones
sudo journalctl -u qos-supervisor.service | grep "⚡"

# Exportar a archivo
sudo journalctl -u qos-supervisor.service > supervisor_logs.txt
```

## Ejemplos de Uso

### Monitoreo diario
```bash
# Por la mañana, revisar estado
./qos-diagnostics.sh status

# Ver si hubo problemas en la noche
./qos-diagnostics.sh errors 480  # Últimas 8 horas

# Ver estadísticas del día anterior
./qos-diagnostics.sh stats 24
```

### Diagnóstico de problema
```bash
# Usuario reporta que no hay datos
./qos-diagnostics.sh status      # Ver estado actual
./qos-diagnostics.sh logs 15     # Ver qué pasó últimos 15 min
./qos-diagnostics.sh actions 60  # Ver si se tomaron acciones

# Si es necesario, reiniciar manualmente
./qos-diagnostics.sh restart
```

### Análisis de disponibilidad
```bash
# Disponibilidad semanal
./qos-diagnostics.sh stats 168

# Ver cuántos ciclos se completaron sin problemas
./qos-diagnostics.sh cycles 50 | grep "Problemas: 0"
```

### Seguimiento en tiempo real
```bash
# Durante pruebas o debugging
./qos-diagnostics.sh live

# En otra terminal, simular problemas
sudo systemctl stop dlms-mosquitto-bridge.service

# Observar cómo el supervisor detecta y corrige
```

## Mantenimiento

### Rotación de logs
Journald rota logs automáticamente. Configuración en `/etc/systemd/journald.conf`:

```ini
[Journal]
SystemMaxUse=500M          # Máximo espacio en disco
SystemKeepFree=1G          # Espacio libre mínimo
SystemMaxFileSize=100M     # Tamaño máximo por archivo
MaxRetentionSec=604800     # Retener 7 días
```

### Actualizar supervisor
```bash
cd /home/pci/Documents/sebas_giraldo/Tesis-app/dlms-bridge

# Editar qos_supervisor_service.py según necesidad
nano qos_supervisor_service.py

# Reiniciar para aplicar cambios
sudo systemctl restart qos-supervisor.service

# Verificar que arrancó correctamente
./qos-diagnostics.sh status
```

### Desactivar temporalmente
```bash
# Detener supervisor
sudo systemctl stop qos-supervisor.service

# Hacer mantenimiento manual...

# Reiniciar supervisor
sudo systemctl start qos-supervisor.service
```

## Integración con Monitoreo Externo

### Exportar métricas
```bash
# Script para exportar métricas diarias
#!/bin/bash
./qos-diagnostics.sh stats 24 > /var/log/qos-metrics-$(date +%Y%m%d).txt
```

### Alertas por email
Agregar a `qos_supervisor_service.py`:

```python
def send_alert(self, message):
    # Implementar envío de email
    pass

def take_corrective_action(self, issue):
    # Al detectar problema crítico
    if self.failed_checks >= 5:
        self.send_alert(f"Problema crítico: {issue}")
```

## Troubleshooting

### Supervisor no inicia
```bash
# Ver logs de error
sudo journalctl -u qos-supervisor.service -n 50

# Verificar permisos
ls -l /etc/sudoers.d/qos-supervisor

# Verificar sintaxis de Python
python3 qos_supervisor_service.py
```

### No reinicia servicios automáticamente
```bash
# Verificar permisos sudo
sudo -l | grep systemctl

# Probar manualmente
sudo systemctl restart dlms-mosquitto-bridge.service
```

### Consumo de CPU alto
```bash
# Ver estadísticas del proceso
systemctl status qos-supervisor.service

# Aumentar CHECK_INTERVAL en configuración
# De 30s a 60s para reducir frecuencia
```

## Mejores Prácticas

1. **Revisión diaria**: Ejecutar `./qos-diagnostics.sh status` cada mañana
2. **Análisis semanal**: Revisar `./qos-diagnostics.sh stats 168` los lunes
3. **Backup de logs**: Exportar logs importantes antes de rotación
4. **Ajuste de umbrales**: Afinar según comportamiento del sistema
5. **Documentar incidentes**: Registrar problemas recurrentes
6. **Mantenimiento preventivo**: Si >5 reinicios/día, investigar causa raíz

## Soporte

Para problemas o mejoras:
1. Revisar logs: `./qos-diagnostics.sh logs 60`
2. Ver estado: `./qos-diagnostics.sh status`
3. Consultar documentación en `/docs`
4. Revisar código fuente: `qos_supervisor_service.py`
