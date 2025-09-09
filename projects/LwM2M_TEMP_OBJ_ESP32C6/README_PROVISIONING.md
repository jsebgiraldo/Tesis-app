# LwM2M Temperature Object for ESP32-C6 with WiFi Provisioning

This project implements a LwM2M temperature sensor object on ESP32-C6 with integrated WiFi provisioning capabilities using BLE or SoftAP transport.

## Features

- **WiFi Provisioning**: Provision WiFi credentials via BLE or SoftAP
- **LwM2M Integration**: Connect to LwM2M servers using Anjay library
- **Temperature Sensor**: Simulated temperature object implementation
- **Security**: Support for multiple security levels (0, 1, 2)
- **QR Code**: Generate QR codes for easy mobile app provisioning

## WiFi Provisioning

The device supports two provisioning methods:

### BLE Provisioning (Default)
- Uses Bluetooth Low Energy for provisioning
- Generates QR code for mobile app scanning
- Service name format: `PROV_XXXXXX` (last 3 bytes of MAC)
- Default security: Version 1 with PoP `"abcd1234"`

### SoftAP Provisioning
- Creates a WiFi Access Point for provisioning
- Connect to the AP and visit `http://192.168.4.1`
- Default security: Version 1 with PoP `"abcd1234"`

## Build and Flash

1. **Configure the project:**
   ```bash
   idf.py menuconfig
   ```
   - Go to "WiFi Provisioning Configuration" to change settings
   - Select transport method (BLE/SoftAP)
   - Configure security version and credentials

2. **Build:**
   ```bash
   idf.py build
   ```

3. **Flash:**
   ```bash
   idf.py -p PORT flash
   ```

4. **Monitor:**
   ```bash
   idf.py -p PORT monitor
   ```

## Provisioning Process

### Using Mobile Apps

**Android:**
- Install ESP BLE Provisioning app from Play Store
- Scan QR code displayed in terminal
- Follow app instructions to configure WiFi

**iOS:**
- Install ESP BLE Provisioning app from App Store
- Scan QR code displayed in terminal
- Follow app instructions to configure WiFi

### Using Command Line Tool

```bash
# Navigate to ESP-IDF tools directory
cd $IDF_PATH/tools/esp_prov

# Provision using BLE (replace PROV_XXXXXX with actual device name)
python esp_prov.py --transport ble --service_name PROV_XXXXXX --sec_ver 1 --pop abcd1234 --ssid "YourWiFiSSID" --passphrase "YourWiFiPassword"

# Provision using SoftAP
python esp_prov.py --transport softap --service_name 192.168.4.1:80 --sec_ver 1 --pop abcd1234 --ssid "YourWiFiSSID" --passphrase "YourWiFiPassword"
```

### Interactive Provisioning

For WiFi network scanning and selection:

```bash
# BLE provisioning with network scan
python esp_prov.py --transport ble --service_name PROV_XXXXXX --sec_ver 1 --pop abcd1234

# Follow prompts to scan and select WiFi network
```

## Configuration Options

### Security Versions

- **Security 0**: No encryption (not recommended)
- **Security 1**: X25519 + AES-CTR + PoP authentication
- **Security 2**: SRP6a + AES-GCM authentication

### Reprovisioning

Enable reprovisioning in menuconfig to allow updating WiFi credentials without factory reset:

```
WiFi Provisioning Configuration → Enable reprovisioning
```

### Reset Provisioning

To reset provisioning data and restart provisioning:

```bash
# Erase NVS partition
idf.py -p PORT erase_flash
idf.py -p PORT flash
```

## LwM2M Configuration

Configure LwM2M settings in menuconfig:

```
LwM2M APP →
├── LwM2M Endpoint Name: "esp32c6-temp"
├── LwM2M Server URI: "coap://192.168.137.1:5683"
├── Use LwM2M Bootstrap: [N]
├── DTLS PSK Identity: ""
├── DTLS PSK Key: ""
└── Various timing and buffer configurations
```

## Project Structure

```
main/
├── main.c                 # Main application entry point
├── wifi_provisioning.c    # WiFi provisioning implementation
├── wifi_provisioning.h    # WiFi provisioning header
├── lwm2m_client.c         # LwM2M client implementation
├── temp_object.c          # Temperature object implementation
├── temp_object.h          # Temperature object header
├── qrcode.c               # QR code generation
├── qrcode.h               # QR code header
├── CMakeLists.txt         # Component configuration
└── Kconfig.projbuild      # Project configuration options
```

## Troubleshooting

### Provisioning Failed
- Check WiFi credentials are correct
- Verify security settings match
- Ensure device is in range of WiFi network

### BLE Not Working
- Confirm ESP32-C6 Bluetooth is enabled
- Check mobile device Bluetooth permissions
- Verify BLE transport is selected in menuconfig

### Connection Issues
- Monitor serial output for error messages
- Check network firewall settings
- Verify LwM2M server is accessible

### Reset to Factory
To completely reset the device:
```bash
idf.py -p PORT erase_flash
idf.py -p PORT flash
```

## Development Notes

- The temperature object generates simulated readings
- WiFi credentials are stored in NVS and persist across reboots
- Device will auto-connect to WiFi on subsequent boots
- Provisioning is only required on first boot or after factory reset

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
