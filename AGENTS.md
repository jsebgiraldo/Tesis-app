# Repository Guidelines

## Project Structure & Module Organization
- Root folders: `projects/` (ESP-IDF apps), `docs/`, `protocols/`, `docker/`, `tools/`, `backups/`.
- ESP-IDF apps live under `projects/<app>/` with:
  - `main/`, `CMakeLists.txt`, `Kconfig.projbuild`, `sdkconfig.defaults`.
  - Shared code in `projects/common/components/common` (e.g., `wifi.c`, `net_common.c`).
- Example paths: `projects/mqtt_node/main/main.c`, `projects/mqtt_node/tools/flash.sh`.

## Build, Test, and Development Commands
- Local build (choose target):
  - `cd projects/mqtt_node`
  - `idf.py set-target esp32` (or `esp32c3`, `esp32c6`, `esp32h2`)
  - `idf.py build`
- Flash + monitor: `idf.py -p <PORT> flash monitor` or `./tools/flash.sh /dev/tty.*`.
- Docker toolchain:
  - `docker build -t esp-idf-iot:latest docker/`
  - `./docker/docker-run-mqtt.sh` then run `idf.py set-target esp32 && idf.py build` inside.
- Quick MQTT check: `mosquitto_sub -h <broker> -t /iot/test/telemetry -v`.

## Coding Style & Naming Conventions
- Language: C (ESP-IDF, C99). Indentation: 4 spaces, no tabs. Max line ~100 cols.
- Files and symbols: `snake_case.c/h`; macros `UPPER_CASE`, types `snake_case_t`.
- Kconfig options upper-case with a clear prefix (e.g., `CONFIG_MQTT_*`).
- Prefer small, focused modules under `components/`; keep `main/` orchestration-only.

## Testing Guidelines
- No formal unit-test suite yet. Validate with hardware-in-the-loop:
  - Build succeeds; device boots; logs clean in `idf.py monitor`.
  - For MQTT, observe telemetry via `mosquitto_sub`.
- Capture traces when relevant: `sudo ./tools/tshark_capture.sh -i <iface> -f "host <ip>" -o data/pcaps/<name>`.

## Commit & Pull Request Guidelines
- Commits: imperative mood, concise subject; optional body with rationale.
  - Examples: "Add WiFi provisioning for ESP32C6", "Refactor LwM2M retry logic".
- PRs must include:
  - Clear description, linked issues, affected app(s) (e.g., `projects/mqtt_node`).
  - Logs or screenshots; for protocol work, attach `.pcapng` from `data/pcaps/` when useful.

## Security & Configuration
- Do not commit secrets/tokens. Configure via `menuconfig` or `sdkconfig.defaults`.
- `build/`, `sdkconfig*`, and `data/pcaps/*.pcapng` are ignored; keep it that way.

## Agent-Specific Notes
- Prefer minimal, targeted changes; match existing CMake and file layout.
- When adding a new app, mirror the `projects/<app>/` structure and reuse `projects/common/components` when possible.

