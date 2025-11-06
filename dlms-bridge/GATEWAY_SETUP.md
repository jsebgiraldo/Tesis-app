# 🌉 ThingsBoard Gateway - DLMS Bridge

## 📋 Resumen

Este servicio conecta medidores DLMS directamente al **ThingsBoard Gateway** (MQTT), reemplazando la implementación anterior que publicaba directamente al servidor ThingsBoard.

**Arquitectura:**
```
[Medidor DLMS] ←→ [simple_gateway_bridge.py] ←→ [Gateway MQTT ThingsBoard] ←→ [Servidor ThingsBoard]
  192.168.1.127                                      localhost:1883
```

**Beneficios:**
- ✅ Usa el Gateway oficial de ThingsBoard (connector MQTT)
- ✅ Sin conflictos con servicios anteriores
- ✅ Código simple y robusto (tu `dlms_reader.py` + `tb_mqtt_client.py`)
- ✅ Gestión centralizada en ThingsBoard
- ✅ Servicio systemd con auto-reinicio

---

## 🚀 Instalación y Arranque

### 1. Detener servicios anteriores (si existen)

```bash
cd /home/pci/Documents/sebas_giraldo/Tesis-app/dlms-bridge

# Detener servicios viejos y procesos DLMS anteriores
sudo ./manage-gateway-service.sh stop-old

# Verificar que no hay procesos DLMS corriendo
ps aux | grep -E "dlms|bridge" | grep -v grep

# Si hay algún proceso, detenerlo:
# sudo kill -TERM <PID>
```

### 2. Instalar servicio Gateway Bridge

```bash
# Instalar servicio en systemd
sudo ./manage-gateway-service.sh install

# Iniciar servicio
sudo ./manage-gateway-service.sh start

# Ver logs en vivo
./manage-gateway-service.sh follow
```

**Deberías ver:**
```
2025-11-04 10:52:40 [INFO] ✅ DLMS conectado
2025-11-04 10:52:40 [INFO] ✅ MQTT Gateway conectado
2025-11-04 10:52:44 [INFO] 📤 VOL: 133.99 | CUR: 1.32 | FRE: 59.97 | ACT: 0.70 | ACT: 56348.00
```

---

## 🛠️ Gestión del Servicio

### Comandos disponibles

```bash
# Ver estado
./manage-gateway-service.sh status

# Reiniciar
sudo ./manage-gateway-service.sh restart

# Detener
sudo ./manage-gateway-service.sh stop

# Ver últimos logs (50 líneas)
./manage-gateway-service.sh logs

# Seguir logs en tiempo real
./manage-gateway-service.sh follow
```

### Logs del servicio

```bash
# Ver logs con journalctl
sudo journalctl -u tb-gateway-dlms.service -f

# Ver logs de hoy
sudo journalctl -u tb-gateway-dlms.service --since today

# Ver últimos 100 logs
sudo journalctl -u tb-gateway-dlms.service -n 100
```

---

## 🔧 Configuración

### Archivo de servicio

**Ubicación:** `/etc/systemd/system/tb-gateway-dlms.service`

Parámetros importantes:
```ini
WorkingDirectory=/home/pci/Documents/sebas_giraldo/Tesis-app/dlms-bridge
ExecStart=.../venv/bin/python3 simple_gateway_bridge.py --meter-id 1 --interval 5.0
Restart=on-failure
RestartSec=10
```

### Base de datos

**Archivo:** `data/admin.db`

**Tabla `meters`:**
- `id`: ID del medidor (default: 1)
- `name`: Nombre del medidor
- `ip_address`: IP del medidor DLMS (ej: `192.168.1.127`)
- `port`: Puerto DLMS (default: `3333`)
- `tb_token`: Token del Gateway en ThingsBoard (`oCS3U0Grx4URhssER2QX`)
- `tb_host`: Host del broker MQTT (`localhost`)
- `tb_port`: Puerto MQTT (`1883`)

**Actualizar configuración:**
```python
from admin.database import Database, get_meter_by_id

db = Database('data/admin.db')
session = db.get_session()

meter = get_meter_by_id(session, 1)
meter.tb_token = 'NUEVO_TOKEN_AQUI'
meter.tb_host = 'localhost'
meter.tb_port = 1883
session.commit()
session.close()
```

