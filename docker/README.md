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

## Portainer (UI de gestión Docker)
- Administra visualmente contenedores, volúmenes e imágenes locales.
- Arranque: `cd docker/portainer && ./up.sh` y abre `http://localhost:9000` (primera vez crea usuario admin).
- Alternativa PowerShell Windows: `./up.ps1`.
- Detener: `./down.sh` / `./down.ps1`.
- HTTPS auto: `https://localhost:9443` (cert self-signed). Para producción agrega proxy/cert válido.

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
