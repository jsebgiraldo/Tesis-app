# DLMS to MQTT Bridge

Un bridge que recolecta datos de medidores DLMS/COSEM (específicamente medidores Microstar) y los publica vía MQTT para integración con sistemas de IoT y análisis de datos.

## 🎯 Características

- ✅ Lectura de medidores DLMS/COSEM sobre TCP/IP
- ✅ Soporte específico para medidores Microstar Electric
- ✅ Publicación de telemetría vía MQTT
- ✅ Arquitectura async/await para alta eficiencia
- ✅ Manejo robusto de errores con reintentos automáticos
- ✅ Configuración flexible por variables de entorno
- ✅ Sin dependencias DLMS externas - usa implementación propia

## 📋 Requisitos

- Python 3.10 o superior
- Acceso TCP/IP al medidor DLMS
- Broker MQTT (ej: Mosquitto, HiveMQ)

## 🚀 Instalación

### 1. Crear entorno virtual

```bash
cd dlms-bridge
python3 -m venv .venv
source .venv/bin/activate  # En Linux/Mac
# o
.venv\Scripts\activate  # En Windows
```

### 2. Instalar dependencias

```bash
pip install -r requirements.txt
```

### 3. Configurar variables de entorno

```bash
cp .env.example .env
# Editar .env con tus parámetros
```

## ⚙️ Configuración

El archivo `.env` contiene todos los parámetros configurables:

### Configuración DLMS

```bash
DLMS_HOST=192.168.5.177          # IP del medidor
DLMS_PORT=3333                    # Puerto TCP
DLMS_CLIENT_SAP=1                 # Service Access Point del cliente
DLMS_SERVER_LOGICAL=0             # ⚠️ CRÍTICO: Debe ser 0 para Microstar
DLMS_SERVER_PHYSICAL=1            # Dirección física del servidor
DLMS_PASSWORD=22222222            # Contraseña de autenticación
DLMS_MEASUREMENTS=["voltage_l1", "current_l1"]  # Mediciones a leer
```

### Configuración MQTT

```bash
MQTT_HOST=localhost               # Broker MQTT
MQTT_PORT=1883                    # Puerto MQTT
MQTT_CLIENT_ID=dlms-bridge        # ID del cliente
MQTT_TOPIC=meters/{device_id}/telemetry  # Topic de publicación
DEVICE_ID=microstar-001           # Identificador del dispositivo
```

### Configuración de Control

```bash
POLL_PERIOD_S=5.0                 # Tiempo entre lecturas (segundos)
MAX_CONSEC_ERRORS=10              # Máx errores consecutivos antes de parar
VERBOSE=false                     # Logs detallados (incluye frames HDLC)
```

## 🏃 Ejecución

### Modo básico

```bash
python -m app.main
```

### Con logs detallados

```bash
VERBOSE=true python -m app.main
```

### Detener el servicio

Presiona `Ctrl+C` para detener de forma limpia.

## 📊 Formato de Datos

### Datos Publicados por MQTT

**Topic:** `meters/{device_id}/telemetry` (ej: `meters/microstar-001/telemetry`)

**Payload (JSON):**

```json
{
  "device_id": "microstar-001",
  "ts": 1728564123456,
  "seq": 42,
  "measurements": {
    "voltage_l1": {
      "obis": "1-1:32.7.0",
      "value": 135.4,
      "unit_code": 32,
      "raw_value": 13540,
      "description": "Phase A instantaneous voltage"
    },
    "current_l1": {
      "obis": "1-1:31.7.0",
      "value": 1.302,
      "unit_code": 33,
      "raw_value": 1302,
      "description": "Phase A instantaneous current"
    }
  }
}
```

### Campos

- `device_id`: Identificador del dispositivo (configurable)
- `ts`: Timestamp en milisegundos (Unix epoch)
- `seq`: Número de secuencia incremental
- `measurements`: Objeto con todas las mediciones configuradas
  - `obis`: Código OBIS de la medición
  - `value`: Valor procesado con scaler aplicado
  - `unit_code`: Código de unidad según DLMS
  - `raw_value`: Valor crudo sin procesar
  - `description`: Descripción de la medición

