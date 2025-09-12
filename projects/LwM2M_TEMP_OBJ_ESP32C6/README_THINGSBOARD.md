# Conexión ESP32-C6 (Anjay) a ThingsBoard (Edge/Server) vía LwM2M

Este proyecto usa Anjay (cliente LwM2M) para publicar el Objeto Temperatura (3303) hacia ThingsBoard (Edge o Server) usando CoAP (sin DTLS por defecto).

## Requisitos
- ThingsBoard Edge/Server corriendo con transporte LwM2M/CoAP habilitado.
  - En nuestro stack, Edge expone UDP 5683 dentro del contenedor y está mapeado al host en 15683 (compose: `15683-15688:5683-5688/udp`).
- ESP-IDF 5.2+ y toolchain para ESP32-C6.
- Wi‑Fi AP accesible por el ESP32-C6 (por defecto `PI5_AP` / `banano2025`).

## Parámetros del proyecto
Valores por defecto (puedes cambiarlos en `idf.py menuconfig` o `sdkconfig.defaults`):
- SSID/Pass Wi‑Fi: `PI5_AP` / `banano2025`.
- LwM2M Endpoint Name: `esp32c6-temp`.
- LwM2M Server URI:
  - `coap://10.42.0.1:15683` (host del AP en Raspberry Pi; NetworkManager "shared" suele asignar 10.42.0.1).
  - Si usas otra red, coloca la IP del host donde corre Edge y el puerto mapeado (15683 por nuestro compose).
- Bootstrap: deshabilitado por defecto.
- Seguridad: NOSEC (coap). Para DTLS (coaps), configura `LWM2M_SECURITY_PSK_ID` y `LWM2M_SECURITY_PSK_KEY` (hex) y usa `coaps://host:5684`.

## Pasos para conectar
1) Asegura que ThingsBoard Edge está arriba y escucha LwM2M en el host:
   - Edge UI: http://localhost:18080
   - Server UI: http://localhost:8081
   - LwM2M (CoAP) mapeado: UDP 15683 → 5683.

2) En ThingsBoard Server (UI puerto 8081):
   - Inicia sesión como tenant (`tenant@thingsboard.org` / `tenant` si carga demo).
   - Ve a Devices y crea un dispositivo nuevo de tipo LwM2M (p.ej. nombre `esp32c6-temp`).
   - En la pestaña LwM2M, verifica que el transporte está habilitado y que el servidor espera registros NOSEC (o configura DTLS si lo necesitas).

3) Configura el proyecto ESP32-C6:
   - `idf.py set-target esp32c6`
   - `idf.py menuconfig`
     - WiFi: SSID/Password (o usa BLE provisioning si ya lo tienes).
     - LwM2M: Endpoint Name, Server URI (`coap://<HOST_IP>:15683`).
     - (Opcional) Habilita DTLS/PSK y coloca PSK Identity/Key.

4) Compila y flashea:
   - `idf.py build`
   - `idf.py -p /dev/ttyUSB0 flash monitor`

5) Verifica en la UI de ThingsBoard:
   - El dispositivo debe aparecer como "active" tras el Register.
   - Observa recursos 3303/0/5700, 5701, 5601, 5602. Recibirás Notify (cada ~10s) y Send (cada ~5s) desde el cliente.

## Notas
- Si utilizas el AP creado por `tools/wifi_ap_nmcli.sh`, la IP del host suele ser 10.42.0.1.
- Si ejecutas Edge en otra máquina, asegúrate de que el puerto UDP 15683 está accesible desde el ESP32-C6.
- Para performance, ajusta `CONFIG_LWM2M_IN_BUFFER_SIZE/OUT_BUFFER_SIZE` según carga.
- Para DTLS (coaps) con PSK, ThingsBoard requiere configurar el dispositivo con identidad/clave; usa la misma identidad/clave en el cliente.
