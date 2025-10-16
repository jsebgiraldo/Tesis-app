# Eclipse Leshan LwM2M Server (local)

Este stack ejecuta el servidor demo de Eclipse Leshan en Docker.
Pensado para pruebas locales con el cliente LwM2M en ESP32.

Servicios:
- leshan (Web UI, CoAP/CoAPS)

Puertos (host -> contenedor):
- 8081 -> 8080 (Web UI)
- 5690/udp -> 5683/udp (CoAP)
- 5691/udp -> 5684/udp (CoAPS)

Nota: Usamos 8081 y 5690-5691 en host para no interferir con ThingsBoard (8080, 5683-5688/udp).

## Uso

```bash
cd docker/leshan
# Arrancar
docker compose up -d
# Ver logs
docker compose logs -f
# Detener
docker compose down
```

UI: http://localhost:8081

Para registrar un cliente ESP32, configura el endpoint y apunta a `coap://host.docker.internal:5690` cuando el ESP esté dentro de un contenedor, o `coap://<IP_HOST>:5690` si el ESP está en la red local.

## Troubleshooting
- Si el puerto 8081 está ocupado, cambia el mapeo en `docker-compose.yml`.
- Si ThingsBoard usa 5683-5688/udp, mantén Leshan en 5690/5691/udp.
- En macOS, `host.docker.internal` resuelve al host desde contenedores.
