"""Main controller that orchestrates DLMS reading and MQTT publishing."""

import asyncio
import time
from typing import Dict, Any

from .config import settings
from .dlms_reader import AsyncDLMSAdapter
from .mqtt_transport import MQTTTransport


class Controller:
    """
    Main controller for the DLMS-MQTT bridge.
    
    Manages the polling loop that reads from the DLMS meter and publishes
    to MQTT broker with error handling and automatic retry logic.
    """
    
    def __init__(self):
        """Initialize the controller with settings from config."""
        self.reader = AsyncDLMSAdapter(
            host=settings.DLMS_HOST,
            port=settings.DLMS_PORT,
            timeout=settings.DLMS_TIMEOUT_S,
            client_sap=settings.DLMS_CLIENT_SAP,
            server_logical=settings.DLMS_SERVER_LOGICAL,
            server_physical=settings.DLMS_SERVER_PHYSICAL,
            password=settings.DLMS_PASSWORD,
            measurements=settings.DLMS_MEASUREMENTS,
            verbose=settings.VERBOSE,
        )
        self._task: asyncio.Task | None = None
        self._running = asyncio.Event()
    
    async def start(self):
        """Start the controller polling loop."""
        if self._task and not self._task.done():
            print("[controller] Already running")
            return
        
        print("[controller] Starting DLMS-MQTT bridge...")
        self._running.set()
        self._task = asyncio.create_task(self._run())
    
    async def stop(self):
        """Stop the controller polling loop."""
        print("[controller] Stopping...")
        self._running.clear()
        if self._task:
            await asyncio.wait([self._task], timeout=10)
        print("[controller] Stopped")
    
    async def _run(self):
        """
        Main polling loop.
        
        Continuously reads DLMS measurements and publishes to MQTT
        with exponential backoff on errors.
        """
        error_streak = 0
        seq = 0
        
        # Create MQTT transport
        async with MQTTTransport(
            host=settings.MQTT_HOST,
            port=settings.MQTT_PORT,
            client_id=settings.MQTT_CLIENT_ID,
            username=settings.MQTT_USERNAME,
            password=settings.MQTT_PASSWORD,
            tls=settings.MQTT_TLS,
            qos=settings.MQTT_QOS,
            retain=settings.MQTT_RETAIN,
        ) as mqtt:
            
            print(f"[controller] Connected to MQTT broker at {settings.MQTT_HOST}:{settings.MQTT_PORT}")
            print(f"[controller] Reading from DLMS meter at {settings.DLMS_HOST}:{settings.DLMS_PORT}")
            print(f"[controller] Poll period: {settings.POLL_PERIOD_S}s")
            
            while self._running.is_set():
                try:
                    # Read measurements from DLMS meter
                    sample: Dict[str, Any] = await self.reader.read_measurements()
                    
                    # Build MQTT payload
                    payload = {
                        "device_id": settings.DEVICE_ID,
                        "ts": sample.get("ts", int(time.time() * 1000)),
                        "seq": seq,
                        "measurements": sample.get("values", {}),
                    }
                    
                    # Publish to MQTT
                    topic = settings.MQTT_TOPIC.format(device_id=settings.DEVICE_ID)
                    await mqtt.publish_json(topic, payload)
                    
                    # Log success
                    if settings.VERBOSE:
                        print(f"[controller] Published seq={seq} to {topic}")
                    else:
                        # Print a simple status indicator
                        print(f"[controller] Published reading #{seq} at {time.strftime('%H:%M:%S')}")
                    
                    # Reset error counter on success
                    seq += 1
                    error_streak = 0
                    
                except KeyboardInterrupt:
                    # Allow clean shutdown
                    raise
                
                except Exception as e:
                    error_streak += 1
                    print(f"[controller] Error ({error_streak}/{settings.MAX_CONSEC_ERRORS}): {e}")
                    
                    # Check if we've exceeded max consecutive errors
                    if error_streak >= settings.MAX_CONSEC_ERRORS:
                        print("[controller] Too many consecutive errors, stopping...")
                        self._running.clear()
                        break
                    
                    # Exponential backoff (capped at 30 seconds)
                    backoff_time = min(2 ** error_streak, 30)
                    print(f"[controller] Retrying in {backoff_time}s...")
                    await asyncio.sleep(backoff_time)
                    continue
                
                # Wait for next poll cycle
                await asyncio.sleep(settings.POLL_PERIOD_S)
