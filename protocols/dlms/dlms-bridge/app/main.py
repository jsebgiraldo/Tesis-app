"""Main entry point for the DLMS-MQTT bridge application."""

import asyncio
import signal
import sys

from .controller import Controller
from .config import settings


async def main():
    """
    Main application entry point.
    
    Creates and starts the controller, then runs until interrupted
    by Ctrl+C or system signal.
    """
    print("=" * 60)
    print("DLMS to MQTT Bridge")
    print("=" * 60)
    print(f"Device ID: {settings.DEVICE_ID}")
    print(f"DLMS Meter: {settings.DLMS_HOST}:{settings.DLMS_PORT}")
    print(f"MQTT Broker: {settings.MQTT_HOST}:{settings.MQTT_PORT}")
    print(f"MQTT Topic: {settings.MQTT_TOPIC.format(device_id=settings.DEVICE_ID)}")
    print(f"Measurements: {', '.join(settings.DLMS_MEASUREMENTS)}")
    print("=" * 60)
    print()
    
    # Create controller
    ctrl = Controller()
    
    # Setup graceful shutdown
    shutdown_event = asyncio.Event()
    
    def signal_handler():
        """Handle shutdown signals."""
        print("\n[main] Shutdown signal received")
        shutdown_event.set()
    
    # Register signal handlers for graceful shutdown
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, signal_handler)
    
    try:
        # Start the controller
        await ctrl.start()
        
        # Wait for shutdown signal
        await shutdown_event.wait()
        
    except KeyboardInterrupt:
        print("\n[main] Keyboard interrupt received")
    
    except Exception as e:
        print(f"[main] Unexpected error: {e}", file=sys.stderr)
        return 1
    
    finally:
        # Stop the controller
        await ctrl.stop()
        print("[main] Goodbye!")
    
    return 0


def run():
    """Entry point wrapper for running the async main function."""
    try:
        exit_code = asyncio.run(main())
        sys.exit(exit_code)
    except KeyboardInterrupt:
        print("\n[main] Interrupted")
        sys.exit(0)


if __name__ == "__main__":
    run()
