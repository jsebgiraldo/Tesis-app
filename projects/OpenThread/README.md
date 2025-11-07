# ESP32C6 OpenThread CLI Demo

Demo de OpenThread para ESP32C6 con interfaz CLI completa para configuración y control de nodos Thread.

## 📋 Requisitos Previos

- **ESP-IDF v5.1 o superior** instalado y configurado
- **ESP32C6** DevKit
- Cable USB-C para conexión
- Variables de entorno ESP-IDF configuradas

## 🚀 Inicio Rápido

### 1. Configurar el Target
```bash
idf.py set-target esp32c6
```

### 2. Compilar el Proyecto
Usa el task de VSCode: `Ctrl+Shift+P` → `Tasks: Run Task` → `Build - ESP32C6 OpenThread`

O desde terminal:
```bash
idf.py build
```

### 3. Flashear y Monitorear
Usa el task: `Flash & Monitor - ESP32C6 OpenThread`

O desde terminal:
```bash
idf.py -p COMX flash monitor
```
*(Reemplaza COMX con tu puerto COM, ej: COM3)*

## 📡 Comandos OpenThread CLI Básicos

Una vez que el dispositivo está corriendo y conectado al monitor serial, puedes usar estos comandos:

### Información del Dispositivo
```
> version
> eui64
> extaddr
> rloc16
```

### Configuración de Red
```
> dataset init new
> dataset commit active
> ifconfig up
> thread start
```

### Ver Estado
```
> state
> ipaddr
> neighbor table
> child table
> router table
```

### Configuración Manual de Red
```
> dataset networkname OpenThreadDemo
> dataset channel 15
> dataset panid 0x1234
> dataset commit active
```

### Escaneo de Redes
```
> scan
```

### Commissioner (Para agregar dispositivos)
```
> commissioner start
> commissioner joiner add * J01NME
> commissioner stop
```

### Joiner (Para unirse a una red)
```
> ifconfig up
> joiner start J01NME
```

## 🔧 Tasks Disponibles en VSCode

| Task | Descripción | Atajo |
|------|-------------|-------|
| **Build** | Compila el proyecto | `Ctrl+Shift+B` |
| **Flash** | Flashea al ESP32C6 | - |
| **Monitor** | Abre monitor serial | - |
| **Flash & Monitor** | Flashea y abre monitor | - |
| **Clean** | Limpia archivos de build | - |
| **Erase Flash** | Borra completamente la flash | - |
| **Menuconfig** | Abre configuración | - |
| **Set Target** | Establece ESP32C6 como target | - |

## 📁 Estructura del Proyecto

```
OpenThread/
├── main/
│   ├── CMakeLists.txt
│   └── ot_cli_main.c          # Código principal
├── .vscode/
│   ├── tasks.json             # Tasks configurados
│   └── launch.json            # Debug config
├── CMakeLists.txt             # Build principal
├── sdkconfig.defaults         # Configuración por defecto
└── README.md                  # Este archivo
```

## 🌐 Crear una Red Thread Básica

### En el Primer Nodo (Leader):
```
> dataset init new
> dataset networkname MiRedThread
> dataset channel 15
> dataset panid 0xABCD
> dataset commit active
> ifconfig up
> thread start
> state
```

Espera hasta que el estado sea `leader`, luego obtén las credenciales:
```
> dataset active -x
```

### En Nodos Adicionales (Router/Child):
```
> dataset set active <hex-string-del-leader>
> ifconfig up
> thread start
> state
```

## 🔍 Verificar Conectividad

### Desde el Leader:
```
> ipaddr
```
Anota la dirección IPv6 con prefijo `fdxx:`

### Desde otro nodo:
```
> ping <ipv6-del-leader>
```

## 📊 Monitoreo y Diagnóstico

```bash
# Ver logs del sistema
> log level 5

# Información de radio
> radio stats

# Información de buffers
> bufferinfo

# Estadísticas MAC
> mac stats
```

## 🛠️ Solución de Problemas

### El dispositivo no se detecta
- Verifica el cable USB-C
- Instala drivers USB-Serial (CP210x o CH340)
- Verifica en Administrador de Dispositivos

### Error al compilar
```bash
idf.py fullclean
idf.py set-target esp32c6
idf.py build
```

### No se puede formar red
- Verifica que el canal Thread (11-26) no esté congestionado
- Asegúrate de que `ifconfig up` retorna exitosamente
- Verifica logs con `log level 5`

## 📚 Recursos Adicionales

- [OpenThread Docs](https://openthread.io/)
- [ESP-IDF OpenThread Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-guides/openthread.html)
- [Thread Specification](https://www.threadgroup.org/support#specifications)
- [ESP32C6 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf)

## 📝 Notas Importantes

- El ESP32C6 tiene radio IEEE 802.15.4 integrado (6 GHz Thread)
- La configuración se guarda en NVS (Non-Volatile Storage)
- Para borrar configuración: `idf.py erase-flash`
- El baudrate por defecto es 115200

## 🎯 Próximos Pasos

1. Configurar múltiples nodos
2. Implementar CoAP server/client
3. Agregar MQTT-SN
4. Implementar Thread Border Router
5. Integración con Home Assistant / Matter

---

**¿Necesitas ayuda?** Revisa los logs con el monitor serial y consulta la documentación de OpenThread.
