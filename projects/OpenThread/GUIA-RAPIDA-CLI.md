# 🚀 Guía Rápida - OpenThread CLI en ESP32C6

## ✅ Estado Actual
- **Hardware**: ESP32C6 DevKit (98:88:e0:7b:c5:ac)
- **Puerto**: COM12
- **ESP-IDF**: v5.3.1 LTS
- **OpenThread**: openthread-esp32/c8fc5f643b
- **Características**: Thread FTD (sin Commissioner/Joiner)

---

## 📋 Comandos Básicos del CLI

### Información del Dispositivo
```bash
# Ver versión de OpenThread
version

# Ver EUI-64 (dirección única del dispositivo)
eui64

# Ver Extended PAN ID
extpanid

# Ver información del hardware
factoryreset  # ⚠️ Resetea a valores de fábrica

# Ver dirección IPv6 del nodo
ipaddr

# Ver información de la interfaz radio
ifconfig

# Ver estado actual del nodo
state  # Valores: disabled, detached, child, router, leader
```

### Configuración de Red Thread

#### Opción 1: Crear Red Nueva (Modo Leader)
```bash
# 1. Configurar dataset inicial
dataset init new

# 2. Ver dataset generado
dataset

# 3. Confirmar y activar dataset
dataset commit active

# 4. Configurar canal (11-26, recomendado 15)
channel 15

# 5. Configurar PAN ID
panid 0xabcd

# 6. Configurar nombre de red
networkname MiRedThread

# 7. Activar interfaz Thread
ifconfig up

# 8. Iniciar Thread
thread start

# 9. Verificar estado (debe ser "leader" después de unos segundos)
state
```

#### Opción 2: Unirse a Red Existente (Modo Child/Router)
```bash
# 1. Configurar dataset de red existente
dataset set active <dataset_hex>

# 2. O configurar manualmente:
channel 15
panid 0xabcd
networkkey 00112233445566778899aabbccddeeff
extpanid 1111111122222222

# 3. Activar y conectar
ifconfig up
thread start

# 4. Verificar conexión
state  # Debe mostrar "child" o "router"
```

---

## 🔧 Comandos de Diagnóstico

### Red y Conectividad
```bash
# Ver tabla de vecinos
neighbor table

# Ver topología de routers
router table

# Ver información del líder
leaderdata

# Ver información de la red
networkdata show

# Estadísticas de paquetes
counters

# Ping a otro nodo (usar IPv6 del destino)
ping <ipv6-address>

# Ver RLOC (Routing Locator)
rloc16
```

### Radio y Enlace
```bash
# Ver canal actual
channel

# Ver potencia de transmisión (dBm)
txpower

# Escanear canales disponibles
scan

# Ver calidad de enlace
linkquality

# RSSI del enlace con el padre
parent
```

### Configuración Avanzada
```bash
# Ver todas las configuraciones
config

# Modo de dispositivo (FTD siempre activo en este build)
mode  # Muestra: rdn (router, rx-on-when-idle, full network data)

# Habilitar/deshabilitar router
routerrole

# Ver dataset completo
dataset active -x
```

---

## 📡 Ejemplo Completo: Crear Red Thread

### En el ESP32C6 #1 (Leader):
```bash
> dataset init new
Done
> dataset panid 0x1234
Done
> dataset channel 15
Done
> dataset networkname RedESP32C6
Done
> dataset commit active
Done
> ifconfig up
Done
> thread start
Done
> state
leader
> ipaddr
fdde:ad00:beef:0:0:ff:fe00:fc00
fdde:ad00:beef:0:0:ff:fe00:d800
fe80:0:0:0:9a88:e0ff:fe7b:c5ac
Done
```

