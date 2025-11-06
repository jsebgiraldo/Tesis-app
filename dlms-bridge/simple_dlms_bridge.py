#!/usr/bin/env python3
"""
Simple DLMS to ThingsBoard Bridge
Lectura directa de medidor DLMS y envío a ThingsBoard usando el SDK oficial
"""

import time
import sys
import signal
from pathlib import Path

# Add project root
sys.path.insert(0, str(Path(__file__).parent))

from tb_mqtt_client import ThingsBoardMQTTClient
from dlms_cosem import cosem
from dlms_cosem.client import TcpTransport
from dlms_cosem.protocol import DlmsConnection

# Configuration from database
from admin.database import Database, get_meter_by_id

# Mediciones disponibles
MEASUREMENTS = {
    "voltage_l1": "1-1:32.7.0",
    "current_l1": "1-1:31.7.0",
    "frequency": "1-1:14.7.0",
    "active_power": "1-1:1.7.0",
    "active_energy": "1-1:1.8.0",
}

running = True

def signal_handler(sig, frame):
    global running
    print("\n[!] Deteniendo...")
    running = False

def main():
    global running
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    print("=" * 70)
    print("DLMS to ThingsBoard Bridge (Simple)")
    print("=" * 70)
    
    # Load config from database
    db = Database('data/admin.db')
    session = db.get_session()
    meter = get_meter_by_id(session, 1)
    
    if not meter:
        print("❌ No se encontró medidor en base de datos")
        return 1
    
    print(f"\n✓ Medidor: {meter.name}")
    print(f"  DLMS: {meter.ip_address}:{meter.port}")
    print(f"  ThingsBoard: {meter.tb_host}:{meter.tb_port}")
    print(f"  Token: {meter.tb_token[:10]}...{meter.tb_token[-4:]}")
    
    # Connect to ThingsBoard
    print(f"\n🔌 Conectando a ThingsBoard...")
    mqtt_client = ThingsBoardMQTTClient(
        host=meter.tb_host,
        port=meter.tb_port,
        token=meter.tb_token
    )
    
    connected = mqtt_client.connect(timeout=30, keepalive=60)
    if not connected:
        print("❌ No se pudo conectar a ThingsBoard")
        return 1
    
    print("✅ Conectado a ThingsBoard")
    
    # Connect to DLMS meter
    print(f"\n🔌 Conectando a medidor DLMS...")
    
    try:
        # Using direct TCP connection
        from dlms_cosem.io import TcpIO
        from dlms_cosem.connection import DlmsConnection
        
        io = TcpIO(
            host=meter.ip_address,
            port=meter.port,
            timeout=5
        )
        
        connection = DlmsConnection(
            client_logical_address=1,
            server_logical_address=1,
            io=io
        )
        
        connection.connect()
        
        print("✅ Conectado a medidor DLMS")
        print(f"\n📊 Iniciando polling (Ctrl+C para detener)")
        print("=" * 70)
        
        cycle = 0
        successful = 0
        
        while running:
            cycle += 1
            
            try:
                # Read measurements
                telemetry = {}
                
                for name, obis in MEASUREMENTS.items():
                    try:
                        result = connection.get(obis)
                        if result:
                            telemetry[name] = float(result.value)
                    except Exception as e:
                        print(f"⚠️  Error leyendo {name}: {e}")
                
                if telemetry:
                    # Send to ThingsBoard
                    success = mqtt_client.publish_telemetry(telemetry, wait=False)
                    
                    if success:
                        successful += 1
                        print(f"[{cycle}] ✅ Datos enviados: {telemetry}")
                    else:
                        print(f"[{cycle}] ⚠️  Error enviando datos")
                else:
                    print(f"[{cycle}] ⚠️  No se obtuvieron datos")
                
                # Wait interval
                time.sleep(5)
                
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"[{cycle}] ❌ Error: {e}")
                time.sleep(5)
        
        connection.disconnect()
        
        print(f"\n📊 Estadísticas:")
        print(f"  Ciclos totales: {cycle}")
        print(f"  Exitosos: {successful}")
        print(f"  Tasa de éxito: {successful/cycle*100:.1f}%")
        
    except Exception as e:
        print(f"❌ Error conectando a DLMS: {e}")
        import traceback
        traceback.print_exc()
        return 1
    
    finally:
        mqtt_client.stop()
        print("\n✓ Desconectado")
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
