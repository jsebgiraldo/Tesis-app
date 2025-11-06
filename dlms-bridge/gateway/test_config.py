#!/usr/bin/env python3
"""
Test script for DLMS Connector
Verifica la configuración y conectividad antes de iniciar el gateway
"""

import json
import yaml
import sys
from pathlib import Path

# Colors
GREEN = '\033[0;32m'
RED = '\033[0;31m'
YELLOW = '\033[1;33m'
BLUE = '\033[0;34m'
NC = '\033[0m'

def print_success(msg):
    print(f"{GREEN}✓ {msg}{NC}")

def print_error(msg):
    print(f"{RED}✗ {msg}{NC}")

def print_warning(msg):
    print(f"{YELLOW}⚠ {msg}{NC}")

def print_info(msg):
    print(f"{BLUE}ℹ {msg}{NC}")

def print_header(msg):
    print(f"\n{BLUE}{'='*60}{NC}")
    print(f"{BLUE}{msg}{NC}")
    print(f"{BLUE}{'='*60}{NC}")

def check_config_files():
    """Verificar que existen los archivos de configuración"""
    print_header("Checking Configuration Files")
    
    gateway_dir = Path(__file__).parent
    config_dir = gateway_dir / "config"
    
    required_files = [
        config_dir / "tb_gateway.yaml",
        config_dir / "dlms_connector.json"
    ]
    
    all_exist = True
    for file_path in required_files:
        if file_path.exists():
            print_success(f"Found: {file_path.name}")
        else:
            print_error(f"Missing: {file_path.name}")
            all_exist = False
    
    return all_exist

def validate_gateway_config():
    """Validar configuración del gateway"""
    print_header("Validating Gateway Configuration")
    
    config_path = Path(__file__).parent / "config" / "tb_gateway.yaml"
    
    try:
        with open(config_path, 'r') as f:
            config = yaml.safe_load(f)
        
        # Check ThingsBoard connection
        tb_config = config.get('thingsboard', {})
        host = tb_config.get('host')
        port = tb_config.get('port')
        token = tb_config.get('security', {}).get('accessToken')
        
        if not host:
            print_error("ThingsBoard host not configured")
            return False
        else:
            print_success(f"ThingsBoard host: {host}")
        
        if not port:
            print_warning("ThingsBoard port not configured, using default 1883")
        else:
            print_success(f"ThingsBoard port: {port}")
        
        if not token or token == "YOUR_GATEWAY_ACCESS_TOKEN":
            print_error("Gateway access token not configured!")
            print_info("Get token from ThingsBoard UI: Devices → Your Gateway → Details")
            return False
        else:
            print_success(f"Access token configured: {token[:10]}...{token[-4:]}")
        
        # Check connectors
        connectors = config.get('connectors', [])
        if not connectors:
            print_error("No connectors configured")
            return False
        
        dlms_connector = None
        for conn in connectors:
            if conn.get('type') == 'custom' and conn.get('class') == 'DLMSConnector':
                dlms_connector = conn
                break
        
        if not dlms_connector:
            print_error("DLMS Connector not found in configuration")
            return False
        else:
            print_success(f"DLMS Connector configured: {dlms_connector.get('name')}")
            if dlms_connector.get('enabled', True):
                print_success("DLMS Connector is enabled")
            else:
                print_warning("DLMS Connector is disabled")
        
        return True
    
    except Exception as e:
        print_error(f"Error validating gateway config: {e}")
        return False

def validate_dlms_config():
    """Validar configuración de dispositivos DLMS"""
    print_header("Validating DLMS Devices Configuration")
    
    config_path = Path(__file__).parent / "config" / "dlms_connector.json"
    
    try:
        with open(config_path, 'r') as f:
            config = json.load(f)
        
        devices = config.get('devices', [])
        
        if not devices:
            print_error("No DLMS devices configured")
            return False
        
        print_success(f"Found {len(devices)} DLMS device(s)")
        
        for i, device in enumerate(devices, 1):
            print(f"\n  Device {i}: {device.get('name', 'Unknown')}")
            
            # Check required fields
            required_fields = ['name', 'host', 'port', 'measurements']
            for field in required_fields:
                if field in device:
                    print_success(f"    {field}: {device[field]}")
                else:
                    print_error(f"    Missing required field: {field}")
                    return False
            
            # Check measurements
            measurements = device.get('measurements', [])
            if not measurements:
                print_warning(f"    No measurements configured for {device['name']}")
            else:
                print_info(f"    {len(measurements)} measurement(s) configured")
        
        return True
    
    except Exception as e:
        print_error(f"Error validating DLMS config: {e}")
        return False

def test_dlms_connectivity():
    """Probar conectividad a dispositivos DLMS"""
    print_header("Testing DLMS Device Connectivity")
    
    config_path = Path(__file__).parent / "config" / "dlms_connector.json"
    
    try:
        with open(config_path, 'r') as f:
            config = json.load(f)
        
        devices = config.get('devices', [])
        
        import socket
        
        all_reachable = True
        for device in devices:
            name = device.get('name')
            host = device.get('host')
            port = device.get('port', 3333)
            
            print(f"\n  Testing {name} ({host}:{port})...")
            
            # Test TCP connection
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(3)
                result = sock.connect_ex((host, port))
                sock.close()
                
                if result == 0:
                    print_success(f"    TCP connection successful")
                else:
                    print_error(f"    Cannot connect to {host}:{port}")
                    all_reachable = False
            except Exception as e:
                print_error(f"    Connection error: {e}")
                all_reachable = False
        
        return all_reachable
    
    except Exception as e:
        print_error(f"Error testing connectivity: {e}")
        return False

def check_python_dependencies():
    """Verificar dependencias de Python"""
    print_header("Checking Python Dependencies")
    
    required_packages = [
        'thingsboard_gateway',
        'dlms_cosem',
        'yaml'
    ]
    
    all_installed = True
    for package in required_packages:
        try:
            __import__(package)
            print_success(f"{package} installed")
        except ImportError:
            print_error(f"{package} NOT installed")
            all_installed = False
    
    if not all_installed:
        print_info("\nInstall missing packages with:")
        print_info("  pip install -r requirements-gateway.txt")
    
    return all_installed

def main():
    print_header("ThingsBoard Gateway - DLMS Connector Test")
    
    results = {
        'config_files': check_config_files(),
        'gateway_config': validate_gateway_config(),
        'dlms_config': validate_dlms_config(),
        'dependencies': check_python_dependencies(),
        'connectivity': test_dlms_connectivity()
    }
    
    print_header("Test Results Summary")
    
    for test_name, result in results.items():
        status = "PASS" if result else "FAIL"
        color = GREEN if result else RED
        print(f"  {test_name:20s}: {color}{status}{NC}")
    
    all_passed = all(results.values())
    
    print()
    if all_passed:
        print_success("All tests passed! ✓")
        print_info("\nYou can now start the gateway:")
        print_info("  ./start_gateway.sh")
        print_info("or")
        print_info("  sudo systemctl start tb-gateway.service")
        return 0
    else:
        print_error("Some tests failed! ✗")
        print_info("\nPlease fix the issues before starting the gateway.")
        return 1

if __name__ == '__main__':
    sys.exit(main())
