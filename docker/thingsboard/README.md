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

# (Primera vez) Inicializa el esquema (y opcionalmente datos demo):
./install.sh                  # solo esquema
LOAD_DEMO=true ./install.sh   # esquema + datos demo

# Ver logs del servicio principal
./logs.sh
```

Luego abre `http://localhost:18080` (el contenedor expone `8080` como `18080` en el host).
- Si ves la UI de ThingsBoard, inicia sesión con:
   - System Administrator: sysadmin@thingsboard.org / sysadmin
   - Tenant Administrator: tenant@thingsboard.org / tenant
   - Customer User: customer@thingsboard.org / customer
- Si no carga la UI (solo REST API), instala/añade el servicio `tb-web-ui` según la guía oficial.

## Puertos expuestos
- 18080->8080: REST API (y UI si `tb-web-ui` está embebido o habilitado)
- 17070->7070: gRPC de transporte interno (si corresponde)
- 11883->1883: MQTT (si está habilitado en tb-node)
- 18883->8883: MQTTs (si está habilitado en tb-node)
- 15683-15688->5683-5688/udp: CoAP/LwM2M (si están habilitados)
- 9092: Kafka (PLAINTEXT dentro de Docker y expuesto al host en el mismo puerto)
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
- Variables de conexión dentro de la red:
   - Postgres: `postgres:5432` (usuario/clave por defecto: `postgres` / `postgres`)
   - Kafka: `kafka:9092`
- Consulta la documentación oficial de ThingsBoard 4.x para añadir transports (`tb-mqtt-transport`, `tb-coap-transport`) y la UI (`tb-web-ui`) si los necesitas.
