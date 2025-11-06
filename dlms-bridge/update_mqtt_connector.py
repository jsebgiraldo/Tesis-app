#!/usr/bin/env python3
"""
Script para actualizar el connector MQTT 'DLMS-to-MQTT' con los tópicos correctos.
"""

import requests
import json
import sys
import time

# Configuración ThingsBoard
TB_HOST = "localhost"
TB_PORT = 8080
TB_USERNAME = "tenant@thingsboard.org"
TB_PASSWORD = "tenant"


def get_token():
    """Obtiene token de autenticación."""
    url = f"http://{TB_HOST}:{TB_PORT}/api/auth/login"
    payload = {"username": TB_USERNAME, "password": TB_PASSWORD}
    headers = {"Content-Type": "application/json"}
    
    try:
        response = requests.post(url, json=payload, headers=headers, timeout=10)
        response.raise_for_status()
        return response.json()["token"]
    except Exception as e:
        print(f"❌ Error autenticando: {e}")
        return None


def get_gateway_device(token):
    """Busca el dispositivo Gateway DLMS-BRIDGE."""
    url = f"http://{TB_HOST}:{TB_PORT}/api/tenant/devices?pageSize=100&page=0"
    headers = {
        "Content-Type": "application/json",
        "X-Authorization": f"Bearer {token}"
    }
    
    try:
        response = requests.get(url, headers=headers, timeout=10)
        response.raise_for_status()
        devices = response.json().get("data", [])
        
        for device in devices:
            if "DLMS" in device["name"].upper() and "BRIDGE" in device["name"].upper():
                return device
        
        return None
    except Exception as e:
        print(f"❌ Error buscando gateway: {e}")
        return None


def update_mqtt_connector(token, gateway_id):
    """Actualiza la configuración del connector MQTT con los tópicos DLMS."""
    
    # Configuración actualizada del connector
    # SIMPLIFICADA: Solo escucha el tópico que el bridge usa
    connector_config = {
        "mode": "basic",
        "name": "DLMS-to-MQTT",
        "type": "mqtt",
        "enableRemoteLogging": True,
        "logLevel": "INFO",
        "sendDataOnlyOnChange": False,
        "configuration": "dlmsToMqtt.json",
        "configurationJson": {
            "broker": {
                "host": "127.0.0.1",
                "port": 1884,
                "version": 3,
                "clientId": "dlms-connector",
                "security": {
                    "type": "anonymous"
                },
                "maxNumberOfWorkers": 100,
                "maxMessageNumberPerWorker": 10
            },
            "mapping": [
                {
                    "topicFilter": "v1/devices/me/telemetry",
                    "converter": {
                        "type": "json",
                        "deviceNameJsonExpression": "DLMS-Meter-01",
                        "deviceTypeJsonExpression": "DLMS Energy Meter",
                        "sendDataOnlyOnChange": False,
                        "timeout": 60000,
                        "attributes": [],
                        "timeseries": [
                            {
                                "type": "double",
                                "key": "voltage_l1",
                                "value": "${voltage_l1}"
                            },
                            {
                                "type": "double",
                                "key": "current_l1",
                                "value": "${current_l1}"
                            },
                            {
                                "type": "double",
                                "key": "frequency",
                                "value": "${frequency}"
                            },
                            {
                                "type": "double",
                                "key": "active_power",
                                "value": "${active_power}"
                            },
                            {
                                "type": "double",
                                "key": "active_energy",
                                "value": "${active_energy}"
                            }
                        ]
                    }
                }
            ]
        },
        "configVersion": "",
        "ts": int(time.time() * 1000)
    }
    
    # Actualizar el attribute en ThingsBoard
    url = f"http://{TB_HOST}:{TB_PORT}/api/plugins/telemetry/DEVICE/{gateway_id}/SHARED_SCOPE"
    headers = {
        "Content-Type": "application/json",
        "X-Authorization": f"Bearer {token}"
    }
    
    payload = {
        "DLMS-to-MQTT": connector_config
    }
    
    try:
        response = requests.post(url, json=payload, headers=headers, timeout=10)
        response.raise_for_status()
        return True
    except Exception as e:
        print(f"❌ Error actualizando connector: {e}")
        if hasattr(e, 'response') and e.response:
            print(f"   Response: {e.response.text}")
        return False


def main():
    print("=" * 70)
    print("🔧 Actualizar Connector MQTT 'DLMS-to-MQTT'")
    print("=" * 70)
    print()
    
    # Autenticar
    print("🔑 Autenticando...")
    token = get_token()
    if not token:
        return 1
    print("✅ Autenticado")
    
    # Buscar Gateway
    print("\n🌉 Buscando Gateway DLMS-BRIDGE...")
    gateway = get_gateway_device(token)
    if not gateway:
        print("❌ No se encontró el Gateway DLMS-BRIDGE")
        return 1
    
    gateway_id = gateway["id"]["id"]
    gateway_name = gateway["name"]
    print(f"✅ Gateway encontrado: {gateway_name}")
    print(f"   ID: {gateway_id}")
    
    # Actualizar connector
    print("\n🔧 Actualizando configuración del connector...")
    print()
    print("📝 Nuevos tópicos MQTT:")
    print("   Broker: 127.0.0.1:1883")
    print("   MQTT Version: 5")
    print("   Client ID: ThingsBoard_gateway")
    print()
    print("   Data Mapping:")
    print("   ✅ v1/devices/me/telemetry → DLMS-Meter-01")
    print()
    print("   📊 Telemetry configurada:")
    print("      - voltage_l1")
    print("      - current_l1")
    print("      - frequency")
    print("      - active_power")
    print("      - active_energy")
    print()
    
    if update_mqtt_connector(token, gateway_id):
        print("✅ Connector actualizado correctamente")
        print()
        print("📊 Configuración simplificada:")
        print("   - Solo 1 mapping: v1/devices/me/telemetry")
        print("   - Device: DLMS-Meter-01")
        print("   - Type: DLMS Energy Meter")
        print()
        print("=" * 70)
        print("✅ Configuración completada")
        print("=" * 70)
        print()
        print("🔄 Ahora reinicia el servicio:")
        print("   sudo systemctl restart tb-gateway-dlms.service")
        print()
        print("📊 Verifica los datos:")
        print("   python3 verify_gateway.py")
    else:
        print("❌ No se pudo actualizar el connector")
        return 1
    
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n\n⚠️ Cancelado por usuario")
        sys.exit(1)
    except Exception as e:
        print(f"\n❌ Error inesperado: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
