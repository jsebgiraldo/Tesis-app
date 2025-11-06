#!/usr/bin/env python3
"""
ThingsBoard Device Provisioning usando Device Credentials (key/secret).

Este script provisiona un dispositivo en ThingsBoard usando las credenciales
de provisioning y luego inicia el bridge DLMS→MQTT automáticamente.
"""

import json
import sys
import requests
import logging
from typing import Optional, Dict
import time

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class DeviceProvisioner:
    """Provisionador de dispositivos con credenciales key/secret."""
    
    def __init__(
        self,
        host: str = "localhost",
        port: int = 8080,
        provision_key: str = "",
        provision_secret: str = ""
    ):
        """
        Inicializa el provisionador.
        
        Args:
            host: Host de ThingsBoard
            port: Puerto HTTP de ThingsBoard
            provision_key: Device provision key
            provision_secret: Device provision secret
        """
        self.base_url = f"http://{host}:{port}"
        self.provision_key = provision_key
        self.provision_secret = provision_secret
        
        logger.info(f"Provisionador inicializado: {self.base_url}")
        logger.info(f"Provision Key: {provision_key[:8]}...{provision_key[-4:]}")
    
    def provision_device(
        self,
        device_name: str,
        device_type: str = "Energy Meter"
    ) -> Optional[Dict]:
        """
        Provisiona un dispositivo usando credenciales.
        
        Args:
            device_name: Nombre del dispositivo
            device_type: Tipo de dispositivo
            
        Returns:
            Datos del dispositivo provisionado con access token
        """
        try:
            url = f"{self.base_url}/api/v1/provision"
            
            payload = {
                "deviceName": device_name,
                "provisionDeviceKey": self.provision_key,
                "provisionDeviceSecret": self.provision_secret
            }
            
            logger.info(f"\nProvisionando dispositivo '{device_name}'...")
            logger.info(f"Endpoint: {url}")
            
            response = requests.post(
                url,
                json=payload,
                timeout=10,
                headers={"Content-Type": "application/json"}
            )
            
            logger.info(f"Status Code: {response.status_code}")
            
            if response.status_code == 200:
                data = response.json()
                logger.info("✓ Dispositivo provisionado exitosamente!")
                
                # ThingsBoard retorna diferentes estructuras según configuración
                if "credentialsValue" in data:
                    access_token = data["credentialsValue"]
                elif "accessToken" in data:
                    access_token = data["accessToken"]
                elif "token" in data:
                    access_token = data["token"]
                else:
                    # Mostrar toda la respuesta para debugging
                    logger.info(f"Respuesta completa: {json.dumps(data, indent=2)}")
                    
                    # Buscar token en cualquier campo
                    for key, value in data.items():
                        if "token" in key.lower() or "credential" in key.lower():
                            if isinstance(value, str) and len(value) > 10:
                                access_token = value
                                break
                    else:
                        logger.error("No se encontró access token en la respuesta")
                        logger.error(f"Respuesta: {data}")
                        return None
                
                result = {
                    "device_name": device_name,
                    "access_token": access_token,
                    "provision_data": data
                }
                
                logger.info(f"\n{'='*70}")
                logger.info("✓ PROVISIONING COMPLETADO")
                logger.info(f"{'='*70}")
                logger.info(f"Dispositivo:   {device_name}")
                logger.info(f"Access Token:  {access_token[:12]}...{access_token[-8:]}")
                logger.info(f"{'='*70}\n")
                
                return result
                
            elif response.status_code == 400:
                logger.error(f"✗ Error 400: Credenciales inválidas o dispositivo ya existe")
                logger.error(f"Respuesta: {response.text}")
                
                # Intentar parsear respuesta de error
                try:
                    error_data = response.json()
                    if "message" in error_data:
                        logger.error(f"Mensaje: {error_data['message']}")
                except:
                    pass
                
                return None
                
            elif response.status_code == 404:
                logger.error(f"✗ Error 404: Endpoint no encontrado")
                logger.error(f"Verifica que ThingsBoard esté configurado correctamente")
                return None
                
            else:
                logger.error(f"✗ Error provisionando: {response.status_code}")
                logger.error(f"Respuesta: {response.text}")
                return None
                
        except requests.exceptions.ConnectionError:
            logger.error(f"✗ No se pudo conectar a {self.base_url}")
            logger.error("  ¿ThingsBoard está corriendo?")
            return None
        except Exception as e:
            logger.error(f"✗ Error en provisioning: {e}")
            import traceback
            traceback.print_exc()
            return None


