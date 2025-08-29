# Proyectos ESP-IDF

Estructura sugerida:
- `common/` componentes compartidos (wifi, net, métricas)
- `mqtt_node/`
- `coap_node/`
- `http_node/`
- `lwm2m_node/`

Cada proyecto expone:
- `main/` app principal
- `CMakeLists.txt`
- `sdkconfig.defaults`

## Métricas comunes
- Timestamps envío/recepción
- Bytes enviados/recibidos
- Retransmisiones
- Energía estimada (tiempo activo vs sleep)

## Scripts útiles
- `tools/flash.sh`
- `tools/monitor.sh`

---

## Guías por proyecto

### 1) common (componentes compartidos)
Ubicación: `projects/common/components/common`

Archivos y función:
- `include/net_common.h` / `net_common.c`
  - Inicializa `esp_netif` y loop de eventos.
  - Crea interfaces por defecto: WiFi STA, Ethernet (RMII) y placeholder Thread.
  - `net_common_wait_ip(ifx, timeout_ms)`: espera hasta obtener IP.
  - `net_common_get_netif(ifx)`: retorna `esp_netif_t*` de la interfaz.
- `include/wifi.h` / `wifi.c`
  - WiFi STA: `wifi_init_sta(ssid, pass)`, `wifi_start()`, `wifi_is_connected()`.
  - Maneja eventos `WIFI_EVENT` / `IP_EVENT`.
- `include/ethernet.h` / `ethernet.c`
  - Ethernet: `eth_init_default()`, `eth_start()`, `eth_is_connected()`.
  - Nota: pines/PHY por placa; ajustar si no usas IP101 por defecto.
- `CMakeLists.txt`
  - Declara dependencias: `esp_wifi`, `esp_eth`, `nvs_flash`, `esp_event`, `esp_netif`, `mqtt`.

Build como dependencia:
- Los proyectos finales agregan `set(EXTRA_COMPONENT_DIRS "../common/components")` en su `CMakeLists.txt`.

### 2) mqtt_node (publicación MQTT)
Ubicación: `projects/mqtt_node`

Objetivo:
- Conectar a un broker MQTT y publicar telemetría periódica.
- Soporta múltiples redes: WiFi, Ethernet y (placeholder) Thread.

Archivos clave y función:
- `main/main.c`
  - Config via `Kconfig` (`CONFIG_MQTT_*`).
  - Inicializa redes según flags, espera IP y arranca cliente MQTT.
  - Tópicos de ejemplo: `"/iot/test/telemetry"` (publish), `"/iot/test/cmd"` (subscribe).
- `CMakeLists.txt`
  - Define proyecto y ruta a `common` como componente extra.
- `Kconfig.projbuild`
  - Opciones: `MQTT_USE_WIFI`, `MQTT_USE_ETH`, `MQTT_USE_THREAD`, `MQTT_WIFI_SSID`, `MQTT_WIFI_PASS`, `MQTT_BROKER_URI`.
- `sdkconfig.defaults`
  - Valores por defecto para las opciones anteriores.
- `tools/flash.sh`
  - Atajo para flashear/monitor.

Configuración:
- `idf.py menuconfig` → "MQTT Node Options".
- Broker de prueba: `mqtt://test.mosquitto.org:1883` o `mqtt://host.docker.internal:1883` cuando se compila dentro de Docker y el broker corre en el host.

Build (nativo):
- `cd projects/mqtt_node`
- `idf.py set-target esp32` (o `esp32c3/esp32h2` según hardware)
- `idf.py build`

Build (Docker):
- Construye imagen: ver `docker/README.md` (`esp-idf-iot:5.2`).
- Ejecuta contenedor montando el proyecto y common, luego dentro: `idf.py set-target esp32 && idf.py build`.

Ejecución:
- Flasheo/monitor: `./tools/flash.sh /dev/tty.usbserial-XXXX`
- Validación: `mosquitto_sub -h <broker> -t /iot/test/telemetry -v`

Notas:
- Ethernet: puede requerir ajuste de pines/PHY en `ethernet.c` según tu placa.
- Thread: placeholder (requiere ESP32-H2/H4 + OpenThread).

### 3) coap_node (pendiente)
- Cliente CoAP (confirmable/no confirmable, block-wise, observe) usando port de `libcoap`/ejemplos ESP-IDF.
- Estructura prevista: `main/`, `Kconfig.projbuild`, `sdkconfig.defaults` (URI/puerto, modo CON/NON, observe, block size).
- Métricas: RTT CON/ACK, pérdidas/retransmisiones, overhead UDP.

### 4) http_node (pendiente)
- Cliente `esp_http_client` para GET/POST, con/ sin TLS.
- Config: URL, keepalive, tamaño de payload.
- Métricas: latencia por transacción, bytes por transacción.

### 5) lwm2m_node (pendiente)
- Cliente LwM2M (Anjay/Wakaama) con registro, observe/notify.
- Config: URI servidor, objetos habilitados, periodo de update.
- Métricas: latencia de Register/Update, overhead, fiabilidad de Notify.

---

Consejos de desarrollo:
- VS Code: usa la extensión de ESP-IDF o compila una vez para generar `compile_commands.json` y resolver includes.
- Capturas: `tools/tshark_capture.sh` en raíz del repo para generar `.pcapng` en `data/pcaps/`.
