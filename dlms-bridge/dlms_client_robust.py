#!/usr/bin/env python3
"""
Robust DLMS Client Wrapper
Wraps dlms_reader.DLMSClient with auto-recovery, state management, and retry logic.
Created to maintain compatibility with dlms_poller_production.py
"""

import logging
import time
from enum import Enum
from dataclasses import dataclass
from typing import Optional, Dict, Any
from dlms_reader import DLMSClient

# Configure logger
logger = logging.getLogger('dlms_client_robust')


class ConnectionState(Enum):
    """Connection state enumeration"""
    DISCONNECTED = "disconnected"
    CONNECTING = "connecting"
    CONNECTED = "connected"
    ERROR = "error"


@dataclass
class DLMSConfig:
    """DLMS Configuration"""
    host: str
    port: int
    server_logical: int = 1
    server_physical: int = 0
    client_sap: int = 0x10
    max_info_length: Optional[int] = 128
    password: bytes = b"00000000"
    timeout: float = 7.0  # Aumentado de 5.0 a 7.0 para reducir timeouts
    max_retries: int = 3
    retry_delay: float = 2.0
    verbose: bool = False
    
    # Additional configuration parameters for compatibility
    reconnect_threshold: int = 15
    circuit_breaker_threshold: int = 15
    circuit_breaker_timeout: float = 30.0
    heartbeat_interval: float = 15.0
    buffer_clear_on_error: bool = True


