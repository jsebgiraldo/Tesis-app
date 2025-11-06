#!/usr/bin/env python3
"""
DLMS to ThingsBoard Gateway Bridge
===================================
Envía datos DLMS a través del ThingsBoard Gateway.

Este script:
1. Lee datos del medidor DLMS usando tu lógica existente
2. Los envía al Gateway de ThingsBoard en formato correcto
3. El Gateway los reenvía a ThingsBoard como dispositivos hijos

Configuración del Gateway en ThingsBoard:
- Nombre: DLMS-BRIDGE
- Token: oCS3U0Grx4URhssER2QX
- Puerto: 1883
"""

import time
import signal
import sys
import json
from datetime import datetime
from pathlib import Path

# Add project root to path
sys.path.insert(0, str(Path(__file__).parent))

from admin.database import Database, get_all_meters
from tb_mqtt_client import ThingsBoardMQTTClient

# Control de ejecución
running = True

def signal_handler(sig, frame):
    """Maneja señales de interrupción"""
    global running
    print("\n🛑 Deteniendo bridge...")
    running = False

signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)


class SimpleDLMSReader:
    """
    Lector DLMS simplificado que no requiere dlms_client_robust
    Usa solo dlms-cosem básico
    """
    
    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.connection = None
        
    def connect(self):
        """Conectar al medidor DLMS"""
        try:
            from dlms_cosem.io import BlockingTcpIO, TcpTransport
            
            # Crear IO bloqueante
            io = BlockingTcpIO(
                host=self.host,
                port=self.port,
                timeout=10
            )
            
            # Crear transporte
            transport = TcpTransport(
                client_logical_address=1,
                server_logical_address=1,
                io=io,
                timeout=10
            )
            
            # Conectar
            transport.connect()
            
            self.connection = transport
            return True
            
        except Exception as e:
            print(f"❌ Error conectando a {self.host}:{self.port}: {e}")
            return False
    
    def read_measurements(self, measurements):
        """Lee las mediciones del medidor"""
        if not self.connection:
            return None
        
        # Mapeo de nombres a OBIS codes (usando objetos Cosem Address)
        from dlms_cosem.cosem import CosemAttribute
        
        telemetry = {}
        
        # Mapeo simplificado de OBIS
        OBIS_MAP = {
            "voltage_l1": (1, 0, 32, 7, 0, 255),
            "voltage_l2": (1, 0, 52, 7, 0, 255),
            "voltage_l3": (1, 0, 72, 7, 0, 255),
            "current_l1": (1, 0, 31, 7, 0, 255),
            "current_l2": (1, 0, 51, 7, 0, 255),
            "current_l3": (1, 0, 71, 7, 0, 255),
            "active_power": (1, 0, 1, 7, 0, 255),
            "reactive_power": (1, 0, 3, 7, 0, 255),
            "frequency": (1, 0, 14, 7, 0, 255),
            "active_energy": (1, 0, 1, 8, 0, 255),
        }
        
        for measurement in measurements:
            obis = OBIS_MAP.get(measurement)
            if not obis:
                continue
            
            try:
                # Crear atributo COSEM
                attr = CosemAttribute(
                    interface=3,  # Register class
                    instance=obis,
                    attribute=2  # Value attribute
                )
                
                # Leer valor
                result = self.connection.get(attr)
                if result:
                    telemetry[measurement] = float(result)
            except Exception as e:
                # Silenciar errores individuales
                pass
        
        return telemetry if telemetry else None
    
    def disconnect(self):
        """Desconectar del medidor"""
        if self.connection:
            try:
                self.connection.disconnect()
            except:
                pass
            self.connection = None


