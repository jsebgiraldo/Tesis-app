#!/usr/bin/env python3
"""
ThingsBoard Gateway - Custom DLMS Connector
===========================================
Conector personalizado para integrar medidores DLMS/COSEM con ThingsBoard Gateway.

Este conector implementa la interfaz de ThingsBoard Gateway y reutiliza nuestro
ProductionDLMSPoller existente para mantener la robustez probada.

Características:
- Compatible con ThingsBoard Gateway API
- Polling multi-meter asíncrono
- Auto-discovery de dispositivos DLMS en red
- Mapeo flexible de OBIS codes a telemetría
- Watchdog y reconexión automática
"""

import asyncio
import logging
from typing import Dict, List, Optional, Any
from threading import Thread
from time import time, sleep
import sys
from pathlib import Path

# Add project root to path
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from dlms_poller_production import ProductionDLMSPoller

try:
    from thingsboard_gateway.connectors.connector import Connector
except ImportError:
    # Fallback for development without TB Gateway installed
    logging.warning("ThingsBoard Gateway not installed, using mock Connector")
    class Connector:
        def __init__(self, gateway, config, connector_type):
            self._gateway = gateway
            self._config = config
            self._connector_type = connector_type
            self._log = logging.getLogger(self.__class__.__name__)
        
        def open(self):
            pass
        
        def close(self):
            pass
        
        def get_name(self):
            return self._config.get('name', 'Unknown')


log = logging.getLogger(__name__)


class DLMSDevice:
    """Representa un medidor DLMS conectado"""
    
    def __init__(self, name: str, host: str, port: int, measurements: List[str], 
                 interval: float = 5.0, device_type: str = "DLMS_Meter"):
        self.name = name
        self.host = host
        self.port = port
        self.measurements = measurements
        self.interval = interval
        self.device_type = device_type
        
        self.poller: Optional[ProductionDLMSPoller] = None
        self.last_poll_time = 0
        self.total_polls = 0
        self.successful_polls = 0
        
        self._log = logging.getLogger(f'DLMSDevice[{name}]')
    
    def create_poller(self) -> bool:
        """Crear poller DLMS"""
        try:
            self.poller = ProductionDLMSPoller(
                host=self.host,
                port=self.port,
                measurements=self.measurements,
                interval=self.interval,
                verbose=False
            )
            self._log.info(f"✓ Poller created for {self.host}:{self.port}")
            return True
        except Exception as e:
            self._log.error(f"Failed to create poller: {e}")
            return False
    
    def connect(self) -> bool:
        """Conectar al medidor"""
        if not self.poller:
            if not self.create_poller():
                return False
        
        try:
            connected = self.poller._connect_with_recovery()
            if connected:
                self._log.info(f"✅ Connected to {self.host}:{self.port}")
                return True
            else:
                self._log.error(f"Failed to connect to {self.host}:{self.port}")
                return False
        except Exception as e:
            self._log.error(f"Connection error: {e}")
            return False
    
    def poll(self) -> Optional[Dict[str, Any]]:
        """Realizar lectura DLMS"""
        if not self.poller:
            self._log.warning("Poller not initialized")
            return None
        
        try:
            self.total_polls += 1
            readings = self.poller.poll_once()
            
            if readings and any(v is not None for v in readings.values()):
                self.successful_polls += 1
                self.last_poll_time = time()
                return readings
            else:
                self._log.warning("No readings returned")
                return None
        
        except Exception as e:
            self._log.error(f"Poll error: {e}")
            return None
    
    def disconnect(self):
        """Desconectar del medidor"""
        if self.poller:
            try:
                self.poller.stop()
                self._log.info("✓ Disconnected")
            except Exception as e:
                self._log.error(f"Disconnect error: {e}")
    
    def get_stats(self) -> Dict[str, Any]:
        """Obtener estadísticas"""
        success_rate = (self.successful_polls / self.total_polls * 100) if self.total_polls > 0 else 0
        return {
            'name': self.name,
            'host': self.host,
            'port': self.port,
            'total_polls': self.total_polls,
            'successful_polls': self.successful_polls,
            'success_rate': success_rate,
            'last_poll_time': self.last_poll_time
        }


