# Docker para ESP-IDF

## Construir imagen
```bash
docker build -t esp-idf-iot:latest docker/
```

## Usar la imagen con proyecto
```bash
docker run --rm -it \
  -v "$PWD/projects/mqtt_node":/project \
  -v "$PWD/projects/common/components":/project/components \
  --name idf-mqtt esp-idf-iot:latest
```

- Para acceso a red puente (uso de broker externo):
  - Docker Desktop macOS usa NAT; el host es `host.docker.internal`.

## Notas
- Para Thread se compila con componentes OpenThread de ESP-IDF (ESP32-H2/H4). Requiere hardware compatible.