def update_mqtt_config(
    access_token: str,
    device_name: str,
    mqtt_host: str = "localhost",
    config_file: str = "mqtt_config.json"
) -> bool:
    """
    Actualiza el archivo de configuración MQTT.
    
    Args:
        access_token: Token del dispositivo
        device_name: Nombre del dispositivo
        mqtt_host: Host MQTT
        config_file: Path del archivo de configuración
        
    Returns:
        True si se actualizó exitosamente
    """
    try:
        # Leer configuración actual
        with open(config_file, 'r') as f:
            config = json.load(f)
        
        # Actualizar valores
        config["access_token"] = access_token
        config["device_name"] = device_name
        config["mqtt_host"] = mqtt_host
        
        # Guardar
        with open(config_file, 'w') as f:
            json.dump(config, f, indent=2)
        
        logger.info(f"✓ Configuración actualizada en {config_file}")
        return True
        
    except Exception as e:
        logger.error(f"✗ Error actualizando configuración: {e}")
        return False


def verify_thingsboard_running(host: str, port: int) -> bool:
    """Verifica que ThingsBoard esté corriendo."""
    try:
        url = f"http://{host}:{port}"
        response = requests.get(url, timeout=5)
        
        if response.status_code in [200, 302, 301]:
            logger.info(f"✓ ThingsBoard está corriendo en {url}")
            return True
        else:
            logger.warning(f"⚠ ThingsBoard respondió con código {response.status_code}")
            return True  # Puede estar corriendo pero retornar otro código
            
    except requests.exceptions.ConnectionError:
        logger.error(f"✗ No se puede conectar a ThingsBoard en http://{host}:{port}")
        return False
    except Exception as e:
        logger.error(f"✗ Error verificando ThingsBoard: {e}")
        return False


def main():
    """Función principal."""
    import argparse
    
    parser = argparse.ArgumentParser(
        description="Provisioning de dispositivo DLMS con credenciales key/secret"
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
        "--key",
        type=str,
        required=True,
        help="Device provision key"
    )
    
    parser.add_argument(
        "--secret",
        type=str,
        required=True,
        help="Device provision secret"
    )
    
    parser.add_argument(
        "--device-name",
        type=str,
        default="medidor_dlms_principal",
        help="Nombre del dispositivo (default: medidor_dlms_principal)"
    )
    
    parser.add_argument(
        "--mqtt-host",
        type=str,
        default="localhost",
        help="Host MQTT (default: localhost)"
    )
    
    parser.add_argument(
        "--config",
        type=str,
        default="mqtt_config.json",
        help="Archivo de configuración (default: mqtt_config.json)"
    )
    
    args = parser.parse_args()
    
    print("\n" + "="*70)
    print("   DLMS to ThingsBoard - Device Provisioning")
    print("="*70 + "\n")
    
    # Verificar ThingsBoard
    logger.info("Verificando ThingsBoard...")
    if not verify_thingsboard_running(args.host, args.port):
        logger.error("\n✗ ThingsBoard no está accesible")
        logger.error("  Verifica que esté corriendo:")
        logger.error("    sudo systemctl status thingsboard")
        sys.exit(1)
    
    print()
    
    # Provisionar dispositivo
    provisioner = DeviceProvisioner(
        host=args.host,
        port=args.port,
        provision_key=args.key,
        provision_secret=args.secret
    )
    
    result = provisioner.provision_device(args.device_name)
    
    if not result:
        logger.error("\n✗ Provisioning falló")
        logger.error("\nPosibles causas:")
        logger.error("  • Credenciales key/secret incorrectas")
        logger.error("  • Dispositivo ya existe con ese nombre")
        logger.error("  • Provisioning no habilitado en ThingsBoard")
        sys.exit(1)
    
    # Actualizar configuración
    logger.info(f"\nActualizando {args.config}...")
    if not update_mqtt_config(
        result["access_token"],
        result["device_name"],
        args.mqtt_host,
        args.config
    ):
        logger.warning("\n⚠ No se pudo actualizar configuración automáticamente")
        logger.info(f"\nAccess Token: {result['access_token']}")
        logger.info("Actualiza manualmente mqtt_config.json con este token")
        sys.exit(1)
    
    print("\n" + "="*70)
    print("✓ SISTEMA LISTO PARA USAR")
    print("="*70)
    print(f"\nDispositivo:      {result['device_name']}")
    print(f"Access Token:     {result['access_token'][:12]}...{result['access_token'][-8:]}")
    print(f"MQTT Host:        {args.mqtt_host}")
    print(f"\nPróximo paso:")
    print(f"  python3 dlms_mqtt_bridge.py --config {args.config}")
    print(f"\nO usar script completo:")
    print(f"  ./start_mqtt_polling.sh")
    print("\n" + "="*70 + "\n")
    
    sys.exit(0)


if __name__ == "__main__":
    main()