class DLMSConnector(Connector):
    """
    ThingsBoard Gateway Connector for DLMS/COSEM devices
    
    Implementa la interfaz de TB Gateway para gestionar múltiples medidores DLMS.
    """
    
    def __init__(self, gateway, config: Dict[str, Any], connector_type: str):
        super().__init__(gateway, config, connector_type)
        
        self._log = logging.getLogger(f'{self.__class__.__name__}')
        self._log.info("=" * 60)
        self._log.info("Initializing DLMS Connector for ThingsBoard Gateway")
        self._log.info("=" * 60)
        
        # Configuration
        self._connector_type = connector_type
        self.name = config.get('name', 'DLMS Connector')
        
        # Devices
        self.devices: Dict[str, DLMSDevice] = {}
        
        # Threading
        self._stopped = False
        self._poll_thread: Optional[Thread] = None
        
        # Load devices from config
        self._load_devices(config.get('devices', []))
        
        self._log.info(f"✓ Loaded {len(self.devices)} DLMS devices")
    
    def _load_devices(self, devices_config: List[Dict[str, Any]]):
        """Cargar configuración de dispositivos"""
        for dev_config in devices_config:
            try:
                name = dev_config['name']
                host = dev_config['host']
                port = dev_config.get('port', 3333)
                measurements = dev_config.get('measurements', [
                    'voltage_l1', 'current_l1', 'active_power', 'frequency'
                ])
                interval = dev_config.get('pollingInterval', 5000) / 1000.0  # ms to seconds
                device_type = dev_config.get('deviceType', 'DLMS_Meter')
                
                device = DLMSDevice(
                    name=name,
                    host=host,
                    port=port,
                    measurements=measurements,
                    interval=interval,
                    device_type=device_type
                )
                
                self.devices[name] = device
                self._log.info(f"  ✓ Device '{name}' @ {host}:{port} ({len(measurements)} measurements)")
            
            except Exception as e:
                self._log.error(f"Failed to load device config: {e}")
    
    def open(self):
        """
        Iniciar el conector (requerido por TB Gateway API)
        """
        self._log.info("🚀 Opening DLMS Connector...")
        
        # Connect all devices
        connected_count = 0
        for device in self.devices.values():
            if device.connect():
                connected_count += 1
                # Send device connection event to TB Gateway
                self._send_device_connected(device)
        
        self._log.info(f"✅ Connected {connected_count}/{len(self.devices)} devices")
        
        # Start polling thread
        self._stopped = False
        self._poll_thread = Thread(target=self._poll_loop, daemon=True, name="DLMS-Poller")
        self._poll_thread.start()
        
        self._log.info("✓ DLMS Connector opened successfully")
    
    def close(self):
        """
        Detener el conector (requerido por TB Gateway API)
        """
        self._log.info("⏹️  Closing DLMS Connector...")
        
        self._stopped = True
        
        # Wait for poll thread
        if self._poll_thread and self._poll_thread.is_alive():
            self._poll_thread.join(timeout=5)
        
        # Disconnect all devices
        for device in self.devices.values():
            device.disconnect()
            # Send device disconnection event to TB Gateway
            self._send_device_disconnected(device)
        
        self._log.info("✓ DLMS Connector closed")
    
    def get_name(self) -> str:
        """Obtener nombre del conector"""
        return self.name
    
    def get_type(self) -> str:
        """Obtener tipo de conector"""
        return self._connector_type
    
    def is_connected(self) -> bool:
        """Verificar si el conector está activo"""
        return not self._stopped and any(
            device.poller is not None for device in self.devices.values()
        )
    
    def _poll_loop(self):
        """
        Loop principal de polling
        Ejecuta en thread separado
        """
        self._log.info("📊 Starting polling loop...")
        
        # Track last poll time per device
        last_polls: Dict[str, float] = {name: 0 for name in self.devices.keys()}
        
        while not self._stopped:
            try:
                current_time = time()
                
                for name, device in self.devices.items():
                    # Check if it's time to poll this device
                    time_since_last = current_time - last_polls[name]
                    
                    if time_since_last >= device.interval:
                        # Poll device
                        readings = device.poll()
                        
                        if readings:
                            # Send telemetry to ThingsBoard via Gateway
                            self._send_telemetry(device, readings)
                        
                        # Update last poll time
                        last_polls[name] = current_time
                
                # Sleep a bit to avoid busy loop
                sleep(0.1)
            
            except Exception as e:
                self._log.error(f"Error in poll loop: {e}", exc_info=True)
                sleep(1)
        
        self._log.info("✓ Polling loop stopped")
    
    def _send_device_connected(self, device: DLMSDevice):
        """Notificar a TB Gateway que un dispositivo se conectó"""
        try:
            # Send device connection event
            self._gateway.add_device(
                device_name=device.name,
                device_type=device.device_type,
                connector=self
            )
            
            # Send initial attributes
            attributes = {
                'host': device.host,
                'port': device.port,
                'measurements': device.measurements,
                'polling_interval': device.interval
            }
            
            self._gateway.send_attributes(
                device_name=device.name,
                attributes=attributes
            )
            
            self._log.info(f"📡 Device '{device.name}' added to ThingsBoard")
        
        except Exception as e:
            self._log.error(f"Failed to send device connected event: {e}")
    
    def _send_device_disconnected(self, device: DLMSDevice):
        """Notificar a TB Gateway que un dispositivo se desconectó"""
        try:
            self._gateway.del_device(device_name=device.name)
            self._log.info(f"📡 Device '{device.name}' removed from ThingsBoard")
        except Exception as e:
            self._log.error(f"Failed to send device disconnected event: {e}")
    
    def _send_telemetry(self, device: DLMSDevice, readings: Dict[str, Any]):
        """Enviar telemetría a ThingsBoard via Gateway"""
        try:
            # Build telemetry dict (exclude timestamp)
            telemetry = {}
            for key, value in readings.items():
                if key != 'timestamp' and value is not None:
                    try:
                        telemetry[key] = float(value)
                    except (ValueError, TypeError):
                        telemetry[key] = str(value)
            
            if not telemetry:
                return
            
            # Send to gateway
            self._gateway.send_telemetry(
                device_name=device.name,
                telemetry=telemetry
            )
            
            self._log.debug(f"📤 Sent telemetry for '{device.name}': {telemetry}")
        
        except Exception as e:
            self._log.error(f"Failed to send telemetry: {e}")
    
    def on_attributes_update(self, content: Dict[str, Any]):
        """
        Manejar actualización de atributos desde ThingsBoard
        (Requerido por TB Gateway API)
        """
        self._log.debug(f"Attributes update: {content}")
        # TODO: Implementar lógica de actualización de configuración dinámica
    
    def server_side_rpc_handler(self, content: Dict[str, Any]):
        """
        Manejar RPC desde ThingsBoard
        (Requerido por TB Gateway API)
        """
        self._log.debug(f"RPC request: {content}")
        # TODO: Implementar comandos RPC (ej: reconnect, change_polling_interval)
        
        method = content.get('data', {}).get('method')
        device_name = content.get('device')
        
        if method == 'getStats':
            device = self.devices.get(device_name)
            if device:
                return device.get_stats()
        
        elif method == 'reconnect':
            device = self.devices.get(device_name)
            if device:
                device.disconnect()
                sleep(1)
                success = device.connect()
                return {'success': success}
        
        return {'error': 'Unknown method'}


# Entry point for ThingsBoard Gateway
def create_connector(gateway, config: Dict[str, Any], connector_type: str):
    """
    Factory function requerida por ThingsBoard Gateway
    """
    return DLMSConnector(gateway, config, connector_type)
