#!/usr/bin/env python3
"""
ThingsBoard Auto-Provisioning para DLMS Bridge.

Crea automáticamente el dispositivo en ThingsBoard y obtiene el access token.
Funciona con instancias locales de ThingsBoard.
"""

import json
import sys
import requests
import logging
from typing import Optional, Dict

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class ThingsBoardProvisioner:
    """Cliente para provisioning automático en ThingsBoard."""
    
    def __init__(
        self,
        host: str = "localhost",
        port: int = 8080,
        username: str = "tenant@thingsboard.org",
        password: str = "tenant"
    ):
        """
        Inicializa el provisionador.
        
        Args:
            host: Host de ThingsBoard (default: localhost)
            port: Puerto HTTP de ThingsBoard (default: 8080)
            username: Usuario de ThingsBoard (default: tenant@thingsboard.org)
            password: Contraseña (default: tenant)
        """
        self.base_url = f"http://{host}:{port}"
        self.username = username
        self.password = password
        self.token: Optional[str] = None
        
        logger.info(f"Provisionador inicializado: {self.base_url}")
    
    def login(self) -> bool:
        """
        Hace login en ThingsBoard y obtiene JWT token.
        
        Returns:
            True si login exitoso, False en caso contrario
        """
        try:
            url = f"{self.base_url}/api/auth/login"
            payload = {
                "username": self.username,
                "password": self.password
            }
            
            logger.info(f"Haciendo login en ThingsBoard como {self.username}...")
            
            response = requests.post(url, json=payload, timeout=10)
            
            if response.status_code == 200:
                data = response.json()
                self.token = data.get("token")
                
                if self.token:
                    logger.info("✓ Login exitoso")
                    return True
                else:
                    logger.error("✗ No se recibió token en respuesta")
                    return False
            else:
                logger.error(f"✗ Login fallido: {response.status_code} - {response.text}")
                return False
                
        except requests.exceptions.ConnectionError:
            logger.error(f"✗ No se pudo conectar a {self.base_url}")
            logger.error("  ¿ThingsBoard está corriendo?")
            return False
        except Exception as e:
            logger.error(f"✗ Error en login: {e}")
            return False
    
    def get_headers(self) -> Dict[str, str]:
        """Retorna headers HTTP con autenticación."""
        return {
            "Content-Type": "application/json",
            "X-Authorization": f"Bearer {self.token}"
        }
    
    def device_exists(self, device_name: str) -> Optional[Dict]:
        """
        Verifica si un dispositivo ya existe.
        
        Args:
            device_name: Nombre del dispositivo
            
        Returns:
            Datos del dispositivo si existe, None si no existe
        """
        try:
            url = f"{self.base_url}/api/tenant/devices"
            params = {"deviceName": device_name}
            
            response = requests.get(
                url,
                headers=self.get_headers(),
                params=params,
                timeout=10
            )
            
            if response.status_code == 200:
                data = response.json()
                if data:
                    logger.info(f"✓ Dispositivo '{device_name}' ya existe")
                    return data
            
            return None
            
        except Exception as e:
            logger.error(f"Error verificando dispositivo: {e}")
            return None
    
    def create_device(
        self,
        device_name: str,
        device_label: str = "DLMS Energy Meter",
        device_type: str = "Energy Meter"
    ) -> Optional[Dict]:
        """
        Crea un nuevo dispositivo en ThingsBoard.
        
        Args:
            device_name: Nombre del dispositivo
            device_label: Etiqueta descriptiva
            device_type: Tipo de dispositivo
            
        Returns:
            Datos del dispositivo creado o None si falla
        """
        try:
            url = f"{self.base_url}/api/device"
            
            payload = {
                "name": device_name,
                "label": device_label,
                "type": device_type
            }
            
            logger.info(f"Creando dispositivo '{device_name}'...")
            
            response = requests.post(
                url,
                headers=self.get_headers(),
                json=payload,
                timeout=10
            )
            
            if response.status_code == 200:
                device = response.json()
                logger.info(f"✓ Dispositivo creado: {device['id']['id']}")
                return device
            else:
                logger.error(f"✗ Error creando dispositivo: {response.status_code}")
                logger.error(f"  Respuesta: {response.text}")
                return None
                
        except Exception as e:
            logger.error(f"✗ Error creando dispositivo: {e}")
            return None
    
    def get_device_credentials(self, device_id: str) -> Optional[str]:
        """
        Obtiene el access token del dispositivo.
        
        Args:
            device_id: ID del dispositivo
            
        Returns:
            Access token o None si falla
        """
        try:
            url = f"{self.base_url}/api/device/{device_id}/credentials"
            
            response = requests.get(
                url,
                headers=self.get_headers(),
                timeout=10
            )
            
            if response.status_code == 200:
                credentials = response.json()
                access_token = credentials.get("credentialsId")
                
                if access_token:
                    logger.info(f"✓ Access token obtenido: {access_token[:8]}...{access_token[-4:]}")
                    return access_token
                else:
                    logger.error("✗ No se encontró access token en credenciales")
                    return None
            else:
                logger.error(f"✗ Error obteniendo credenciales: {response.status_code}")
                return None
                
        except Exception as e:
            logger.error(f"✗ Error obteniendo credenciales: {e}")
            return None
    
    def provision_device(self, device_name: str) -> Optional[str]:
        """
        Aprovisiona un dispositivo completo (crea o usa existente).
        
        Args:
            device_name: Nombre del dispositivo
            
        Returns:
            Access token del dispositivo o None si falla
        """
        logger.info("\n" + "="*70)
        logger.info("PROVISIONING AUTOMÁTICO DE DISPOSITIVO")
        logger.info("="*70 + "\n")
        
        # 1. Login
        if not self.login():
            return None
        
        # 2. Verificar si ya existe
        existing_device = self.device_exists(device_name)
        
        if existing_device:
            logger.info(f"✓ Usando dispositivo existente: {device_name}")
            device_id = existing_device["id"]["id"]
        else:
            # 3. Crear dispositivo nuevo
            device = self.create_device(device_name)
            
            if not device:
                return None
            
            device_id = device["id"]["id"]
        
        # 4. Obtener access token
        access_token = self.get_device_credentials(device_id)
        
        if access_token:
            logger.info("\n" + "="*70)
            logger.info("✓ PROVISIONING COMPLETADO EXITOSAMENTE")
            logger.info("="*70)
            logger.info(f"\nDispositivo: {device_name}")
            logger.info(f"Device ID:   {device_id}")
            logger.info(f"Token:       {access_token[:12]}...{access_token[-8:]}")
            logger.info("\n" + "="*70 + "\n")
            
            return access_token
        else:
            return None


