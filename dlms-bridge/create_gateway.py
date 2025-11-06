#!/usr/bin/env python3
"""
Script para crear automáticamente el Gateway DLMS-BRIDGE en ThingsBoard.

Crea el dispositivo Gateway y actualiza el token en la database.
"""

import requests
import json
import sys
from admin.database import Database, get_meter_by_id

# Configuración ThingsBoard
TB_HOST = "localhost"
TB_PORT = 8080
TB_USERNAME = "tenant@thingsboard.org"
TB_PASSWORD = "tenant"

# Gateway config
GATEWAY_NAME = "DLMS-BRIDGE"
GATEWAY_LABEL = "DLMS Energy Meter Gateway"


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


def create_gateway(token):
    """Crea el Gateway en ThingsBoard."""
    url = f"http://{TB_HOST}:{TB_PORT}/api/device"
    headers = {
        "Content-Type": "application/json",
        "X-Authorization": f"Bearer {token}"
    }
    
    payload = {
        "name": GATEWAY_NAME,
        "label": GATEWAY_LABEL,
        "additionalInfo": {
            "gateway": True,
            "description": "Gateway para medidores DLMS/COSEM"
        }
    }
    
    try:
        response = requests.post(url, json=payload, headers=headers, timeout=10)
        response.raise_for_status()
        return response.json()
    except Exception as e:
        print(f"❌ Error creando Gateway: {e}")
        if hasattr(e, 'response') and e.response:
            print(f"   Response: {e.response.text}")
        return None


def get_device_credentials(token, device_id):
    """Obtiene las credenciales (access token) del dispositivo."""
    url = f"http://{TB_HOST}:{TB_PORT}/api/device/{device_id}/credentials"
    headers = {
        "Content-Type": "application/json",
        "X-Authorization": f"Bearer {token}"
    }
    
    try:
        response = requests.get(url, headers=headers, timeout=10)
        response.raise_for_status()
        creds = response.json()
        return creds.get("credentialsId", None)  # Este es el access token
    except Exception as e:
        print(f"❌ Error obteniendo credenciales: {e}")
        return None


def update_database_token(access_token):
    """Actualiza el token en la database."""
    try:
        db = Database('data/admin.db')
        session = db.get_session()
        
        meter = get_meter_by_id(session, 1)
        if meter:
            meter.tb_token = access_token
            meter.tb_host = "localhost"
            meter.tb_port = 1883
            meter.tb_enabled = True
            session.commit()
            session.close()
            return True
        else:
            session.close()
            print("❌ No se encontró medidor ID=1 en database")
            return False
    except Exception as e:
        print(f"❌ Error actualizando database: {e}")
        return False


def main():
    print("=" * 70)
    print("🏗️  Crear Gateway DLMS-BRIDGE en ThingsBoard")
    print("=" * 70)
    print(f"Servidor: {TB_HOST}:{TB_PORT}")
    print(f"Gateway: {GATEWAY_NAME}")
    print()
    
    # 1. Autenticar
    print("🔑 Autenticando...")
    auth_token = get_token()
    if not auth_token:
        print("\n❌ No se pudo autenticar")
        return 1
    print("✅ Autenticado")
    print()
    
    # 2. Crear Gateway
    print(f"🏗️  Creando Gateway '{GATEWAY_NAME}'...")
    gateway = create_gateway(auth_token)
    if not gateway:
        print("\n❌ No se pudo crear Gateway")
        print("\nPosibles causas:")
        print("   1. Ya existe un dispositivo con ese nombre")
        print("   2. No tienes permisos")
        print("\nPrueba eliminarlo primero en la UI y vuelve a ejecutar")
        return 1
    
    device_id = gateway["id"]["id"]
    print(f"✅ Gateway creado")
    print(f"   ID: {device_id}")
    print(f"   Nombre: {gateway['name']}")
    print()
    
    # 3. Obtener Access Token
    print("🔑 Obteniendo Access Token...")
    access_token = get_device_credentials(auth_token, device_id)
    if not access_token:
        print("\n⚠️ No se pudo obtener Access Token")
        print(f"   Ve a la UI y cópialo manualmente:")
        print(f"   http://{TB_HOST}:{TB_PORT}/devices/{device_id}")
        return 1
    
    print(f"✅ Access Token obtenido")
    print(f"   Token: {access_token}")
    print()
    
    # 4. Actualizar Database
    print("💾 Actualizando database...")
    if not update_database_token(access_token):
        print("\n⚠️ No se pudo actualizar database")
        print(f"   Actualiza manualmente:")
        print(f"   UPDATE meters SET tb_token='{access_token}' WHERE id=1;")
        return 1
    
    print("✅ Database actualizada")
    print()
    
    # 5. Reiniciar servicio
    print("🔄 Reiniciando servicio...")
    import subprocess
    try:
        subprocess.run(["sudo", "systemctl", "restart", "tb-gateway-dlms.service"], 
                      check=True, timeout=10)
        print("✅ Servicio reiniciado")
    except Exception as e:
        print(f"⚠️ Error reiniciando servicio: {e}")
        print("   Reinicia manualmente:")
        print("   sudo systemctl restart tb-gateway-dlms.service")
    
    print()
    print("=" * 70)
    print("✅ Gateway DLMS-BRIDGE creado y configurado correctamente")
    print("=" * 70)
    print()
    print("🌐 Ver en ThingsBoard:")
    print(f"   http://{TB_HOST}:{TB_PORT}/devices/{device_id}")
    print()
    print("📊 Verificar telemetría:")
    print("   python3 verify_gateway.py")
    print()
    print("📜 Ver logs del servicio:")
    print("   sudo journalctl -u tb-gateway-dlms.service -f")
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
