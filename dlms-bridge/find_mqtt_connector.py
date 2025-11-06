#!/usr/bin/env python3
"""
Script para encontrar y modificar el connector MQTT "DLMS-to-MQTT" en ThingsBoard.
"""

import requests
import json
import sys

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


def get_gateway_connectors(token, gateway_id):
    """Obtiene los connectors del gateway."""
    # Primero intentar obtener los attributes del gateway donde se guardan los connectors
    url = f"http://{TB_HOST}:{TB_PORT}/api/plugins/telemetry/DEVICE/{gateway_id}/values/attributes"
    headers = {
        "Content-Type": "application/json",
        "X-Authorization": f"Bearer {token}"
    }
    
    try:
        response = requests.get(url, headers=headers, timeout=10)
        response.raise_for_status()
        attributes = response.json()
        
        print("\n📋 Attributes del Gateway:")
        for attr in attributes:
            key = attr.get("key", "")
            value = attr.get("value", "")
            print(f"   {key}: {value}")
        
        # Buscar configuración de connectors
        for attr in attributes:
            if "connector" in attr.get("key", "").lower() or "config" in attr.get("key", "").lower():
                print(f"\n🔍 Encontrado: {attr.get('key')}")
                print(f"   Valor: {json.dumps(attr.get('value'), indent=2)}")
        
        return attributes
    except Exception as e:
        print(f"⚠️ No se pudieron obtener attributes: {e}")
        return None


def search_mqtt_connector_config(token, gateway_id):
    """Busca la configuración del connector MQTT en diferentes lugares."""
    
    print("\n🔍 Buscando configuración del connector MQTT 'DLMS-to-MQTT'...")
    
    # 1. Buscar en server attributes
    print("\n1️⃣ Buscando en SERVER attributes...")
    url = f"http://{TB_HOST}:{TB_PORT}/api/plugins/telemetry/DEVICE/{gateway_id}/values/attributes/SERVER_SCOPE"
    headers = {
        "Content-Type": "application/json",
        "X-Authorization": f"Bearer {token}"
    }
    
    try:
        response = requests.get(url, headers=headers, timeout=10)
        if response.status_code == 200:
            attrs = response.json()
            print(f"   ✅ Encontrados {len(attrs)} server attributes")
            for attr in attrs:
                key = attr.get("key", "")
                if "mqtt" in key.lower() or "connector" in key.lower():
                    print(f"   📌 {key}")
                    value = attr.get("value", {})
                    if isinstance(value, dict):
                        print(f"      {json.dumps(value, indent=6)}")
    except Exception as e:
        print(f"   ⚠️ Error: {e}")
    
    # 2. Buscar en shared attributes
    print("\n2️⃣ Buscando en SHARED attributes...")
    url = f"http://{TB_HOST}:{TB_PORT}/api/plugins/telemetry/DEVICE/{gateway_id}/values/attributes/SHARED_SCOPE"
    headers = {
        "Content-Type": "application/json",
        "X-Authorization": f"Bearer {token}"
    }
    
    try:
        response = requests.get(url, headers=headers, timeout=10)
        if response.status_code == 200:
            attrs = response.json()
            print(f"   ✅ Encontrados {len(attrs)} shared attributes")
            for attr in attrs:
                key = attr.get("key", "")
                print(f"   📌 {key}")
                value = attr.get("value", {})
                if "mqtt" in key.lower() or "connector" in key.lower() or "dlms" in key.lower():
                    if isinstance(value, str):
                        try:
                            value = json.loads(value)
                        except:
                            pass
                    print(f"      {json.dumps(value, indent=6)}")
    except Exception as e:
        print(f"   ⚠️ Error: {e}")
    
    # 3. Buscar en client attributes
    print("\n3️⃣ Buscando en CLIENT attributes...")
    url = f"http://{TB_HOST}:{TB_PORT}/api/plugins/telemetry/DEVICE/{gateway_id}/values/attributes/CLIENT_SCOPE"
    headers = {
        "Content-Type": "application/json",
        "X-Authorization": f"Bearer {token}"
    }
    
    try:
        response = requests.get(url, headers=headers, timeout=10)
        if response.status_code == 200:
            attrs = response.json()
            print(f"   ✅ Encontrados {len(attrs)} client attributes")
            for attr in attrs:
                key = attr.get("key", "")
                print(f"   📌 {key}")
                value = attr.get("value", {})
                if "mqtt" in key.lower() or "connector" in key.lower():
                    if isinstance(value, str):
                        try:
                            value = json.loads(value)
                        except:
                            pass
                    print(f"      {json.dumps(value, indent=6)}")
    except Exception as e:
        print(f"   ⚠️ Error: {e}")


def main():
    print("=" * 70)
    print("🔍 Buscar Connector MQTT 'DLMS-to-MQTT' en ThingsBoard")
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
    
    # Buscar connectors
    search_mqtt_connector_config(token, gateway_id)
    
    print("\n" + "=" * 70)
    print("📝 NOTA:")
    print("=" * 70)
    print()
    print("El connector 'DLMS-to-MQTT' que creaste manualmente probablemente")
    print("está configurado en la UI de ThingsBoard Gateway.")
    print()
    print("Para modificarlo:")
    print("1. Ve a: Devices → DLMS-BRIDGE → Attributes")
    print("2. Busca el attribute que contenga la configuración del connector")
    print("3. O ve a: Gateway → Connectors (si tu instalación tiene esta sección)")
    print()
    print("Configuración MQTT requerida:")
    print("   Broker: localhost:1883")
    print("   MQTT Version: 5")
    print("   Client ID: ThingsBoard_gateway")
    print("   Topics:")
    print("      - Subscribe: v1/gateway/connect")
    print("      - Subscribe: v1/gateway/attributes")
    print("      - Publish: v1/gateway/telemetry")
    print("      - Publish: v1/gateway/attributes/response")
    
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
