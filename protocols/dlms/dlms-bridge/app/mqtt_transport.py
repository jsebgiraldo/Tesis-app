"""MQTT transport layer for publishing telemetry data."""

import json
from aiomqtt import Client
from typing import Any, Dict


class MQTTTransport:
    """
    Async MQTT client wrapper for publishing telemetry data.
    
    Uses context manager pattern for automatic connection/disconnection.
    """
    
    def __init__(
        self,
        host: str,
        port: int,
        client_id: str,
        username: str | None = None,
        password: str | None = None,
        tls: bool = False,
        qos: int = 1,
        retain: bool = False,
    ):
        """
        Initialize MQTT transport.
        
        Args:
            host: MQTT broker hostname
            port: MQTT broker port
            client_id: MQTT client identifier
            username: Optional authentication username
            password: Optional authentication password
            tls: Enable TLS encryption
            qos: Quality of Service level (0, 1, or 2)
            retain: Retain messages on broker
        """
        self.host = host
        self.port = port
        self.client_id = client_id
        self.username = username
        self.password = password
        self.tls = tls
        self.qos = qos
        self.retain = retain
        self._client: Client | None = None
    
    async def __aenter__(self):
        """Enter context manager - establish MQTT connection."""
        # Create client with authentication if provided
        self._client = Client(
            hostname=self.host,
            port=self.port,
            identifier=self.client_id,
            username=self.username,
            password=self.password,
            tls_context=True if self.tls else None,
        )
        
        await self._client.__aenter__()
        return self
    
    async def __aexit__(self, exc_type, exc, tb):
        """Exit context manager - close MQTT connection."""
        if self._client:
            try:
                await self._client.__aexit__(exc_type, exc, tb)
            except Exception as e:
                # Log error but don't raise to avoid masking original exception
                print(f"Error closing MQTT connection: {e}")
    
    async def publish_json(self, topic: str, payload: Dict[str, Any]) -> None:
        """
        Publish a JSON payload to the specified topic.
        
        Args:
            topic: MQTT topic to publish to
            payload: Dictionary to serialize as JSON
            
        Raises:
            RuntimeError: If MQTT client is not connected
        """
        if not self._client:
            raise RuntimeError("MQTT client not connected")
        
        # Serialize payload to JSON
        json_payload = json.dumps(payload, ensure_ascii=False)
        
        # Publish message
        await self._client.publish(
            topic=topic,
            payload=json_payload.encode("utf-8"),
            qos=self.qos,
            retain=self.retain,
        )
