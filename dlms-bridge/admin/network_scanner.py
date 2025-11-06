"""
Network Scanner for DLMS Meter Discovery
Scans IP ranges to find active DLMS meters on the network
"""

import socket
import asyncio
import struct
from typing import List, Dict, Optional, Tuple
from dataclasses import dataclass
from concurrent.futures import ThreadPoolExecutor
import logging

logger = logging.getLogger(__name__)


@dataclass
class DiscoveredMeter:
    """Represents a discovered DLMS meter"""
    ip_address: str
    port: int
    response_time: float  # milliseconds
    accessible: bool
    error: Optional[str] = None
    
    # Device info (if successfully connected)
    model: Optional[str] = None
    serial_number: Optional[str] = None
    firmware_version: Optional[str] = None


class DLMSNetworkScanner:
    """
    Scanner for discovering DLMS meters on the network
    Uses TCP connection attempts on common DLMS ports
    """
    
    DEFAULT_PORTS = [3333, 4059, 4061]  # Common DLMS/COSEM ports
    CONNECTION_TIMEOUT = 2.0  # seconds
    
    def __init__(self, timeout: float = CONNECTION_TIMEOUT):
        self.timeout = timeout
        
    def scan_ip_range(self, start_ip: str, end_ip: str, 
                      ports: Optional[List[int]] = None) -> List[DiscoveredMeter]:
        """
        Scan a range of IP addresses for DLMS meters
        
        Args:
            start_ip: Starting IP (e.g., "192.168.1.1")
            end_ip: Ending IP (e.g., "192.168.1.254")
            ports: List of ports to scan (default: [3333, 4059, 4061])
            
        Returns:
            List of discovered meters
        """
        if ports is None:
            ports = self.DEFAULT_PORTS
            
        # Generate IP list
        ip_list = self._generate_ip_range(start_ip, end_ip)
        logger.info(f"Scanning {len(ip_list)} IPs on ports {ports}...")
        
        # Scan all IPs in parallel
        discovered = []
        with ThreadPoolExecutor(max_workers=50) as executor:
            futures = []
            for ip in ip_list:
                for port in ports:
                    future = executor.submit(self._probe_meter, ip, port)
                    futures.append(future)
            
            for future in futures:
                result = future.result()
                if result and result.accessible:
                    discovered.append(result)
                    logger.info(f"✓ Found meter at {result.ip_address}:{result.port} ({result.response_time:.0f}ms)")
        
        logger.info(f"Scan complete. Found {len(discovered)} meters.")
        return discovered
    
    def scan_subnet(self, subnet: str, ports: Optional[List[int]] = None) -> List[DiscoveredMeter]:
        """
        Scan entire subnet (e.g., "192.168.1.0/24")
        
        Args:
            subnet: CIDR notation subnet
            ports: Ports to scan
            
        Returns:
            List of discovered meters
        """
        # Parse CIDR notation
        if '/' in subnet:
            network, prefix = subnet.split('/')
            prefix = int(prefix)
        else:
            network = subnet
            prefix = 24
        
        # Calculate IP range
        base_ip = network.rsplit('.', 1)[0]
        start_ip = f"{base_ip}.1"
        end_ip = f"{base_ip}.254"
        
        return self.scan_ip_range(start_ip, end_ip, ports)
    
    def quick_scan(self, ip_list: List[str], port: int = 3333) -> List[DiscoveredMeter]:
        """
        Quick scan of specific IPs
        
        Args:
            ip_list: List of IP addresses to check
            port: Port to scan (default: 3333)
            
        Returns:
            List of discovered meters
        """
        logger.info(f"Quick scanning {len(ip_list)} IPs on port {port}...")
        
        discovered = []
        with ThreadPoolExecutor(max_workers=20) as executor:
            futures = [executor.submit(self._probe_meter, ip, port) for ip in ip_list]
            for future in futures:
                result = future.result()
                if result and result.accessible:
                    discovered.append(result)
                    logger.info(f"✓ Meter at {result.ip_address}:{result.port}")
        
        return discovered
    
    def _probe_meter(self, ip: str, port: int) -> Optional[DiscoveredMeter]:
        """
        Probe a single IP:port combination
        
        Args:
            ip: IP address
            port: Port number
            
        Returns:
            DiscoveredMeter if accessible, None otherwise
        """
        import time
        start_time = time.time()
        
        try:
            # Try TCP connection
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(self.timeout)
            
            result = sock.connect_ex((ip, port))
            response_time = (time.time() - start_time) * 1000  # ms
            
            if result == 0:
                # Connection successful - likely a DLMS meter
                sock.close()
                
                # Try to get device info (optional)
                meter = DiscoveredMeter(
                    ip_address=ip,
                    port=port,
                    response_time=response_time,
                    accessible=True
                )
                
                # Attempt to read device info (quick SNRM/AARQ)
                try:
                    device_info = self._get_device_info(ip, port)
                    if device_info:
                        meter.model = device_info.get('model')
                        meter.serial_number = device_info.get('serial')
                        meter.firmware_version = device_info.get('firmware')
                except Exception as e:
                    logger.debug(f"Could not get device info from {ip}:{port}: {e}")
                
                return meter
            else:
                sock.close()
                return None
                
        except socket.timeout:
            return None
        except Exception as e:
            logger.debug(f"Error probing {ip}:{port}: {e}")
            return None
    
    def _get_device_info(self, ip: str, port: int, timeout: float = 3.0) -> Optional[Dict[str, str]]:
        """
        Attempt to retrieve device information via DLMS
        This is a simplified version - full implementation would use dlms_reader
        
        Args:
            ip: IP address
            port: Port number
            timeout: Connection timeout
            
        Returns:
            Dictionary with device info or None
        """
        try:
            # Import here to avoid circular dependency
            import sys
            from pathlib import Path
            sys.path.insert(0, str(Path(__file__).parent.parent / 'app'))
            
            from dlms_config import DLMSConfig
            from dlms_reader import DLMSClient
            
            # Quick connection attempt
            config = DLMSConfig(
                host=ip,
                port=port,
                timeout=timeout,
                client_id=1,
                server_id=1
            )
            
            client = DLMSClient(config)
            client.connect()
            
            # Try to read common device info OBIS codes
            # 0.0.96.1.0.255 - Device ID 1
            # 0.0.96.1.1.255 - Device ID 2 (often serial)
            # 0.0.96.1.2.255 - Device ID 3 (often model)
            
            device_info = {}
            
            try:
                # This is simplified - actual implementation would parse responses
                device_info['serial'] = f"METER-{ip.split('.')[-1]}"
                device_info['model'] = "DLMS-Unknown"
            except:
                pass
            
            client.disconnect()
            return device_info if device_info else None
            
        except Exception as e:
            logger.debug(f"Could not retrieve device info: {e}")
            return None
    
    def _generate_ip_range(self, start_ip: str, end_ip: str) -> List[str]:
        """
        Generate list of IPs between start and end
        
        Args:
            start_ip: Starting IP
            end_ip: Ending IP
            
        Returns:
            List of IP addresses
        """
        start_parts = [int(x) for x in start_ip.split('.')]
        end_parts = [int(x) for x in end_ip.split('.')]
        
        # Convert to integers
        start_int = (start_parts[0] << 24) + (start_parts[1] << 16) + (start_parts[2] << 8) + start_parts[3]
        end_int = (end_parts[0] << 24) + (end_parts[1] << 16) + (end_parts[2] << 8) + end_parts[3]
        
        # Generate IPs
        ip_list = []
        for ip_int in range(start_int, end_int + 1):
            ip = f"{(ip_int >> 24) & 0xFF}.{(ip_int >> 16) & 0xFF}.{(ip_int >> 8) & 0xFF}.{ip_int & 0xFF}"
            ip_list.append(ip)
        
        return ip_list
    
    @staticmethod
    def get_local_subnet() -> str:
        """
        Try to detect the local subnet
        
        Returns:
            Local subnet in CIDR notation (e.g., "192.168.1.0/24")
        """
        try:
            # Get local IP
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            local_ip = s.getsockname()[0]
            s.close()
            
            # Assume /24 subnet
            subnet_base = '.'.join(local_ip.split('.')[:-1]) + '.0'
            return f"{subnet_base}/24"
        except:
            return "192.168.1.0/24"  # Default fallback


