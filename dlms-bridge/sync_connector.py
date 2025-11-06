#!/usr/bin/env python3
"""
Script para sincronizar el connector DLMS-to-MQTT después de actualizar su configuración.
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


def sync_connector(token, gateway_id):
    """Fuerza la sincronización del connector mediante RPC."""
    
    # Método 1: Usar el endpoint de gateway RPC para sync
    url = f"http://{TB_HOST}:{TB_PORT}/api/plugins/rpc/twoway/{gateway_id}"
    headers = {
        "Content-Type": "application/json",
        "X-Authorization": f"Bearer {token}"
    }
    
    # Comando para sincronizar/reiniciar el connector
    payload = {
        "method": "gateway_sync",
        "params": {},
        "timeout": 5000
    }
    
    try:
        print("🔄 Intentando sincronizar connector...")
        response = requests.post(url, json=payload, headers=headers, timeout=10)
        
        if response.status_code == 200:
            print("✅ Sincronización exitosa")
            return True
        else:
            print(f"⚠️  Response: {response.status_code}")
    except Exception as e:
        print(f"⚠️  Método RPC no disponible: {e}")
    
    return False


def trigger_attribute_update(token, gateway_id):
    """Fuerza actualización mediante modificación de atributo."""
    
    # Leer configuración actual
    url = f"http://{TB_HOST}:{TB_PORT}/api/plugins/telemetry/DEVICE/{gateway_id}/values/attributes/SHARED_SCOPE"
    headers = {
        "Content-Type": "application/json",
        "X-Authorization": f"Bearer {token}"
    }
    
    try:
        response = requests.get(url, headers=headers, timeout=10)
        response.raise_for_status()
        attributes = response.json()
        
        # Buscar el connector config
        connector_config = None
        for attr in attributes:
            if attr.get("key") == "DLMS-to-MQTT":
                connector_config = attr.get("value")
                break
        
        if not connector_config:
            print("❌ No se encontró la configuración del connector")
            return False
        
        # Actualizar timestamp para forzar sync
        connector_config["ts"] = int(time.time() * 1000)
        
        # Escribir de vuelta
        url = f"http://{TB_HOST}:{TB_PORT}/api/plugins/telemetry/DEVICE/{gateway_id}/SHARED_SCOPE"
        payload = {
            "DLMS-to-MQTT": connector_config
        }
        
        response = requests.post(url, json=payload, headers=headers, timeout=10)
        response.raise_for_status()
        
        print("✅ Timestamp actualizado - forzando sync")
        return True
        
    except Exception as e:
        print(f"❌ Error actualizando atributo: {e}")
        return False


def main():
    print("=" * 70)
    print("🔄 Sincronizar Connector DLMS-to-MQTT")
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
    
    # Sincronizar
    print("\n🔄 Sincronizando connector...")
    
    # Intentar RPC
    if not sync_connector(token, gateway_id):
        # Si RPC no funciona, actualizar timestamp
        print("\n🔄 Forzando sync mediante actualización de timestamp...")
        if trigger_attribute_update(token, gateway_id):
            print("✅ Sync forzado")
        else:
            print("❌ No se pudo forzar sync")
            return 1
    
    print()
    print("=" * 70)
    print("✅ Sincronización completada")
    print("=" * 70)
    print()
    print("📝 Pasos siguientes:")
    print("   1. Espera 5-10 segundos")
    print("   2. Recarga la página de ThingsBoard UI")
    print("   3. Ve a: Gateway List → DLMS-BRIDGE → Connectors")
    print("   4. El estado debe cambiar de 'OUT OF SYNC' a 'SYNCED'")
    print()
    print("   Si sigue 'OUT OF SYNC', reinicia el servicio:")
    print("   sudo systemctl restart tb-gateway-dlms.service")
    
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
