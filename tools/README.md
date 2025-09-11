# Tools

- Captura:
  - `tshark_capture.sh`: captura pcap filtrado por interfaz y host.
- Procesamiento:
  - `pcap_metrics.ipynb`: notebook para extraer métricas y gráficos.
- Utilidades ESP-IDF:
  - `flash.sh`, `monitor.sh`.

- Wi‑Fi AP (NetworkManager): `wifi_ap_nmcli.sh`
  - Crea y gestiona un punto de acceso Wi‑Fi en Raspberry Pi 5 (o cualquier Linux con NetworkManager).
  - Requisitos: NetworkManager activo y tarjeta Wi‑Fi con soporte de modo AP.
  - Uso:
    - Levantar AP: `sudo ./wifi_ap_nmcli.sh up <SSID> <PASS> [BAND] [CHANNEL]`
      - BAND: 2 (2.4GHz), 5 (5GHz), 6 (Wi‑Fi 6), opcional
      - CHANNEL: canal válido para la banda (p. ej., 1/6/11 en 2.4; 36/40/44/48 en 5GHz), opcional
    - Bajar AP: `sudo ./wifi_ap_nmcli.sh down <SSID>`
    - Estado: `sudo ./wifi_ap_nmcli.sh status [SSID]`
  - Ejemplo:
    - `sudo ./wifi_ap_nmcli.sh up TesisAP MyPass123 5 36`
  - Notas:
    - Usa IPv4 shared (NAT + DHCP mediante NetworkManager) y desactiva IPv6 en el perfil del AP.
    - Si NetworkManager no está activo: `sudo systemctl enable --now NetworkManager`.
    - En Raspberry Pi OS, asegúrate de que el driver soporta modo AP en wlan0 y que no hay conflicto con otros gestores (wpa_supplicant/dhcpcd).