## 🔍 Mediciones Disponibles

| Clave | OBIS | Descripción | Unidad |
|-------|------|-------------|--------|
| `voltage_l1` | 1-1:32.7.0 | Voltaje instantáneo fase A | V |
| `current_l1` | 1-1:31.7.0 | Corriente instantánea fase A | A |

Para agregar más mediciones, edita el diccionario `MEASUREMENTS` en `../dlms_reader.py`.

## 🛠️ Arquitectura

```
dlms-bridge/
├── app/
│   ├── __init__.py          # Módulo de aplicación
│   ├── config.py            # Configuración con pydantic
│   ├── dlms_reader.py       # Adaptador async para DLMSClient
│   ├── mqtt_transport.py    # Cliente MQTT
│   ├── controller.py        # Controlador principal
│   └── main.py              # Punto de entrada
├── requirements.txt         # Dependencias
├── .env.example             # Plantilla de configuración
└── README.md                # Este archivo
```

### Componentes

1. **config.py**: Gestión de configuración usando Pydantic
2. **dlms_reader.py**: Wrapper async del `DLMSClient` existente
3. **mqtt_transport.py**: Cliente MQTT con context manager
4. **controller.py**: Loop principal con manejo de errores
5. **main.py**: Entry point con graceful shutdown

## 🐛 Troubleshooting

### Error: "Connection timeout"

**Causa:** El medidor puede estar procesando una conexión previa.

**Solución:** 
- Esperar 10-15 segundos entre intentos
- Verificar conectividad: `ping 192.168.5.177`
- Verificar puerto: `nc -vz 192.168.5.177 3333`

### Error: "Association rejected"

**Causa:** Parámetros de autenticación incorrectos.

**Solución:**
- Verificar `DLMS_SERVER_LOGICAL=0` (crítico para Microstar)
- Verificar password correcto para el SAP
- Revisar `DLMS_CLIENT_SAP` y `DLMS_SERVER_PHYSICAL`

### Error: "MQTT connection failed"

**Causa:** Broker MQTT no disponible.

**Solución:**
- Verificar que el broker esté ejecutándose
- Para pruebas locales, instalar Mosquitto:
  ```bash
  # Ubuntu/Debian
  sudo apt install mosquitto mosquitto-clients
  sudo systemctl start mosquitto
  
  # MacOS
  brew install mosquitto
  brew services start mosquitto
  ```

### El medidor reporta valores incorrectos

**Causa:** El medidor Microstar puede reportar códigos de unidad incorrectos (ej: 35/Hz en lugar de 32/V para voltaje).

**Solución:** Esto es normal y se corrige automáticamente en el código. Los valores finales son correctos.

## 🧪 Pruebas

### Probar lectura DLMS directamente

```bash
cd ..
python3 dlms_reader.py \
  --host 192.168.5.177 \
  --port 3333 \
  --client-sap 1 \
  --server-logical 0 \
  --server-physical 1 \
  --password 22222222 \
  --measurement voltage_l1 current_l1 \
  --verbose
```

### Escuchar publicaciones MQTT

```bash
# Instalar cliente mosquitto
mosquitto_sub -h localhost -t "meters/#" -v
```

## 📝 Notas Importantes

1. **`DLMS_SERVER_LOGICAL=0` es crítico** - Los medidores Microstar NO funcionan con el valor por defecto de 1

2. **Delay entre conexiones** - Esperar entre conexiones evita problemas de timeout

3. **Códigos OBIS** - Los códigos OBIS siguen el estándar IEC 62056-21

4. **Scaler automático** - Los valores incluyen el scaler automáticamente aplicado

## 🔗 Referencias

- [IEC 62056 DLMS/COSEM](https://www.dlms.com/)
- [OBIS Codes](https://www.dlms.com/dlms-cosem/obis-codes)
- [MQTT Protocol](https://mqtt.org/)

## 📄 Licencia

Ver archivo LICENSE del proyecto principal.

## 👥 Autor

Proyecto de tesis - Sebastián Giraldo

---

**Estado:** ✅ Funcional - Probado con medidor Microstar Electric Company Limited