def update_config_file(access_token: str, config_file: str = "mqtt_config.json"):
    """
    Actualiza el archivo de configuración con el nuevo access token.
    
    Args:
        access_token: Token a guardar
        config_file: Path del archivo de configuración
    """
    try:
        # Leer configuración actual
        with open(config_file, 'r') as f:
            config = json.load(f)
        
        # Actualizar token
        config["access_token"] = access_token
        
        # Guardar
        with open(config_file, 'w') as f:
            json.dump(config, f, indent=2)
        
        logger.info(f"✓ Configuración actualizada en {config_file}")
        return True
        
    except Exception as e:
        logger.error(f"✗ Error actualizando configuración: {e}")
        return False


def main():
    """Función principal."""
    import argparse
    
    parser = argparse.ArgumentParser(
        description="Provisioning automático de dispositivo DLMS en ThingsBoard"
    )
    
    parser.add_argument(
        "--host",
        type=str,
        default="localhost",
        help="Host de ThingsBoard (default: localhost)"
    )
    
    parser.add_argument(
        "--port",
        type=int,
        default=8080,
        help="Puerto HTTP de ThingsBoard (default: 8080)"
    )
    
    parser.add_argument(
        "--username",
        type=str,
        default="tenant@thingsboard.org",
        help="Usuario de ThingsBoard (default: tenant@thingsboard.org)"
    )
    
    parser.add_argument(
        "--password",
        type=str,
        default="tenant",
        help="Contraseña de ThingsBoard (default: tenant)"
    )
    
    parser.add_argument(
        "--device-name",
        type=str,
        default="medidor_dlms_principal",
        help="Nombre del dispositivo (default: medidor_dlms_principal)"
    )
    
    parser.add_argument(
        "--config",
        type=str,
        default="mqtt_config.json",
        help="Archivo de configuración a actualizar (default: mqtt_config.json)"
    )
    
    parser.add_argument(
        "--no-update-config",
        action="store_true",
        help="No actualizar el archivo de configuración"
    )
    
    args = parser.parse_args()
    
    # Crear provisionador
    provisioner = ThingsBoardProvisioner(
        host=args.host,
        port=args.port,
        username=args.username,
        password=args.password
    )
    
    # Provisionar dispositivo
    access_token = provisioner.provision_device(args.device_name)
    
    if not access_token:
        logger.error("\n✗ Provisioning falló")
        sys.exit(1)
    
    # Actualizar configuración si no se deshabilitó
    if not args.no_update_config:
        logger.info(f"\nActualizando {args.config}...")
        if update_config_file(access_token, args.config):
            logger.info("\n✓ Sistema listo para usar!")
            logger.info(f"\nPróximo paso: ./start_mqtt_polling.sh")
        else:
            logger.warning("\n⚠ Token obtenido pero no se pudo actualizar config")
            logger.info(f"\nToken: {access_token}")
            logger.info("Copia este token manualmente a mqtt_config.json")
    else:
        logger.info(f"\nAccess Token: {access_token}")
        logger.info("Copia este token a tu archivo de configuración")
    
    sys.exit(0)


if __name__ == "__main__":
    main()
