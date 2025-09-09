# OpenWISP (Docker Compose)

This stack runs OpenWISP locally without conflicting with the other stacks in this repo (ThingsBoard, Leshan).

- Dashboard: http://localhost:8091
- API: http://localhost:8092
- HTTPS: https://localhost:8445
- Postgres (host): 5433
- InfluxDB (host): 8086
- Redis (host): 6379

Ports intentionally avoid collisions:
- ThingsBoard uses 8080, 7070, 1883, 8883, 5683-5688/udp
- Leshan uses 8081, 5683/udp, 5684/udp
- OpenWISP uses 8091, 8092, 8445 and Postgres on 5433

## Usage

Make sure you have Docker and Docker Compose installed.

- Start: `./up.sh`
- Status: `./status.sh`
- Logs: `./logs.sh`
- Stop: `./down.sh`

First boot will run migrations and may take a few minutes. When ready, open the Dashboard URL above.

## Configuration

Defaults are in `.env`. You can customize passwords, domains and SSL mode. For production, change `DJANGO_SECRET_KEY` and use proper DNS and TLS.

## Notes

- If you also run ThingsBoard, avoid UDP 5683-5688 conflicts; this stack does not expose CoAP ports.
- If port 6379 is used by another local redis, change it in `.env` and in `docker-compose.yml`.