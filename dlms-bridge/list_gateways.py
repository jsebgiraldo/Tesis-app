#!/usr/bin/env python3
"""
Script para listar todos los Gateways en ThingsBoard y eliminar duplicados.
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
        print(f"❌ Error eliminando dispositivo: {e}")
        return False


def main():
    print("=" * 70)
    print("🔍 Listar Gateways en ThingsBoard")
    print("=" * 70)
    print()
    
    # Autenticar
    print("🔑 Autenticando...")
    token = get_token()
    if not token:
        return 1
    print("✅ Autenticado")
    print()
    
    # Listar dispositivos
    print("📋 Listando dispositivos...")
    devices = list_all_devices(token)
    
    if not devices:
        print("⚠️ No se encontraron dispositivos")
        return 0
    
    print(f"✅ Se encontraron {len(devices)} dispositivos")
    print()
    
    # Filtrar gateways con nombre similar
    gateways = [d for d in devices if "DLMS" in d["name"].upper() or "BRIDGE" in d["name"].upper()]
    
    if not gateways:
        print("⚠️ No se encontraron gateways DLMS")
        return 0
    
    print(f"🌉 Gateways encontrados ({len(gateways)}):")
    print()
    
    for i, gw in enumerate(gateways, 1):
        device_id = gw["id"]["id"]
        name = gw["name"]
        label = gw.get("label", "N/A")
        created = gw.get("createdTime", 0)
        
        print(f"   [{i}] {name}")
        print(f"       ID: {device_id}")
        print(f"       Label: {label}")
        print(f"       Created: {created}")
        print()
    
    # Preguntar cuál eliminar
    if len(gateways) > 1:
        print("⚠️ Se encontraron múltiples gateways")
        print()
        print("¿Cuál gateway deseas CONSERVAR? (Los demás se eliminarán)")
        print()
        
        for i, gw in enumerate(gateways, 1):
            print(f"   {i}) {gw['name']} (ID: {gw['id']['id']})")
        
        print(f"   0) Cancelar (no eliminar nada)")
        print()
        
        try:
            choice = input("Selecciona el número del gateway a CONSERVAR: ").strip()
            choice_num = int(choice)
            
            if choice_num == 0:
                print("\n⚠️ Operación cancelada")
                return 0
            
            if choice_num < 1 or choice_num > len(gateways):
                print("\n❌ Opción inválida")
                return 1
            
            # Gateway a conservar
            keep_gateway = gateways[choice_num - 1]
            keep_id = keep_gateway["id"]["id"]
            keep_name = keep_gateway["name"]
            
            print(f"\n✅ Conservando: {keep_name}")
            print()
            
            # Eliminar los demás
            for gw in gateways:
                gw_id = gw["id"]["id"]
                gw_name = gw["name"]
                
                if gw_id != keep_id:
                    print(f"🗑️  Eliminando: {gw_name}...")
                    if delete_device(token, gw_id):
                        print(f"   ✅ Eliminado correctamente")
                    else:
                        print(f"   ❌ Error al eliminar")
                    print()
            
            print("=" * 70)
            print("✅ Limpieza completada")
            print("=" * 70)
            print()
            print(f"Gateway conservado: {keep_name}")
            print(f"ID: {keep_id}")
            print()
            print("Ahora actualiza el token en la database:")
            print(f"   python3 update_gateway_token.py {keep_id}")
            
        except (ValueError, KeyboardInterrupt):
            print("\n⚠️ Operación cancelada")
            return 0
    else:
        print("✅ Solo hay 1 gateway, no hay duplicados")
        gw = gateways[0]
        print(f"   Nombre: {gw['name']}")
        print(f"   ID: {gw['id']['id']}")
    
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
