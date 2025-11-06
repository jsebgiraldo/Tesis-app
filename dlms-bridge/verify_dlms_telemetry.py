#!/usr/bin/env python3
"""
Verifica que los datos DLMS estén llegando a ThingsBoard.
"""

import requests
import json
from datetime import datetime

TB_HOST = "localhost"
TB_PORT = 8080
TB_USERNAME = "tenant@thingsboard.org"
TB_PASSWORD = "tenant"


def get_token():
    url = f"http://{TB_HOST}:{TB_PORT}/api/auth/login"
    payload = {"username": TB_USERNAME, "password": TB_PASSWORD}
    response = requests.post(url, json=payload, timeout=10)
    response.raise_for_status()
    return response.json()["token"]


def find_device(token, device_name):
    """Busca dispositivo por nombre."""
    url = f"http://{TB_HOST}:{TB_PORT}/api/tenant/devices?pageSize=100&page=0"
    headers = {"X-Authorization": f"Bearer {token}"}
    
    response = requests.get(url, headers=headers, timeout=10)
    response.raise_for_status()
    
    devices = response.json().get("data", [])
    for device in devices:
        if device_name in device["name"]:
            return device
    
    return None


def get_latest_telemetry(token, device_id):
    """Obtiene la última telemetría del dispositivo."""
    url = f"http://{TB_HOST}:{TB_PORT}/api/plugins/telemetry/DEVICE/{device_id}/values/timeseries"
    headers = {"X-Authorization": f"Bearer {token}"}
    
    response = requests.get(url, headers=headers, timeout=10)
    response.raise_for_status()
    
    return response.json()


def main():
    print("=" * 70)
    print("📊 Verificar Telemetría DLMS en ThingsBoard")
    print("=" * 70)
    print()
    
    # Autenticar
    print("🔑 Autenticando...")
    token = get_token()
    print("✅ Autenticado")
    
    # Buscar dispositivo DLMS-Meter-01
    print("\n🔍 Buscando dispositivo DLMS-Meter-01...")
    device = find_device(token, "DLMS-Meter-01")
    
    if not device:
        print("❌ Dispositivo DLMS-Meter-01 no encontrado")
        print("\n💡 Nota: El Gateway connector debe crear este dispositivo")
        print("   cuando reciba el primer mensaje MQTT.")
        return 1
    
    device_id = device["id"]["id"]
    device_name = device["name"]
    print(f"✅ Dispositivo encontrado: {device_name}")
    print(f"   ID: {device_id}")
    print(f"   Type: {device.get('type', 'N/A')}")
    
    # Obtener telemetría
    print("\n📊 Obteniendo última telemetría...")
    telemetry = get_latest_telemetry(token, device_id)
    
    if not telemetry:
        print("❌ No hay datos de telemetría")
        return 1
    
    print("✅ Telemetría recibida:")
    print()
    
    for key, values in telemetry.items():
        if values and len(values) > 0:
            latest = values[0]
            timestamp = datetime.fromtimestamp(latest["ts"] / 1000)
            value = latest["value"]
            print(f"   📈 {key:15s}: {value:10s}  ({timestamp})")
    
    print()
    print("=" * 70)
    print("✅ Verificación completada")
    print("=" * 70)
    
    return 0


if __name__ == "__main__":
    try:
        exit(main())
    except Exception as e:
        print(f"\n❌ Error: {e}")
        import traceback
        traceback.print_exc()
        exit(1)
