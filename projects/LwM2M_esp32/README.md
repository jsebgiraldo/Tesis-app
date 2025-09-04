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
