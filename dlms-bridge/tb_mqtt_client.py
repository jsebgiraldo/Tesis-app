"""
ThingsBoard MQTT Client Wrapper - Optimized for Worker Pattern
Provides robust MQTT connection with QoS and persistent connections
Uses paho-mqtt with ThingsBoard best practices
"""
import logging
import time
import json
from typing import Dict, Any, Optional
import paho.mqtt.client as mqtt

logger = logging.getLogger(__name__)


class ThingsBoardMQTTClient:
    """
    Optimized MQTT client for ThingsBoard with persistent connections
    
    Features:
    - Persistent MQTT connection (no reconnection loops)
    - QoS=1 by default (guaranteed delivery)
    - Proper keepalive and clean_session handling
    - Simple publish interface for telemetry
    """
    
    def __init__(self, host: str, port: int, token: str, client_id: Optional[str] = None):
        """
        Initialize ThingsBoard MQTT client
        
        Args:
            host: ThingsBoard server host
            port: MQTT port (default 1883)
            token: Device access token
            client_id: Optional unique client ID
        """
        self.host = host
        self.port = port
        self.token = token
        self.client_id = client_id or f"dlms_client_{int(time.time())}"
        
        # Create paho-mqtt client with clean session
        self.client = mqtt.Client(
            client_id=self.client_id,
            clean_session=True,  # Don't persist session
            protocol=mqtt.MQTTv311
        )
        
        # Set credentials
        self.client.username_pw_set(self.token)
        
        # Configure callbacks
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_publish = self._on_publish
        
        self._connected = False
        self._connection_errors = 0
        
        logger.info(f"🔧 ThingsBoard MQTT client initialized: {self.client_id}")
    
    def _on_connect(self, client, userdata, flags, rc):
        """Callback when connected"""
        if rc == 0:
            self._connected = True
            self._connection_errors = 0
            logger.info(f"✅ MQTT Connected: {self.client_id}")
        else:
            self._connected = False
            error_msgs = {
                1: "Incorrect protocol version",
                2: "Invalid client identifier",
                3: "Server unavailable",
                4: "Bad username or password",
                5: "Not authorized"
            }
            logger.error(f"❌ MQTT Connection failed: code {rc} - {error_msgs.get(rc, 'Unknown')}")
            self._connection_errors += 1
    
    def _on_disconnect(self, client, userdata, rc):
        """Callback when disconnected"""
        self._connected = False
        if rc != 0:
            logger.warning(f"⚠️ MQTT Disconnected unexpectedly: code {rc}")
        else:
            logger.debug(f"ℹ️ MQTT Disconnected gracefully")
    
    def _on_publish(self, client, userdata, mid):
        """Callback when message is published"""
        logger.debug(f"📤 Message {mid} acknowledged")
    
    def connect(self, timeout: int = 10, keepalive: int = 60) -> bool:
        """
        Connect to ThingsBoard
        
        Args:
            timeout: Connection timeout in seconds
            keepalive: Keepalive interval in seconds (default 60s)
            
        Returns:
            True if connected successfully
        """
        try:
            logger.info(f"🔌 Connecting to ThingsBoard at {self.host}:{self.port}...")
            
            # Connect to broker
            self.client.connect(self.host, self.port, keepalive=keepalive)
            
            # Start network loop in background thread
            self.client.loop_start()
            
            # Wait for connection
            start_time = time.time()
            while not self._connected and (time.time() - start_time) < timeout:
                time.sleep(0.1)
            
            if self._connected:
                logger.info(f"✅ Connected successfully: {self.client_id}")
                return True
            else:
                logger.error(f"❌ Connection timeout after {timeout}s")
                return False
                
        except Exception as e:
            logger.error(f"❌ Connection failed: {e}")
            return False
    
    def disconnect(self) -> bool:
        """
        Gracefully disconnect from ThingsBoard
        
        Returns:
            True if disconnected successfully
        """
        try:
            if self._connected:
                logger.info(f"🔌 Disconnecting: {self.client_id}")
                self.client.loop_stop()
                self.client.disconnect()
                self._connected = False
                logger.info("✅ Disconnected successfully")
                return True
            else:
                logger.debug("Already disconnected")
                return True
        except Exception as e:
            logger.error(f"❌ Disconnect error: {e}")
            return False
    
    def is_connected(self) -> bool:
        """Check if client is connected"""
        return self._connected and self.client.is_connected()
    
    def publish_telemetry(self, data: Dict[str, Any], wait: bool = False) -> bool:
        """
        Publish telemetry data with QoS=1 guarantee
        
        Args:
            data: Telemetry data dictionary
            wait: Wait for publish confirmation (not implemented in paho-mqtt easily)
            
        Returns:
            True if published successfully
        """
        if not self.is_connected():
            logger.warning("⚠️ Not connected, cannot publish telemetry")
            return False
        
        try:
            # Format telemetry for ThingsBoard
            # If data has 'ts' key, it's already formatted
            if "ts" in data:
                payload = data
            else:
                # Add timestamp
                payload = {
                    "ts": int(time.time() * 1000),
                    "values": data
                }
            
            # Convert to JSON
            payload_json = json.dumps(payload)
            
            # Publish with QoS=1
            topic = "v1/devices/me/telemetry"
            result = self.client.publish(topic, payload_json, qos=1)
            
            if result.rc == mqtt.MQTT_ERR_SUCCESS:
                logger.debug(f"📤 Published: {len(payload_json)} bytes")
                return True
            else:
                logger.warning(f"⚠️ Publish failed: rc={result.rc}")
                return False
                
        except Exception as e:
            logger.error(f"❌ Publish error: {e}")
            return False
    
    def publish_attributes(self, attributes: Dict[str, Any], wait: bool = False) -> bool:
        """
        Publish device attributes
        
        Args:
            attributes: Attributes dictionary
            wait: Wait for publish confirmation
            
        Returns:
            True if published successfully
        """
        if not self.is_connected():
            logger.warning("⚠️ Not connected, cannot publish attributes")
            return False
        
        try:
            # Convert to JSON
            payload_json = json.dumps(attributes)
            
            # Publish with QoS=1
            topic = "v1/devices/me/attributes"
            result = self.client.publish(topic, payload_json, qos=1)
            
            if result.rc == mqtt.MQTT_ERR_SUCCESS:
                logger.debug(f"📋 Attributes published: {attributes}")
                return True
            else:
                logger.warning(f"⚠️ Attributes publish failed: rc={result.rc}")
                return False
                
        except Exception as e:
            logger.error(f"❌ Publish attributes error: {e}")
            return False
    
    def stop(self):
        """Stop the client and cleanup resources"""
        try:
            logger.info(f"🛑 Stopping ThingsBoard client: {self.client_id}")
            self.disconnect()
            logger.info("✅ Client stopped successfully")
        except Exception as e:
            logger.error(f"❌ Stop error: {e}")
    
    def get_stats(self) -> Dict[str, Any]:
        """Get connection statistics"""
        return {
            "client_id": self.client_id,
            "connected": self.is_connected(),
            "connection_errors": self._connection_errors,
            "host": self.host,
            "port": self.port
        }


if __name__ == "__main__":
    # Example usage
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
    )
    
    # Test connection
    client = ThingsBoardMQTTClient(
        host="localhost",
        port=1883,
        token="TEST_TOKEN"
    )
    
    if client.connect():
        # Publish test telemetry
        client.publish_telemetry({
            "temperature": 25.5,
            "voltage": 220.0
        })
        
        # Publish attributes
        client.publish_attributes({
            "model": "DLMS Meter",
            "version": "2.0"
        })
        
        time.sleep(2)
        client.stop()
