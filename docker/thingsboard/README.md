# ThingsBoard CE (microservicios) local: Postgres + Kafka + tb-node

Este stack levanta una instalación de ThingsBoard CE 4.x basada en microservicios mínimos para desarrollo:
- PostgreSQL 16 (base de datos)
- Kafka 4.0 (KRaft, sin Zookeeper) como cola
- thingsboard-ce (imagen `thingsboard/tb-node:4.2.0`) con REST/API y motor de reglas

Nota: En despliegues microservicio, los transports (MQTT/CoAP) y la UI web suelen ser contenedores separados. Aquí arrancamos el núcleo (tb-node) con Kafka y Postgres. Si la UI o el MQTT no responden, agrega los servicios correspondientes (p. ej., `tb-web-ui`, `tb-mqtt-transport`).

## Requisitos
- Docker Desktop (o Docker Engine) + Docker Compose v2 (`docker compose`)
- ~3–4 GB de RAM libres

## Arranque rápido
```bash
cd docker/thingsboard
./up.sh                 # levanta postgres, kafka y thingsboard-ce

# (Primera vez) Inicializa el esquema y datos demo:
docker compose run --rm -e INSTALL_TB=true -e LOAD_DEMO=true thingsboard-ce

# Ver logs del servicio principal
./logs.sh
```

Luego abre `http://localhost:8080`.
- Si ves la UI de ThingsBoard, inicia sesión con:
   - System Administrator: sysadmin@thingsboard.org / sysadmin
   - Tenant Administrator: tenant@thingsboard.org / tenant
   - Customer User: customer@thingsboard.org / customer
- Si no carga la UI (solo REST API), instala/añade el servicio `tb-web-ui` según la guía oficial.

## Puertos expuestos
- 8080: REST API (y UI si `tb-web-ui` está embebido o habilitado)
- 29092: Kafka (PLAINTEXT para el host)
   - Interno en la red de Docker: `kafka:9092`
- 5432: Postgres (si necesitas conectarte desde el host)

MQTT/CoAP: en microservicios suelen correr en contenedores de transporte separados. No se incluyen aquí por defecto.

## Conectar tu `mqtt_node` (ESP-IDF)
- Si añadiste `tb-mqtt-transport`, configura tu firmware con:
   - `TB_HOST`: IP del host (o del contenedor donde corre MQTT); en el ESP usa la IP LAN del host.
   - `TB_PORT`: `1883`
   - `TB_TOKEN`: Device Token del dispositivo en ThingsBoard.
- Sin `tb-mqtt-transport`, el puerto 1883 no estará disponible en este stack.

## Operaciones
```bash
./logs.sh           # seguir logs de thingsboard-ce
./down.sh           # detener contenedores (datos persisten)
./reset.sh          # detener y eliminar volúmenes (cuidado)
```

## Datos persistentes
Este stack usa volúmenes con nombre de Docker:
- `tb-postgres-data`: datos de PostgreSQL
- `tb-ce-kafka-data`: datos de Kafka

`./reset.sh` elimina los contenedores y estos volúmenes para un arranque limpio.

## Notas
- Kafka está expuesto al host en `localhost:29092` (PLAINTEXT, solo para desarrollo).
- Variables de conexión dentro de la red:
   - Postgres: `postgres:5432` (usuario/clave: `thingsboard` / `thingsboard`)
   - Kafka: `kafka:9092`
- Consulta la documentación oficial de ThingsBoard 4.x para añadir transports (`tb-mqtt-transport`, `tb-coap-transport`) y la UI (`tb-web-ui`) si los necesitas.