### Parámetros del bridge

```bash
# Cambiar medidor
sudo systemctl edit tb-gateway-dlms.service
# Modificar: --meter-id 2

# Cambiar intervalo de polling
# Modificar: --interval 10.0

# Luego recargar
sudo systemctl daemon-reload
sudo systemctl restart tb-gateway-dlms.service
```

---

## 📊 Verificación en ThingsBoard

### 1. Acceder a ThingsBoard

```bash
# Si es local:
http://localhost:8080

# O tu servidor remoto
```

### 2. Ver Gateway

**Ruta:** Devices → Gateways → `DLMS-BRIDGE`

**Verificar:**
- ✅ Estado: "Connected" (verde)
- ✅ Última actividad: Hace < 1 minuto
- ✅ Telemetry: `voltage_l1`, `current_l1`, `frequency`, `active_power`, `active_energy`

### 3. Ver telemetría

**Ruta:** Devices → `DLMS-BRIDGE` → Latest Telemetry

**Deberías ver:**
```
voltage_l1: 134.08 V
current_l1: 1.32 A
frequency: 60.01 Hz
active_power: 0.50 W
active_energy: 56348.00 Wh
```

---

## 🐛 Troubleshooting

### Servicio no inicia

```bash
# Ver error específico
sudo systemctl status tb-gateway-dlms.service

# Ver logs detallados
sudo journalctl -u tb-gateway-dlms.service -n 50

# Verificar archivo de servicio
cat /etc/systemd/system/tb-gateway-dlms.service

# Probar manualmente
cd /home/pci/Documents/sebas_giraldo/Tesis-app/dlms-bridge
source venv/bin/activate
python3 simple_gateway_bridge.py --meter-id 1 --interval 5.0
```

### No se conecta a DLMS

**Síntoma:** `❌ Error DLMS: timed out`

**Soluciones:**
```bash
# 1. Verificar que el medidor esté accesible
ping 192.168.1.127

# 2. Verificar que no haya otro proceso usando el medidor
ps aux | grep -E "dlms|bridge" | grep -v grep
# Si hay alguno, detenerlo:
sudo kill -TERM <PID>

# 3. Probar conexión manual
cd /home/pci/Documents/sebas_giraldo/Tesis-app/dlms-bridge
source venv/bin/activate
python3 -c "
from dlms_reader import DLMSClient
client = DLMSClient('192.168.1.127', 3333, 1, 0, 1, b'22222222', 5.0, None)
client.connect()
print('✅ Conectado')
client.close()
"
```

### No se conecta a MQTT

**Síntoma:** `❌ Error MQTT: Timeout`

**Soluciones:**
```bash
# 1. Verificar que el broker MQTT esté corriendo
sudo systemctl status mosquitto
# O si es otro broker:
netstat -tuln | grep 1883

# 2. Verificar token del Gateway
sqlite3 data/admin.db "SELECT tb_token, tb_host, tb_port FROM meters WHERE id=1;"

# 3. Probar conexión MQTT manual
mosquitto_sub -h localhost -p 1883 -u oCS3U0Grx4URhssER2QX -t '#' -v
```

### Gateway conectado pero sin datos

**Síntoma:** Gateway "Connected" en ThingsBoard pero sin telemetría

**Soluciones:**
```bash
# 1. Ver logs del servicio
./manage-gateway-service.sh follow

# 2. Verificar que esté publicando
# Deberías ver: 📤 VOL: 134.08 | CUR: 1.32 | ...

# 3. Verificar en MQTT directamente
mosquitto_sub -h localhost -p 1883 -u oCS3U0Grx4URhssER2QX -t 'v1/devices/me/telemetry' -v

# 4. Reiniciar servicio
sudo ./manage-gateway-service.sh restart
```

### Lecturas intermitentes

**Síntoma:** Algunas lecturas fallan (⚠️ Sin datos DLMS)

**Causas comunes:**
- Timeout del medidor (normal, el bridge reconectará automáticamente)
- Interferencia de red
- Medidor ocupado procesando otra solicitud

**Monitoreo:**
```bash
# Ver tasa de éxito
./manage-gateway-service.sh logs | grep "📊 FINAL"

# Debería ser > 70%:
# 📊 FINAL: 18/20 exitosos (90.0%)
```

