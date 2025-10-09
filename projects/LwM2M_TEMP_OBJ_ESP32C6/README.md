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

## Descubrimiento Dinámico (DNS / Thread SRP)
Si habilitas `LwM2M Dynamic Discovery` en *menuconfig* el firmware intentará resolver el hostname configurado (por defecto `leshan.default.service.arpa`) antes de crear el objeto Security (0):

1. Intenta AAAA (IPv6) preferentemente (omite link-local fe80 si encuentra ULA/GUA).
2. Fallback a IPv4 si no hay IPv6 válido.
3. Construye URI `coap://[ipv6]:PUERTO` o `coap://ipv4:PUERTO`.
4. Si falla la resolución, cae al flujo clásico de hostname/port configurados en la sección de servidor.

Flags relevantes:
- `CONFIG_LWM2M_DNS_DISCOVERY_ENABLE`
- `CONFIG_LWM2M_DNS_DISCOVERY_HOST`
- `CONFIG_LWM2M_DNS_DISCOVERY_PORT`
- `CONFIG_LWM2M_DNS_DISCOVERY_SECURE` (para usar `coaps://`).

### Uso típico con Thread
1. Border Router publica el servicio/host (SRP) apuntando al servidor Leshan.
2. El nodo se une (Joiner) y obtiene conectividad IPv6.
3. En el arranque el cliente resuelve el hostname interno y registra con esa IP.

## OpenThread (Joiner Commissioning)
Habilita `LWM2M_NETWORK_USE_THREAD` y configura:
- `LWM2M_THREAD_JOINER_PSKD` (clave PSKd que coincide con el Border Router).

Flujo:
1. El dispositivo intenta unirse vía Joiner (30s timeout actual).
2. Si se adjunta: inicia directamente el cliente LwM2M.
3. Si no y Wi‑Fi está habilitado como fallback: continúa por Wi‑Fi.

Logs clave:
- `Thread network selected` → inicio del proceso.
- `Joiner started` → commissioning arrancó.
- `Thread attached (role=...)` → listo para LwM2M.
- `DNS discovery success:` → resolución dinámica exitosa.

Limitaciones actuales:
- Métricas de conectividad (Objeto 4) aún basadas en Wi‑Fi (pendiente extender a Thread RSSI/Link Quality).
- Sin reintentos escalonados para Joiner (solo primer intento + timeout).
