# ThingsBoard Edge (CE) en Docker: listo para Raspberry Pi, BeaglePlay y Linux/Windows

Este stack levanta ThingsBoard Edge CE 4.2.x con PostgreSQL usando Docker Compose, siguiendo la misma estructura de scripts del repo.

- Imagen: `thingsboard/tb-edge:4.2.0EDGE`
- Base de datos: PostgreSQL 16 (contenedor)
- Persistencia: volúmenes de Docker
- Multi-arquitectura: soporta AMD64 y ARM64 (ideal para Raspberry Pi 4/5 con OS de 64‑bit y BeaglePlay)

Nota: Este stack incluye un ThingsBoard Server CE local (monolítico) para trabajar OFFLINE. Edge se conecta por defecto a ese server interno (`CLOUD_RPC_HOST=tb-server`). También puedes apuntar a `demo.thingsboard.io` u otro server cambiando `.env`.

## Requisitos
- Docker Engine + Docker Compose v2 (`docker compose`)
- ~2 GB RAM libres (mínimo; más si conectas muchos dispositivos)
- Acceso a un servidor ThingsBoard CE/PE 4.2.x (o `demo.thingsboard.io`)
- Credenciales de Edge: `CLOUD_ROUTING_KEY` y `CLOUD_ROUTING_SECRET` obtenidas en el servidor (Edge Management > Instances > Install & Connection Instructions)

## Puertos
- Edge: 18080 -> 8080 (HTTP UI/API)
- Edge: 11883 -> 1883 (MQTT)
- Edge: 15683-15688 UDP -> 5683-5688 (CoAP/LwM2M)
- Server CE local: 8081 -> 8080 (UI del servidor)

Estos puertos evitan colisiones si también ejecutas el stack `docker/thingsboard` del repo.

## Uso rápido
```bash
cd docker/thingsboard-edge
cp -n .env.example .env   # editar luego CLOUD_*
# Edita .env y coloca:
# CLOUD_RPC_HOST=demo.thingsboard.io  # o IP/DNS de tu servidor TB
# CLOUD_ROUTING_KEY=...
# CLOUD_ROUTING_SECRET=...

./up.sh            # descarga y levanta Edge + Server local + Postgres
./status.sh        # espera a que HTTP esté disponible
./logs.sh          # ver logs de tb-edge
```

Abre:
- Edge UI:   http://localhost:18080 (usa credenciales del tenant del Server local)
- Server UI: http://localhost:8081  (inicia con usuarios por defecto luego de instalar DB)

Primera vez (Server local): inicializa el esquema del Server CE para que la UI funcione:

```bash
cd docker/thingsboard-edge
./install-server.sh           # esquema base del Server (opcionalmente carga demo con LOAD_DEMO=true)
```

## Scripts
- `up.sh`     – levanta el stack (pull + up -d).
- `down.sh`   – detiene contenedores (datos persisten).
- `reset.sh`  – detiene y borra volúmenes (limpia datos). Cuidado.
- `logs.sh`   – sigue logs del servicio tb-edge.
- `status.sh` – espera a que Edge responda en 18080.
- `install.sh`– atajo: valida `.env` y hace `pull` + `up` inaugural (Edge + Server).
- `install-server.sh` – inicializa/actualiza el esquema del Server CE local.
- `up.ps1`/`down.ps1` – wrappers simples para Windows PowerShell.

## Configuración
Variables principales (en `.env`):
- `CLOUD_RPC_HOST`    – Host/IP del servidor TB. Por defecto `tb-server` (servidor local del mismo compose). Si apuntas fuera, evita `localhost` dentro del contenedor; usa IP/DNS.
- `CLOUD_ROUTING_KEY` – Edge Key provisto por el servidor.
- `CLOUD_ROUTING_SECRET` – Edge Secret provisto por el servidor.

Variables de base de datos (puedes dejar valores por defecto):
- Edge: `POSTGRES_DB=tb-edge`, `POSTGRES_PASSWORD=postgres`.
- Server: `TB_SERVER_POSTGRES_PASSWORD=postgres`.

Colas/Queues: por defecto Edge usa cola en memoria (válida para desarrollo). Para producción considera Kafka (no incluido aquí por simplicidad; se puede extender al estilo de `docker/thingsboard`).

## Raspberry Pi / BeaglePlay
- Usa un sistema operativo de 64 bits (arm64/aarch64) para mejor compatibilidad.
- Este stack usa imágenes multi-arch; Docker selecciona la variante adecuada automáticamente.
- Si usas armv7 (32-bit) y alguna imagen no está disponible, migra a OS de 64 bits.

## Troubleshooting
- Edge no inicia sesión: verifica `CLOUD_*` y conectividad hacia `CLOUD_RPC_HOST` (puerto gRPC configurado por defecto en la imagen). Edge necesita que el servidor esté accesible al menos una vez para autenticar.
- Conflictos de puertos: cambia mapeos en `docker-compose.yml` (por ejemplo, `18080:8080`).
- Reinicializar todo: `./reset.sh` (borra volúmenes `tb-edge-*`).

## Extensiones posibles
- Añadir Kafka y transports dedicados (MQTT/CoAP) para producción.
- Soporte TLS para gRPC entre Edge y Server.
- Healthchecks avanzados y métricas.

Licencia: Apache 2.0 (ThingsBoard CE). Consulta documentación oficial: https://thingsboard.io/docs/edge/