def main():
    print("=" * 70)
    print("🌉 DLMS to ThingsBoard Gateway Bridge")
    print("=" * 70)
    print()
    
    # Cargar configuración de la base de datos
    db = Database('data/admin.db')
    db.initialize()
    
    with db.get_session() as session:
        meters = get_all_meters(session)
        
        if not meters:
            print("❌ No hay medidores configurados en la base de datos")
            print("   Usa el Admin Dashboard para configurar medidores")
            return 1
        
        # Filtrar solo medidores con ThingsBoard habilitado
        active_meters = [m for m in meters if m.tb_enabled]
        
        if not active_meters:
            print("❌ No hay medidores con ThingsBoard habilitado")
            return 1
        
        print(f"📊 Medidores activos: {len(active_meters)}")
        print()
        
        # Usar el primer medidor (puedes extender para múltiples)
        meter = active_meters[0]
        
        print(f"🔌 Configuración:")
        print(f"   Medidor: {meter.name}")
        print(f"   DLMS: {meter.ip_address}:{meter.port}")
        print(f"   Gateway Token: {meter.tb_token[:10]}...{meter.tb_token[-4:]}")
        print(f"   MQTT Broker: {meter.tb_host}:{meter.tb_port}")
        print()
        
        # Obtener mediciones habilitadas
        measurements = [cfg.measurement_name for cfg in meter.configs if cfg.enabled]
        if not measurements:
            measurements = ["voltage_l1", "current_l1", "active_power", "frequency"]
        
        print(f"📈 Mediciones: {', '.join(measurements)}")
        print()
    
    # Conectar al Gateway MQTT
    print("🔌 Conectando a ThingsBoard Gateway...")
    
    mqtt_client = ThingsBoardMQTTClient(
        host=meter.tb_host,
        port=meter.tb_port,
        token=meter.tb_token,
        client_id=f"dlms_bridge_{meter.id}"
    )
    
    connected = mqtt_client.connect(timeout=10)
    
    if not connected:
        print("❌ No se pudo conectar al Gateway MQTT")
        return 1
    
    print("✅ Conectado al Gateway")
    print()
    
    # Crear lector DLMS
    print("🔌 Conectando a medidor DLMS...")
    dlms_reader = SimpleDLMSReader(meter.ip_address, meter.port)
    
    if not dlms_reader.connect():
        print("❌ No se pudo conectar al medidor DLMS")
        mqtt_client.stop()
        return 1
    
    print("✅ Conectado al medidor DLMS")
    print()
    print("=" * 70)
    print("📊 Iniciando polling (Ctrl+C para detener)")
    print("=" * 70)
    print()
    
    # Estadísticas
    cycle = 0
    successful = 0
    failed = 0
    
    try:
        while running:
            cycle += 1
            timestamp = datetime.now()
            
            # Leer datos del medidor
            telemetry = dlms_reader.read_measurements(measurements)
            
            if telemetry:
                # Enviar a Gateway
                success = mqtt_client.publish_telemetry(telemetry, wait=False)
                
                if success:
                    successful += 1
                    print(f"[{cycle:04d}] ✅ {timestamp.strftime('%H:%M:%S')} | "
                          f"{len(telemetry)} mediciones enviadas")
                    
                    # Mostrar valores cada 10 ciclos
                    if cycle % 10 == 0:
                        print(f"        📊 {telemetry}")
                else:
                    failed += 1
                    print(f"[{cycle:04d}] ⚠️  Error enviando a Gateway")
            else:
                failed += 1
                print(f"[{cycle:04d}] ⚠️  No se obtuvieron datos del medidor")
            
            # Mostrar estadísticas cada 20 ciclos
            if cycle % 20 == 0:
                success_rate = (successful / cycle * 100) if cycle > 0 else 0
                print(f"\n📈 Estadísticas: {successful}/{cycle} exitosos ({success_rate:.1f}%)\n")
            
            # Esperar intervalo (5 segundos por defecto)
            time.sleep(5)
    
    except KeyboardInterrupt:
        print("\n🛑 Interrupción recibida")
    
    finally:
        # Limpiar
        print("\n🧹 Limpiando...")
        dlms_reader.disconnect()
        mqtt_client.stop()
        
        # Estadísticas finales
        print("\n" + "=" * 70)
        print("📊 ESTADÍSTICAS FINALES")
        print("=" * 70)
        print(f"  Ciclos totales:      {cycle}")
        print(f"  Exitosos:            {successful}")
        print(f"  Fallidos:            {failed}")
        if cycle > 0:
            print(f"  Tasa de éxito:       {successful/cycle*100:.1f}%")
        print("=" * 70)
        print("✅ Bridge detenido correctamente")
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