### Para Unir Otro Dispositivo:
```bash
# 1. En el Leader, obtener dataset:
> dataset active -x
0e080000000000010000000300001035060004001fffe00208111111112222222207083333333333333333051000112233445566778899aabbccddeeff03
0f4f70656e5468726561642d3123340102123404100102030405060708090a0b0c0d0e0f00030a4f70656e546872656164000410deadbeefdeadbeef
deadbeefdeadbeef
Done

# 2. En el nuevo dispositivo (ESP32C6 #2):
> dataset set active 0e080000000000010000000300001035060004001fffe00208111111112222222207083333333333333333051000112233445566778899aabbccddeeff030f4f70656e5468726561642d3123340102123404100102030405060708090a0b0c0d0e0f00030a4f70656e546872656164000410deadbeefdeadbeefdeadbeefdeadbeef
Done
> ifconfig up
Done
> thread start
Done
> state
child
```

---

## 🛠️ Comandos de Desarrollo y Debug

```bash
# Logs detallados (nivel 1-5)
loglevel 5  # 5 = más detallado

# Reinicio suave
reset

# Ver memoria
diag stats

# Habilitar modo promiscuo (sniffer)
promiscuous enable

# Ver todos los comandos disponibles
help

# Salir del monitor (en idf.py monitor)
# Presionar: Ctrl + ]
```

---

## 🎯 Tareas VSCode Configuradas

### Build
```
Ctrl+Shift+B → "Build - ESP32C6 OpenThread"
```

### Flash
```
Ctrl+Shift+P → Tasks: Run Task → "Flash - ESP32C6 OpenThread"
Puerto: COM12 (por defecto)
```

### Monitor
```
Ctrl+Shift+P → Tasks: Run Task → "Monitor - ESP32C6 OpenThread"
```

### Flash & Monitor (Recomendado)
```
Ctrl+Shift+P → Tasks: Run Task → "Flash & Monitor - ESP32C6 OpenThread"
Flashea y abre monitor automáticamente
```

---

## ⚠️ Limitaciones Actuales

### ❌ No Disponible (deshabilitado para evitar errores mbedtls):
- **Commissioner**: No se puede comisionar nuevos dispositivos automáticamente
- **Joiner**: No se puede unir a redes con comisionamiento automático
- **DTLS**: No disponible para comunicación segura automática

### ✅ Disponible:
- **Thread FTD**: Funciona como Router/Leader/Child
- **CLI completo**: Todos los comandos de configuración manual
- **IEEE 802.15.4**: Radio Thread nativo del ESP32C6
- **IPv6 Thread**: Stack completo de red Thread
- **Dataset manual**: Configuración manual de redes

### 🔄 Workaround para Commissioning:
En lugar de commissioning automático, usa:
1. **Dataset manual**: Configura `networkkey`, `panid`, `extpanid`, `channel` manualmente
2. **Compartir dataset**: Usa `dataset active -x` para obtener el hex y compartirlo
3. **Aplicar dataset**: Usa `dataset set active <hex>` en nuevos dispositivos

---

## 📚 Recursos Adicionales

- [OpenThread CLI Reference](https://github.com/openthread/openthread/blob/main/src/cli/README.md)
- [ESP-IDF OpenThread Guide](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32c6/api-guides/openthread.html)
- [Thread Specification](https://www.threadgroup.org/support)

---

## 🐛 Troubleshooting

### Monitor no muestra nada después de flash:
```bash
# Presiona botón RESET en el ESP32C6
# O desconecta/reconecta USB
```

### "state" muestra "detached":
```bash
# Verifica que ejecutaste:
ifconfig up
thread start

# Espera 10-30 segundos para que forme red
```

### No puede unirse a red existente:
```bash
# Verifica que el dataset sea correcto:
dataset active -x

# Asegúrate que el canal coincida:
channel
```

### Errores de compilación mbedtls:
```bash
# Ya resuelto en este build:
# CONFIG_OPENTHREAD_COMMISSIONER=n
# CONFIG_OPENTHREAD_JOINER=n
```

---

**✅ Compilación exitosa**: 6 Nov 2025, ESP-IDF v5.3.1 LTS
**🎯 Siguiente paso**: Formar red Thread y probar comunicación entre nodos
