# Hello ESP32 (ESP-IDF)

Ejemplo mínimo para iniciar con nodos ESP32 dentro del workspace. Parpadea un LED y escribe logs por serial.

## Requisitos
- ESP-IDF 5.x instalado y exportado (en Windows usa el ESP-IDF PowerShell/Developer Command Prompt)
- Placa ESP32 con un LED en GPIO2 (puedes cambiar el pin en `menuconfig` -> "Hello ESP32 app configuration")

## Compilar
1. Abre la terminal de ESP-IDF en Windows (IDF PowerShell).
2. Ubícate en esta carpeta:
   ```powershell
   cd "c:\Users\Luis Antonio\Documents\tesis-trabajo\Tesis-app\projects\hello_esp32"
   ```
3. Configura (opcional, para elegir el puerto/pin):
   ```powershell
   idf.py menuconfig
   ```
4. Compila:
   ```powershell
   idf.py build
   ```

## Flashear y monitor
1. Conecta tu ESP32 y verifica el puerto COM (por ejemplo, `COM5`).
2. Flashea y abre monitor en una línea:
   ```powershell
   idf.py -p COM5 flash monitor
   ```

Deberías ver mensajes `Hello from ESP32 (LED ON/OFF)` y el LED parpadeando.

## Estructura
- `CMakeLists.txt`: proyecto IDF
- `main/`:
  - `main.c`: app con blink + logs
  - `CMakeLists.txt`: componente principal
  - `Kconfig.projbuild`: opciones de app (GPIO)
- `sdkconfig.defaults`: valores por defecto de configuración

## Notas
- Si tienes componentes comunes en `projects/common/components`, se montarán automáticamente sin ser obligatorios para este ejemplo.
- Para placas con LED en otro pin (e.g., 5, 25, 27), cámbialo en `menuconfig` o ajusta `CONFIG_HELLO_BLINK_GPIO` en `sdkconfig.defaults`.
