# lwm2m_temp_c6 (ESP32-C6 + WiFi + LwM2M)

Cliente LwM2M (Anjay) que se conecta por Wi‑Fi y publica temperatura (Objeto 3303) a un servidor LwM2M.

## Configuración rápida
- Wi‑Fi SSID/Password via menuconfig.
- Endpoint Name, URI servidor (coap/coaps), modo bootstrap opcional, y PSK/Identity (si COAPS/DTLS) via menuconfig.

## Build/Flash/Monitor (PowerShell)
```
& "C:\Espressif\idf-export.ps1"
idf.py set-target esp32c6
idf.py menuconfig
idf.py build
idf.py -p COM5 flash
idf.py -p COM5 monitor
```

## Dev Container (Docker + VS Code)

Para compilar y flashear desde un contenedor reproducible:

1. Instala Docker Desktop y la extensión "Dev Containers" de VS Code.
2. Abre la carpeta del proyecto y selecciona "Reopen in Container".

El contenedor usa `espressif/idf:release-v5.2` e incluye las extensiones recomendadas.

### Tareas disponibles (dentro del contenedor)

- IDF: Set Target (esp32c6)
- IDF: Menuconfig
- IDF: Build
- IDF: Flash (Linux /dev/ttyUSB0)
- IDF: Flash (macOS via RFC2217)
- IDF: Monitor (Linux /dev/ttyUSB0)
- IDF: Monitor (macOS via RFC2217)

### Puertos serial (COM/tty) en Linux y macOS

- Linux: el contenedor monta `/dev`, por lo que normalmente basta con usar `/dev/ttyUSB0` o `/dev/ttyACM0` en las tareas anteriores.
- macOS: Docker no expone `/dev/cu.*` directamente al contenedor de forma estable. Usa un proxy RFC2217 en el host:
	1. Instala pyserial: `pip3 install --user pyserial`
	2. Identifica el puerto: `ls /dev/cu.usb*` (p.ej. `/dev/cu.SLAB_USBtoUART`)
	3. Ejecuta en el host: `python3 tools/rfc2217-proxy-macos.py --device /dev/cu.SLAB_USBtoUART --port 5333`
	4. Dentro del contenedor, usa el puerto `rfc2217://host.docker.internal:5333` con las tareas de Flash/Monitor.
