# MQTT Node (ESP-IDF)

Ejemplo que publica telemetría periódica a un broker MQTT, soportando múltiples redes: WiFi, Ethernet y (placeholder) Thread.

## Configuración
- Edita `sdkconfig.defaults` o usa `idf.py menuconfig`:
  - Network:
    - `CONFIG_MQTT_USE_WIFI` / `CONFIG_MQTT_USE_ETH` / `CONFIG_MQTT_USE_THREAD`
    - `CONFIG_MQTT_WIFI_SSID` / `CONFIG_MQTT_WIFI_PASS`
  - MQTT:
    - `CONFIG_MQTT_BROKER_URI` (ej. `mqtt://host.docker.internal:1883` si corres broker en el host)

## Build local
```bash
cd projects/mqtt_node
idf.py set-target esp32 # o esp32c3/esp32h2
idf.py build
```

## Build con Docker
```bash
docker build -t esp-idf-iot:latest docker/
# O usa el helper que monta también common:
./docker/docker-run-mqtt.sh
# dentro del contenedor
idf.py set-target esp32 && idf.py build
```

Nota: Si ves el error de CMake sobre EXTRA_COMPONENT_DIRS apuntando a "/common/components",
asegúrate de montar `projects/common` dentro del contenedor (usa el script anterior) o
compila fuera de Docker donde existe `../common/components`.

## Flash/Monitor
```bash
./tools/flash.sh /dev/tty.usbserial-XXXX
```

## Prueba rápida del broker
```bash
mosquitto_sub -h test.mosquitto.org -t /iot/test/telemetry -v
```

## Notas Thread
- Requiere SoC compatible (ESP32-H2/H4) y componentes OpenThread. Este ejemplo deja el hook listo en `net_common`.
