#!/usr/bin/env python3
"""
Script para listar TODOS los dispositivos en ThingsBoard y gestionar gateways.
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


def list_all_devices(token):
    """Lista todos los dispositivos."""
    url = f"http://{TB_HOST}:{TB_PORT}/api/tenant/devices?pageSize=100&page=0"
    headers = {
        "Content-Type": "application/json",
        "X-Authorization": f"Bearer {token}"
    }
    
    try:
        response = requests.get(url, headers=headers, timeout=10)
        response.raise_for_status()
        return response.json().get("data", [])
    except Exception as e:
        print(f"❌ Error listando dispositivos: {e}")
        return []


def get_device_credentials(token, device_id):
    """Obtiene las credenciales del dispositivo."""
    url = f"http://{TB_HOST}:{TB_PORT}/api/device/{device_id}/credentials"
    headers = {
        "Content-Type": "application/json",
        "X-Authorization": f"Bearer {token}"
    }
    
    try:
        response = requests.get(url, headers=headers, timeout=10)
        response.raise_for_status()
        return response.json()
    except Exception as e:
        return None


def delete_device(token, device_id):
    """Elimina un dispositivo."""
    url = f"http://{TB_HOST}:{TB_PORT}/api/device/{device_id}"
    headers = {
        "Content-Type": "application/json",
        "X-Authorization": f"Bearer {token}"
    }
    
    try:
        response = requests.delete(url, headers=headers, timeout=10)
        response.raise_for_status()
        return True
    except Exception as e:
        print(f"   ❌ Error eliminando: {e}")
        return False


def main():
    print("=" * 70)
    print("🔍 Gestión de Dispositivos ThingsBoard")
    print("=" * 70)
    print()
    
    # Autenticar
    print("🔑 Autenticando...")
    token = get_token()
    if not token:
        return 1
    print("✅ Autenticado")
    print()
    
    # Listar TODOS los dispositivos
    print("📋 Listando TODOS los dispositivos...")
    devices = list_all_devices(token)
    
    if not devices:
        print("⚠️ No se encontraron dispositivos")
        return 0
    
    print(f"✅ Se encontraron {len(devices)} dispositivos totales")
    print()
    
    # Mostrar todos los dispositivos
    print("📱 TODOS LOS DISPOSITIVOS:")
    print()
    
    gateways = []
    other_devices = []
    
    for device in devices:
        device_id = device["id"]["id"]
        name = device["name"]
        device_type = device.get("type", "default")
        label = device.get("label", "")
        created = device.get("createdTime", 0)
        
        # Obtener credenciales
        creds = get_device_credentials(token, device_id)
        access_token = creds.get("credentialsId", "N/A") if creds else "N/A"
        
        # Clasificar
        is_gateway = "gateway" in label.lower() or "bridge" in name.lower() or "dlms" in name.lower()
        
        device_info = {
            "id": device_id,
            "name": name,
            "type": device_type,
            "label": label,
            "created": created,
            "token": access_token,
            "raw": device
        }
        
        if is_gateway:
            gateways.append(device_info)
        else:
            other_devices.append(device_info)
    
    # Mostrar gateways
    if gateways:
        print(f"🌉 GATEWAYS ({len(gateways)}):")
        print()
        for i, gw in enumerate(gateways, 1):
            print(f"   [{i}] {gw['name']}")
            print(f"       ID: {gw['id']}")
            print(f"       Type: {gw['type']}")
            print(f"       Label: {gw['label']}")
            print(f"       Token: {gw['token']}")
            print(f"       Created: {gw['created']}")
            print()
    
    # Mostrar otros dispositivos
    if other_devices:
        print(f"📱 OTROS DISPOSITIVOS ({len(other_devices)}):")
        print()
        for i, dev in enumerate(other_devices, 1):
            print(f"   [{i}] {dev['name']}")
            print(f"       ID: {dev['id']}")
            print(f"       Type: {dev['type']}")
            print(f"       Token: {dev['token']}")
            print()
    
    # Menú de acción
    print("=" * 70)
    print("ACCIONES:")
    print()
    print("1) Eliminar gateways viejos/inactivos")
    print("2) Mostrar token del gateway activo")
    print("3) Salir")
    print()
    
    try:
        choice = input("Selecciona una opción: ").strip()
        
        if choice == "1":
            if not gateways:
                print("\n⚠️ No hay gateways para eliminar")
                return 0
            
            print("\n🗑️  ¿Qué gateways deseas ELIMINAR?")
            print()
            for i, gw in enumerate(gateways, 1):
                print(f"   {i}) {gw['name']} - Token: {gw['token'][:20]}...")
            print(f"   0) Cancelar")
            print()
            
            to_delete = input("Números separados por comas (ej: 1,2): ").strip()
            
            if to_delete == "0" or not to_delete:
                print("\n⚠️ Cancelado")
                return 0
            
            indices = [int(x.strip()) for x in to_delete.split(",")]
            
            print()
            for idx in indices:
                if 1 <= idx <= len(gateways):
                    gw = gateways[idx - 1]
                    print(f"🗑️  Eliminando: {gw['name']}...")
                    if delete_device(token, gw['id']):
                        print(f"   ✅ Eliminado correctamente")
                    print()
            
            print("✅ Operación completada")
        
        elif choice == "2":
            if not gateways:
                print("\n⚠️ No hay gateways")
                return 0
            
            print("\nGateways con sus tokens:")
            for gw in gateways:
                print(f"\n   {gw['name']}")
                print(f"   Token: {gw['token']}")
        
        else:
            print("\n⚠️ Cancelado")
    
    except (ValueError, KeyboardInterrupt):
        print("\n⚠️ Cancelado")
        return 0
    
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
