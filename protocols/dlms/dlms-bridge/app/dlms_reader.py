"""DLMS reader module that wraps the existing DLMSClient for async usage."""

import asyncio
import sys
from typing import Dict, Any, List
from pathlib import Path
from decimal import Decimal

# Import the existing DLMSClient from parent directory
parent_dir = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(parent_dir))

from dlms_reader import DLMSClient, obis_to_bytes, MEASUREMENTS


class AsyncDLMSAdapter:
    """
    Async wrapper around the synchronous DLMSClient.
    
    This adapter makes the existing DLMS reader compatible with async/await
    patterns without rewriting the core DLMS logic.
    """
    
    def __init__(
        self,
        host: str,
        port: int,
        timeout: float,
        client_sap: int,
        server_logical: int,
        server_physical: int,
        password: str,
        measurements: List[str],
        verbose: bool = False,
    ):
        """
        Initialize the DLMS adapter.
        
        Args:
            host: Meter IP address
            port: Meter TCP port
            timeout: Socket timeout in seconds
            client_sap: Client Service Access Point
            server_logical: Server logical address (0 for Microstar)
            server_physical: Server physical address
            password: Authentication password
            measurements: List of measurement keys to read (e.g., ["voltage_l1", "current_l1"])
            verbose: Enable frame-level logging
        """
        self.host = host
        self.port = port
        self.timeout = timeout
        self.client_sap = client_sap
        self.server_logical = server_logical
        self.server_physical = server_physical
        self.password = password.encode("ascii", errors="ignore")[:16]
        self.measurements = measurements
        self.verbose = verbose
        
        # Client will be created for each reading session
        self._client: DLMSClient | None = None
    
    def _read_measurements_sync(self) -> Dict[str, Any]:
        """
        Synchronous method that performs the actual DLMS reading.
        
        Returns:
            Dictionary with timestamp and measurement values
        """
        import time
        
        # Create a new client for this reading session
        client = DLMSClient(
            host=self.host,
            port=self.port,
            server_logical=self.server_logical,
            server_physical=self.server_physical,
            client_sap=self.client_sap,
            max_info_length=None,
            password=self.password,
            verbose=self.verbose,
            timeout=self.timeout,
        )
        
        try:
            # Connect and associate
            client.connect()
            
            # Read all configured measurements
            values = {}
            for measurement_key in self.measurements:
                if measurement_key not in MEASUREMENTS:
                    if self.verbose:
                        print(f"Warning: Unknown measurement '{measurement_key}', skipping")
                    continue
                
                measurement = MEASUREMENTS[measurement_key]
                obis = measurement["obis"]
                
                try:
                    value, unit_code, raw_value = client.read_register(obis)
                    
                    # Store the value as float for JSON serialization
                    values[measurement_key] = {
                        "obis": obis,
                        "value": float(value),
                        "unit_code": unit_code,
                        "raw_value": raw_value,
                        "description": measurement["description"],
                    }
                except Exception as e:
                    if self.verbose:
                        print(f"Error reading {measurement_key} ({obis}): {e}")
                    values[measurement_key] = {
                        "obis": obis,
                        "error": str(e),
                    }
            
            return {
                "ts": int(time.time() * 1000),  # Timestamp in milliseconds
                "values": values,
            }
        
        finally:
            # Always close the connection
            client.close()
    
    async def read_measurements(self) -> Dict[str, Any]:
        """
        Async method to read DLMS measurements.
        
        This runs the synchronous DLMS reading in a thread pool executor
        to avoid blocking the async event loop.
        
        Returns:
            Dictionary with timestamp and measurement values
        """
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(None, self._read_measurements_sync)
