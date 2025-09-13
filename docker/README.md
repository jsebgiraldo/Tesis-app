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

## ThingsBoard CE local (broker/servidor IoT)
- Stack listo con Docker Compose en `docker/thingsboard`.
- Arranque: `cd docker/thingsboard && ./up.sh` y abre `http://localhost:8080`.
- Logs: `./logs.sh`. Detener: `./down.sh`.

## Notas
- Para Thread se compila con componentes OpenThread de ESP-IDF (ESP32-H2/H4). Requiere hardware compatible.

## Limpieza de todos los stacks Docker
Puedes detener y borrar los volúmenes de todos los stacks bajo `docker/*` con:

```bash
cd docker
bash clean-all.sh
```

Flags útiles:
- `--yes` omite confirmaciones
- `--prune-system` hace también `docker system prune -a --volumes` (cuidado, borra imágenes/redes no usadas)