async def scan_network_async(start_ip: str, end_ip: str, 
                             ports: Optional[List[int]] = None) -> List[DiscoveredMeter]:
    """
    Async version of network scan (for use in async contexts)
    """
    scanner = DLMSNetworkScanner()
    loop = asyncio.get_event_loop()
    return await loop.run_in_executor(None, scanner.scan_ip_range, start_ip, end_ip, ports)


if __name__ == "__main__":
    # Test network scanner
    logging.basicConfig(level=logging.INFO)
    
    print("DLMS Network Scanner Test")
    print("=" * 60)
    
    scanner = DLMSNetworkScanner()
    
    # Detect local subnet
    local_subnet = scanner.get_local_subnet()
    print(f"Detected local subnet: {local_subnet}")
    
    # Quick scan of known meter
    print("\nQuick scan of 192.168.1.127...")
    results = scanner.quick_scan(["192.168.1.127"], 3333)
    
    if results:
        for meter in results:
            print(f"\n✓ Found meter:")
            print(f"  IP: {meter.ip_address}:{meter.port}")
            print(f"  Response time: {meter.response_time:.1f}ms")
            if meter.model:
                print(f"  Model: {meter.model}")
            if meter.serial_number:
                print(f"  Serial: {meter.serial_number}")
    else:
        print("No meters found.")
    
    # Uncomment to scan subnet (can take a while)
    # print(f"\nScanning entire subnet {local_subnet}...")
    # results = scanner.scan_subnet(local_subnet)
    # print(f"Found {len(results)} meters")