---

## 📁 Archivos del Proyecto

```
dlms-bridge/
├── simple_gateway_bridge.py           # ✅ Bridge principal (ESTE ES EL QUE USA EL SERVICIO)
├── dlms_reader.py                     # Cliente DLMS robusto (copiado de protocols/)
├── tb_mqtt_client.py                  # Cliente MQTT para ThingsBoard
├── tb-gateway-dlms.service            # Definición del servicio systemd
├── manage-gateway-service.sh          # Script de gestión del servicio
├── gateway_dlms_bridge.py             # Versión alternativa (usa dlms-cosem oficial)
├── admin/
│   └── database.py                    # Gestión de base de datos
├── data/
│   └── admin.db                       # Base de datos SQLite
└── venv/                               # Entorno virtual Python
```

---

## 🔄 Migración desde Servicios Anteriores

Si tenías `dlms-mqtt-bridge.service` o `dlms-admin-api.service`:

```bash
# 1. Detener servicios viejos
sudo systemctl stop dlms-mqtt-bridge.service dlms-admin-api.service
sudo systemctl disable dlms-mqtt-bridge.service dlms-admin-api.service

# 2. Verificar que no hay procesos corriendo
ps aux | grep dlms_multi_meter_bridge
# Si hay alguno:
sudo kill -TERM <PID>

# 3. Instalar nuevo servicio
cd /home/pci/Documents/sebas_giraldo/Tesis-app/dlms-bridge
sudo ./manage-gateway-service.sh install
sudo ./manage-gateway-service.sh start

# 4. Verificar
./manage-gateway-service.sh status
./manage-gateway-service.sh follow
```

**Diferencias principales:**
| Anterior | Nuevo |
|----------|-------|
| Publicaba directamente a ThingsBoard | Publica al Gateway MQTT |
| Token del dispositivo | Token del Gateway |
| Multiple servicios (bridge + admin) | Un solo servicio |
| Código complejo con dependencias | Código simple sin dependencias extras |

---

## 📈 Monitoreo y Métricas

### Estadísticas del servicio

```bash
# Ver estadísticas cada 20 ciclos
./manage-gateway-service.sh follow | grep "📊"

# Ejemplo de salida:
# 📊 18/20 exitosos (90.0%)
# 📊 38/40 exitosos (95.0%)
```

### Alarmas recomendadas

**En ThingsBoard:**
1. Gateway desconectado > 5 minutos
2. Sin telemetría > 10 minutos
3. Valores fuera de rango (ej: voltage < 110V o > 150V)

**En el servidor:**
```bash
# Crear monitoreo con cron
crontab -e

# Añadir:
*/5 * * * * systemctl is-active tb-gateway-dlms.service || echo "⚠️ Servicio caído" | mail -s "DLMS Bridge Down" admin@example.com
```

---

## 📞 Soporte

**Logs importantes:**
```bash
# Servicio systemd
sudo journalctl -u tb-gateway-dlms.service -n 100

# Ver solo errores
sudo journalctl -u tb-gateway-dlms.service -p err -n 50

# Seguir en vivo
./manage-gateway-service.sh follow
```

**Información del sistema:**
```bash
# Versión de Python
python3 --version

# Paquetes instalados
source venv/bin/activate
pip list | grep -E "paho|dlms"

# Estado del servicio
systemctl status tb-gateway-dlms.service
```

---

## ✅ Checklist de Funcionamiento

- [ ] Servicio instalado: `systemctl list-unit-files | grep tb-gateway-dlms`
- [ ] Servicio activo: `systemctl is-active tb-gateway-dlms.service`
- [ ] Logs muestran conexión DLMS: `grep "DLMS conectado" <logs>`
- [ ] Logs muestran conexión MQTT: `grep "MQTT Gateway conectado" <logs>`
- [ ] Logs muestran publicaciones: `grep "📤" <logs>`
- [ ] Gateway "Connected" en ThingsBoard UI
- [ ] Telemetría visible en ThingsBoard
- [ ] Tasa de éxito > 70%

---

**Última actualización:** 2025-11-04  
**Versión del bridge:** 1.0.0  
**Autor:** Sebastian Giraldo
