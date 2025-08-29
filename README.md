# Tesis IoT Nodes Lab

Este repositorio organiza experimentos para evaluar conectividad IP en nodos IoT (ESP32 y simulación con QEMU) usando protocolos: LwM2M, MQTT, CoAP y HTTP.

## Estructura
- `docs/` Documentación y plantillas.
- `protocols/` Notas y guías por protocolo (LwM2M, MQTT, CoAP, HTTP).
- `projects/` Proyectos ESP-IDF por protocolo y utilidades comunes.
- `simulations/qemu/` Escenarios de simulación y scripts.

## Objetivo
Generar, capturar y comparar tráfico de red entre protocolos para métricas: latencia, overhead, consumo energético aproximado, fiabilidad.

## Requisitos iniciales
- macOS con Homebrew
- ESP-IDF (v5.x) y toolchains ESP32/ESP32-C3
- Python 3.11+
- Wireshark y tshark
- QEMU

## Primeros pasos
1. Configurar entorno (ver `docs/setup/macOS.md`).
2. Crear proyecto base ESP-IDF (ver `projects/README.md`).
3. Preparar experimento y registro (ver `docs/templates/experimento.md`).
