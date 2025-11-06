# ThingsBoard Gateway - DLMS Connector
## Quick Start Guide

### 🚀 Lo que hemos creado

Este gateway te permite conectar múltiples medidores DLMS a ThingsBoard usando la arquitectura oficial de ThingsBoard IoT Gateway.

### 📁 Estructura Creada

```
gateway/
├── config/
│   ├── tb_gateway.yaml              # Configuración principal
│   ├── tb_gateway.yaml.example      # Ejemplo de configuración
│   ├── dlms_connector.json          # Configuración de medidores
│   └── dlms_connector.json.example  # Ejemplo
│
├── connectors/
│   ├── dlms_connector.py            # Conector personalizado
│   └── __init__.py
│
├── setup_gateway.sh                 # Instalación completa (producción)
├── start_gateway.sh                 # Inicio rápido (desarrollo)
├── test_config.py                   # Test de configuración
├── requirements-gateway.txt         # Dependencies
│
├── README.md                        # Documentación completa
├── ARCHITECTURE.md                  # Comparación con sistema actual
└── QUICKSTART.md                    # Esta guía
```

---

## ⚡ Inicio Rápido (5 minutos)

### 1. Preparar ThingsBoard

1. Ve a tu ThingsBoard: https://demo.thingsboard.io (o tu servidor)
2. **Devices** → **Add Device** (+)
3. Name: `DLMS Gateway`
4. **Marcar: "Is gateway"** ✅
5. Copiar **Access Token**

### 2. Configurar Gateway

```bash
cd gateway/

# Editar configuración principal
nano config/tb_gateway.yaml
```

Pegar tu token:
```yaml
thingsboard:
  host: demo.thingsboard.io  # o tu servidor
  security:
    accessToken: "PEGAR_TU_TOKEN_AQUI"
```

### 3. Configurar Medidores

```bash
# Editar configuración de medidores
nano config/dlms_connector.json
```

Actualizar con tus medidores:
```json
{
  "devices": [
    {
      "name": "mi_medidor",
      "host": "192.168.1.127",
      "port": 3333,
      "measurements": ["voltage_l1", "current_l1", "active_power"]
    }
  ]
}
```

### 4. Probar Configuración

```bash
# Test de configuración
python3 test_config.py
```

Deberías ver:
```
✓ Found: tb_gateway.yaml
✓ ThingsBoard host: demo.thingsboard.io
✓ Access token configured: A1B2C3D4E5...
✓ DLMS Connector configured
✓ Found 1 DLMS device(s)
✓ TCP connection successful

All tests passed! ✓
```

### 5. Iniciar Gateway

**Opción A: Modo Desarrollo (recomendado para pruebas)**
```bash
./start_gateway.sh
```

**Opción B: Modo Producción (systemd)**
```bash
sudo ./setup_gateway.sh install
sudo systemctl start tb-gateway.service
```

### 6. Verificar en ThingsBoard

1. Ve a **Devices** en ThingsBoard
2. Deberías ver:
   - ✅ `DLMS Gateway` (el gateway)
   - ✅ `mi_medidor` (tu medidor, como hijo del gateway)
3. Abre `mi_medidor` → **Latest telemetry**
4. ¡Deberías ver datos actualizándose! 🎉

---

## 🔧 Configuración Avanzada

### Agregar más medidores

Edita `config/dlms_connector.json`:

```json
{
  "devices": [
    {
      "name": "medidor_1",
      "host": "192.168.1.127",
      "port": 3333,
      "pollingInterval": 5000,
      "measurements": ["voltage_l1", "current_l1"]
    },
    {
      "name": "medidor_2",
      "host": "192.168.1.128",
      "port": 3333,
      "pollingInterval": 5000,
      "measurements": ["voltage_l1", "current_l1"]
    }
  ]
}
```

Reinicia el gateway:
```bash
sudo systemctl restart tb-gateway.service
# o Ctrl+C y ./start_gateway.sh en modo desarrollo
```

### Mediciones disponibles

```python
measurements = [
    "voltage_l1", "voltage_l2", "voltage_l3",     # Voltajes (V)
    "current_l1", "current_l2", "current_l3",     # Corrientes (A)
    "active_power",                                # Potencia activa (W)
    "reactive_power",                              # Potencia reactiva (VAr)
    "apparent_power",                              # Potencia aparente (VA)
    "power_factor",                                # Factor de potencia
    "frequency",                                   # Frecuencia (Hz)
    "active_energy",                               # Energía acumulada (kWh)
]
```

---

## 🔍 Troubleshooting

### Error: "Gateway access token not configured"

```bash
# Editar config
nano config/tb_gateway.yaml

# Verificar que no sea el token por defecto:
# ❌ accessToken: "YOUR_GATEWAY_ACCESS_TOKEN"
# ✅ accessToken: "A1B2C3D4E5F6..."
```

### Error: "Cannot connect to device"

```bash
# Test conectividad
ping 192.168.1.127
nc -zv 192.168.1.127 3333

# Si falla, verificar:
# - IP correcta en dlms_connector.json
# - Medidor encendido y accesible
# - Firewall no bloqueando puerto 3333
```

### Error: "ModuleNotFoundError: No module named 'thingsboard_gateway'"

```bash
# Instalar dependencies
pip install -r requirements-gateway.txt

# O instalación manual
pip install thingsboard-gateway
```

### Ver logs del gateway

```bash
# Modo desarrollo: verás logs en la consola

# Modo producción:
sudo journalctl -u tb-gateway.service -f

# Filtrar errores:
sudo journalctl -u tb-gateway.service | grep ERROR
```

---

## 📊 Comparación con Sistema Actual

### ¿Cuándo usar Gateway?

✅ **Usar Gateway si:**
- Tienes 10+ medidores
- Quieres gestión centralizada
- Prefieres arquitectura estándar
- Planeas agregar otros protocolos

✅ **Usar sistema actual si:**
- Tienes pocos medidores (1-5)
- Necesitas control total sobre MQTT
- Ya tienes tokens individuales configurados

### ¿Pueden coexistir?

**¡Sí!** Puedes tener ambos simultáneamente:

```
ThingsBoard:
├── medidor_1 (token individual) ← dlms_multi_meter_bridge.py
├── medidor_2 (token individual) ← dlms_multi_meter_bridge.py
└── DLMS_Gateway (token gateway)
    ├── medidor_3 ← Gateway
    └── medidor_4 ← Gateway
```

No hay conflicto porque usan tokens diferentes.

---

## 📚 Más Información

- **Documentación completa**: [README.md](README.md)
- **Arquitectura y comparación**: [ARCHITECTURE.md](ARCHITECTURE.md)
- **ThingsBoard Gateway Docs**: https://thingsboard.io/docs/iot-gateway/

---

## ✅ Checklist de Setup

- [ ] ThingsBoard Gateway device creado
- [ ] Access token copiado
- [ ] `config/tb_gateway.yaml` configurado con token
- [ ] `config/dlms_connector.json` con tus medidores
- [ ] `python3 test_config.py` pasa todos los tests
- [ ] Gateway iniciado (`./start_gateway.sh`)
- [ ] Dispositivos visibles en ThingsBoard
- [ ] Telemetría actualizándose

---

¡Listo! 🎉 Ahora tienes un gateway ThingsBoard funcionando con tus medidores DLMS.

**¿Dudas?** Revisa [README.md](README.md) o [ARCHITECTURE.md](ARCHITECTURE.md).
