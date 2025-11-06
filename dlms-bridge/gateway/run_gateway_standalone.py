#!/usr/bin/env python3
"""
ThingsBoard Gateway - DLMS Standalone Launcher
Simplified launcher that doesn't require full TB Gateway installation
"""

import sys
import os
from pathlib import Path
import asyncio
import signal
import logging

# Add paths
gateway_dir = Path(__file__).parent
sys.path.insert(0, str(gateway_dir / "connectors"))
sys.path.insert(0, str(gateway_dir.parent))

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - [%(name)s] - %(levelname)s - %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S'
)
logger = logging.getLogger('GatewayLauncher')

# Import our connector
from connectors.dlms_connector import DLMSConnector

# Mock gateway class for standalone operation
class MockGateway:
    """Simple mock of ThingsBoard Gateway for standalone operation"""
    
    def __init__(self, host, port, token):
        self.host = host
        self.port = port
        self.token = token
        self.devices = {}
        
        # Import MQTT client
        from tb_mqtt_client import ThingsBoardMQTTClient
        self.mqtt_client = ThingsBoardMQTTClient(
            host=host,
            port=port,
            token=token
        )
        self.mqtt_client.connect()
        
        logger.info(f"Mock Gateway connected to {host}:{port}")
    
    def add_device(self, device_name, device_type, connector):
        """Register a device with ThingsBoard"""
        logger.info(f"📡 Adding device: {device_name} (type: {device_type})")
        self.devices[device_name] = {
            'type': device_type,
            'connector': connector
        }
        
        # Send device attributes
        self.send_attributes(device_name, {
            'device_type': device_type,
            'connector': 'DLMS'
        })
    
    def del_device(self, device_name):
        """Unregister a device"""
        if device_name in self.devices:
            logger.info(f"📡 Removing device: {device_name}")
            del self.devices[device_name]
    
    def send_telemetry(self, device_name, telemetry):
        """Send telemetry for a device"""
        logger.debug(f"📤 Telemetry for {device_name}: {telemetry}")
        
        # Send via MQTT
        result = self.mqtt_client.publish_telemetry(telemetry, wait=False)
        if result:
            logger.info(f"✅ Telemetry sent for {device_name}: {list(telemetry.keys())}")
        else:
            logger.warning(f"⚠️ Failed to send telemetry for {device_name}")
    
    def send_attributes(self, device_name, attributes):
        """Send attributes for a device"""
        logger.debug(f"📤 Attributes for {device_name}: {attributes}")
        
        # For now, just log (attributes would go through gateway API)
        logger.info(f"📋 Attributes set for {device_name}: {list(attributes.keys())}")
    
    def stop(self):
        """Stop the gateway"""
        if self.mqtt_client:
            self.mqtt_client.stop()
        logger.info("Gateway stopped")


def main():
    import json
    import yaml
    
    # Load configuration
    config_dir = Path(__file__).parent / "config"
    
    # Load gateway config
    with open(config_dir / "tb_gateway.yaml") as f:
        gateway_config = yaml.safe_load(f)
    
    # Load DLMS connector config
    with open(config_dir / "dlms_connector.json") as f:
        dlms_config = json.load(f)
    
    logger.info("=" * 70)
    logger.info("🚀 ThingsBoard Gateway - DLMS Connector (Standalone Mode)")
    logger.info("=" * 70)
    
    # Extract ThingsBoard connection info
    tb_config = gateway_config['thingsboard']
    host = tb_config['host']
    port = tb_config['port']
    token = tb_config['security']['accessToken']
    
    logger.info(f"ThingsBoard: {host}:{port}")
    logger.info(f"Token: {token[:10]}...{token[-4:]}")
    logger.info(f"Devices: {len(dlms_config['devices'])}")
    
    # Create mock gateway
    gateway = MockGateway(host, port, token)
    
    # Create connector
    connector_config = {
        'name': 'DLMS Connector',
        'devices': dlms_config['devices']
    }
    
    connector = DLMSConnector(
        gateway=gateway,
        config=connector_config,
        connector_type='dlms'
    )
    
    # Open connector (this starts polling)
    logger.info("🔌 Opening DLMS Connector...")
    connector.open()
    
    logger.info("=" * 70)
    logger.info("✅ Gateway running! Press Ctrl+C to stop.")
    logger.info("=" * 70)
    
    # Setup signal handlers
    def signal_handler(sig, frame):
        logger.info("🛑 Shutdown signal received...")
        connector.close()
        gateway.stop()
        sys.exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    # Keep running
    try:
        while True:
            import time
            time.sleep(1)
    except KeyboardInterrupt:
        logger.info("🛑 Interrupted by user")
        connector.close()
        gateway.stop()


if __name__ == '__main__':
    main()
