# MQTT

## Stack
- Broker: Mosquitto / EMQX
- Cliente: ESP-IDF mqtt client

## Escenarios
- QoS 0/1/2, keepalive 30-120s
- Payloads: 16B, 64B, 256B, 1KB
- Intervalo envío: 1s, 5s, 10s

## Métricas
- RTT publish->ack (QoS1)
- Bytes totales por mensaje
- Conexiones/reconexiones

## Referencias
- ESP-IDF MQTT: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mqtt.html
