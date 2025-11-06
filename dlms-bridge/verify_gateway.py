#!/usr/bin/env python3
"""
Script para verificar conexión y datos del Gateway en ThingsBoard.

Muestra:
- Estado del Gateway
- Últimos datos de telemetría recibidos
- Configuración MQTT

Requiere: Credenciales de admin de ThingsBoard
"""

import requests
import json
import sys
from datetime import datetime

# Configuración ThingsBoard
TB_HOST = "localhost"  # Cambiar si es remoto
TB_PORT = 8080
TB_USERNAME = "tenant@thingsboard.org"  # Usuario por defecto
TB_PASSWORD = "tenant"  # Cambiar si es diferente

# Gateway name
GATEWAY_NAME = "DLMS-BRIDGE"


def get_token():
    """Obtiene token de autenticación de ThingsBoard."""
    url = f"http://{TB_HOST}:{TB_PORT}/api/auth/login"
    payload = {
        "username": TB_USERNAME,
        "password": TB_PASSWORD
    }
    headers = {"Content-Type": "application/json"}
    
    try:
        response = requests.post(url, json=payload, headers=headers, timeout=10)
        response.raise_for_status()
        return response.json()["token"]
    except Exception as e:
        print(f"❌ Error obteniendo token: {e}")
        print(f"   Verifica usuario/password en {url}")
        return None


def get_gateway_info(token):
    """Obtiene información del Gateway."""
    url = f"http://{TB_HOST}:{TB_PORT}/api/tenant/devices?deviceName={GATEWAY_NAME}"
    headers = {
        "Content-Type": "application/json",
        "X-Authorization": f"Bearer {token}"
    }
    
    try:
        response = requests.get(url, headers=headers, timeout=10)
        response.raise_for_status()
        return response.json()
    except Exception as e:
        print(f"❌ Error obteniendo Gateway: {e}")
        return None


def get_telemetry(token, device_id):
    """Obtiene últimas telemetry del dispositivo."""
    url = f"http://{TB_HOST}:{TB_PORT}/api/plugins/telemetry/DEVICE/{device_id}/values/timeseries"
    headers = {
        "Content-Type": "application/json",
        "X-Authorization": f"Bearer {token}"
    }
    
    try:
        response = requests.get(url, headers=headers, timeout=10)
        response.raise_for_status()
        return response.json()
    except Exception as e:
        print(f"❌ Error obteniendo telemetry: {e}")
        return None


def get_attributes(token, device_id):
    """Obtiene attributes del dispositivo."""
    url = f"http://{TB_HOST}:{TB_PORT}/api/plugins/telemetry/DEVICE/{device_id}/values/attributes"
    headers = {
        "Content-Type": "application/json",
        "X-Authorization": f"Bearer {token}"
    }
    
    try:
        response = requests.get(url, headers=headers, timeout=10)
        response.raise_for_status()
        return response.json()
    except Exception as e:
        print(f"⚠️ Error obteniendo attributes: {e}")
        return None


def format_timestamp(ts):
    """Formatea timestamp de milisegundos a fecha legible."""
    try:
        return datetime.fromtimestamp(ts / 1000).strftime('%Y-%m-%d %H:%M:%S')
    except:
        return "N/A"


def main():
    print("=" * 70)
    print("🔍 Verificación de Gateway ThingsBoard - DLMS-BRIDGE")
    print("=" * 70)
    print(f"Servidor: {TB_HOST}:{TB_PORT}")
    print(f"Gateway: {GATEWAY_NAME}")
    print()
    
    # 1. Obtener token
    print("🔑 Autenticando...")
    token = get_token()
    if not token:
        print("\n❌ No se pudo autenticar. Verifica:")
        print("   1. ThingsBoard está corriendo")
        print("   2. Usuario/password son correctos")
        print(f"   3. URL es correcta: http://{TB_HOST}:{TB_PORT}")
        return 1
    print("✅ Autenticado correctamente")
    print()
    
    # 2. Buscar Gateway
    print(f"🔍 Buscando Gateway '{GATEWAY_NAME}'...")
    gateway = get_gateway_info(token)
    if not gateway:
        print(f"\n❌ Gateway '{GATEWAY_NAME}' no encontrado en ThingsBoard")
        print("\nPara crear el Gateway:")
        print("   1. Ir a: Devices → Add Device")
        print(f"   2. Nombre: {GATEWAY_NAME}")
        print("   3. Tipo: Gateway")
        print("   4. Copiar el Access Token")
        print("   5. Actualizar en database:")
        print("      UPDATE meters SET tb_token='TOKEN_AQUI' WHERE id=1;")
        return 1
    
    device_id = gateway["id"]["id"]
    print(f"✅ Gateway encontrado")
    print(f"   ID: {device_id}")
    print(f"   Nombre: {gateway['name']}")
    print(f"   Tipo: {gateway.get('type', 'N/A')}")
    print()
    
    # 3. Verificar telemetry
    print("📊 Obteniendo telemetría...")
    telemetry = get_telemetry(token, device_id)
    
    if telemetry and len(telemetry) > 0:
        print(f"✅ Telemetría recibida: {len(telemetry)} series")
        print()
        print("   Últimos valores:")
        for key, data_list in sorted(telemetry.items()):
            if data_list and len(data_list) > 0:
                latest = data_list[0]
                value = latest.get('value', 'N/A')
                ts = latest.get('ts', 0)
                ts_formatted = format_timestamp(ts)
                print(f"      {key:20s}: {value:>10s}  ({ts_formatted})")
    else:
        print("⚠️ Sin telemetría reciente")
        print()
        print("   Posibles causas:")
        print("   1. El bridge no está publicando (verificar logs)")
        print("   2. Token incorrecto en database")
        print("   3. MQTT broker no está corriendo")
        print()
        print("   Verificar:")
        print("      sudo journalctl -u tb-gateway-dlms.service -f")
    
    print()
    
    # 4. Verificar attributes
    print("📋 Obteniendo attributes...")
    attributes = get_attributes(token, device_id)
    
    if attributes and len(attributes) > 0:
        print(f"✅ Attributes: {len(attributes)} encontrados")
        print()
        print("   Configuración:")
        for attr in attributes:
            key = attr.get('key', 'N/A')
            value = attr.get('value', 'N/A')
            print(f"      {key:20s}: {value}")
    else:
        print("⚠️ Sin attributes configurados")
    
    print()
    print("=" * 70)
    print("✅ Verificación completada")
    print("=" * 70)
    print()
    print("🌐 Ver en ThingsBoard:")
    print(f"   http://{TB_HOST}:{TB_PORT}/devices/{device_id}")
    print()
    print("📊 Ver telemetría en tiempo real:")
    print(f"   http://{TB_HOST}:{TB_PORT}/devices/{device_id}/latest")
    print()
    
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