class RobustDLMSClient:
    """
    Robust wrapper around DLMSClient with auto-recovery and state management.
    
    Features:
    - Automatic reconnection on failures
    - Connection state tracking
    - Retry logic with exponential backoff
    - Error handling and logging
    """
    
    def __init__(self, config: DLMSConfig):
        """
        Initialize robust DLMS client.
        
        Args:
            config: DLMSConfig instance with connection parameters
        """
        self.config = config
        self._client: Optional[DLMSClient] = None
        self._state = ConnectionState.DISCONNECTED
        self._last_error: Optional[str] = None
        self._retry_count = 0
        
        logger.info(f"RobustDLMSClient initialized for {config.host}:{config.port}")
    
    @property
    def state(self) -> ConnectionState:
        """Get current connection state"""
        return self._state
    
    @property
    def is_connected(self) -> bool:
        """Check if client is connected"""
        return self._state == ConnectionState.CONNECTED
    
    @property
    def last_error(self) -> Optional[str]:
        """Get last error message"""
        return self._last_error
    
    def connect(self, force_reconnect: bool = False) -> bool:
        """
        Connect to DLMS meter with retry logic.
        
        Args:
            force_reconnect: If True, disconnect and reconnect even if already connected
            
        Returns:
            True if connection successful, False otherwise
        """
        if self.is_connected and not force_reconnect:
            logger.debug("Already connected")
            return True
        
        if force_reconnect and self._client:
            logger.info("Forcing reconnection...")
            self.disconnect()
        
        self._state = ConnectionState.CONNECTING
        self._retry_count = 0
        
        while self._retry_count < self.config.max_retries:
            try:
                logger.info(f"🔌 Connecting to DLMS meter (attempt {self._retry_count + 1}/{self.config.max_retries})...")
                
                # Create new client instance
                self._client = DLMSClient(
                    host=self.config.host,
                    port=self.config.port,
                    server_logical=self.config.server_logical,
                    server_physical=self.config.server_physical,
                    client_sap=self.config.client_sap,
                    max_info_length=self.config.max_info_length,
                    password=self.config.password,
                    verbose=self.config.verbose,
                    timeout=self.config.timeout
                )
                
                # Attempt connection
                self._client.connect()
                
                # Success
                self._state = ConnectionState.CONNECTED
                self._last_error = None
                self._retry_count = 0
                logger.info("✅ Connected to DLMS meter successfully")
                return True
                
            except Exception as e:
                self._retry_count += 1
                self._last_error = str(e)
                logger.warning(f"⚠️ Connection attempt {self._retry_count} failed: {e}")
                
                if self._retry_count < self.config.max_retries:
                    delay = self.config.retry_delay * self._retry_count  # Exponential backoff
                    logger.info(f"Retrying in {delay}s...")
                    time.sleep(delay)
                else:
                    logger.error(f"❌ Failed to connect after {self.config.max_retries} attempts")
                    self._state = ConnectionState.ERROR
                    self._client = None
                    return False
        
        return False
    
    def disconnect(self) -> None:
        """Disconnect from DLMS meter"""
        if self._client:
            try:
                self._client.disconnect()
                logger.info("Disconnected from DLMS meter")
            except Exception as e:
                logger.warning(f"Error during disconnect: {e}")
            finally:
                self._client = None
                self._state = ConnectionState.DISCONNECTED
    
    def read_register(self, obis_code: str, retries: int = 2) -> Optional[Any]:
        """
        Read a register with automatic retry on failure.
        
        Args:
            obis_code: OBIS code to read (e.g., "1-1:32.7.0")
            retries: Number of retries on failure
            
        Returns:
            Register value or None on failure
        """
        if not self.is_connected:
            logger.warning("Not connected. Attempting to connect...")
            if not self.connect():
                return None
        
        attempt = 0
        while attempt <= retries:
            try:
                if not self._client:
                    logger.error("Client is None, cannot read register")
                    return None
                
                # Use the DLMSClient's read_register method
                value = self._client.read_register(obis_code)
                return value
                
            except Exception as e:
                attempt += 1
                logger.warning(f"Read failed (attempt {attempt}/{retries + 1}): {e}")
                
                if attempt <= retries:
                    # Try to recover connection
                    logger.info("Attempting to recover connection...")
                    if not self.connect(force_reconnect=True):
                        logger.error("Failed to recover connection")
                        return None
                else:
                    logger.error(f"Failed to read {obis_code} after {retries + 1} attempts")
                    self._last_error = str(e)
                    return None
        
        return None
    
    def read_multiple_registers(self, obis_codes: list) -> Dict[str, Any]:
        """
        Read multiple registers.
        
        Args:
            obis_codes: List of OBIS codes to read
            
        Returns:
            Dictionary mapping OBIS codes to their values
        """
        results = {}
        
        for obis in obis_codes:
            value = self.read_register(obis)
            if value is not None:
                results[obis] = value
            else:
                logger.warning(f"Failed to read {obis}")
        
        return results
    
    def __enter__(self):
        """Context manager entry"""
        self.connect()
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit"""
        self.disconnect()
        return False
    
    def __del__(self):
        """Cleanup on deletion"""
        if self._client:
            try:
                self.disconnect()
            except:
                pass


# Convenience function for backwards compatibility
def create_robust_client(host: str, port: int, **kwargs) -> RobustDLMSClient:
    """
    Create a RobustDLMSClient with default configuration.
    
    Args:
        host: DLMS meter IP address
        port: DLMS meter port
        **kwargs: Additional configuration parameters
        
    Returns:
        Configured RobustDLMSClient instance
    """
    config = DLMSConfig(host=host, port=port, **kwargs)
    return RobustDLMSClient(config)


if __name__ == "__main__":
    # Example usage
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - [%(levelname)s] - %(message)s'
    )
    
    # Create client
    config = DLMSConfig(
        host="192.168.1.127",
        port=3333,
        timeout=5.0,
        verbose=True
    )
    
    client = RobustDLMSClient(config)
    
    # Test connection
    if client.connect():
        print(f"✅ Connected! State: {client.state}")
        
        # Test reading
        voltage = client.read_register("1-1:32.7.0")
        print(f"Voltage: {voltage}")
        
        client.disconnect()
    else:
        print(f"❌ Connection failed: {client.last_error}")
