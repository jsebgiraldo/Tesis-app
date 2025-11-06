#!/usr/bin/env python3
"""
Bridge SIMPLE: DLMS Reader → ThingsBoard Gateway MQTT

Usa el código DLMS que ya funciona + tu cliente MQTT robusto.
Sin dependencias extras, sin complejidad innecesaria.
"""

import sys
import time
import signal
import logging
from typing import Dict, Optional
from datetime import datetime

# Tu código DLMS que SÍ funciona
from dlms_reader import DLMSClient

# Tu cliente MQTT robusto
from tb_mqtt_client import ThingsBoardMQTTClient

# Database
from admin.database import Database, get_meter_by_id

# Logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s'
)
logger = logging.getLogger(__name__)

# Control
running = True

def signal_handler(sig, frame):
    global running
    logger.info("🛑 Deteniendo bridge...")
    running = False

signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)

# Mediciones DLMS
MEASUREMENTS = {
    "voltage_l1": ("1-1:32.7.0", "V"),
    "current_l1": ("1-1:31.7.0", "A"),
    "frequency": ("1-1:14.7.0", "Hz"),
    "active_power": ("1-1:1.7.0", "W"),
    "active_energy": ("1-1:1.8.0", "Wh"),
}


class SimpleDLMSBridge:
    """Bridge minimalista DLMS → ThingsBoard Gateway."""
    
    def __init__(self, meter_id: int = 1, interval: float = 5.0):
        # Cargar config de DB
        db = Database('data/admin.db')
        session = db.get_session()
        meter = get_meter_by_id(session, meter_id)
        
        if not meter:
            raise ValueError(f"Medidor {meter_id} no encontrado")
        
        self.meter_name = meter.name
        self.dlms_host = meter.ip_address
        self.dlms_port = meter.port
        self.gateway_token = meter.tb_token
        self.gateway_host = meter.tb_host or "localhost"
        self.gateway_port = meter.tb_port or 1883
        session.close()
        
        self.interval = interval
        self.dlms: Optional[DLMSClient] = None
        self.mqtt: Optional[ThingsBoardMQTTClient] = None
        
        # Stats
        self.total = 0
        self.success = 0
        
        logger.info("=" * 70)
        logger.info(f"🌉 Simple DLMS Bridge → ThingsBoard Gateway")
        logger.info(f"📊 Medidor: {self.meter_name}")
        logger.info(f"🔌 DLMS: {self.dlms_host}:{self.dlms_port}")
        logger.info(f"🌐 Gateway: {self.gateway_host}:{self.gateway_port}")
        logger.info(f"⏱️  Intervalo: {self.interval}s")
        logger.info("=" * 70)
    
    def connect(self) -> bool:
        """Conecta DLMS y MQTT."""
        # DLMS
        try:
            logger.info(f"🔌 Conectando DLMS...")
            self.dlms = DLMSClient(
                host=self.dlms_host,
                port=self.dlms_port,
                client_sap=1,
                server_logical=0,
                server_physical=1,
                password=b"22222222",
                timeout=5.0,
                max_info_length=None
            )
            self.dlms.connect()
            logger.info("✅ DLMS conectado")
        except Exception as e:
            logger.error(f"❌ Error DLMS: {e}")
            return False
        
        # MQTT
        try:
            logger.info(f"🔌 Conectando MQTT Gateway...")
            self.mqtt = ThingsBoardMQTTClient(
                host=self.gateway_host,
                port=self.gateway_port,
                token=self.gateway_token,
                client_id=f"dlms_{self.meter_name}"
            )
            if not self.mqtt.connect(timeout=10):
                raise Exception("Timeout MQTT")
            logger.info("✅ MQTT Gateway conectado")
        except Exception as e:
            logger.error(f"❌ Error MQTT: {e}")
            return False
        
        return True
    
    def read_measurement(self, key: str) -> Optional[float]:
        """Lee una medición DLMS."""
        if not self.dlms:
            return None
        
        obis, unit = MEASUREMENTS[key]
        
        try:
            result = self.dlms.read_register(obis)
            if result and len(result) >= 2:
                value = result[0]
                return float(value) if value is not None else None
        except Exception as e:
            logger.debug(f"⚠️ Error {key}: {e}")
        
        return None
    
    def poll(self) -> Dict[str, float]:
        """Lee todos los valores."""
        data = {}
        for key in MEASUREMENTS.keys():
            value = self.read_measurement(key)
            if value is not None:
                data[key] = value
        return data
    
    def publish(self, data: Dict[str, float]) -> bool:
        """Publica al Gateway."""
        if not self.mqtt or not data:
            return False
        
        try:
            self.mqtt.publish_telemetry(data)
            return True
        except:
            return False
    
    def run(self):
        """Loop principal."""
        global running
        
        if not self.connect():
            logger.error("❌ No se pudo conectar. Abortando.")
            return 1
        
        logger.info("🚀 Iniciando polling...\n")
        
        consecutive_errors = 0
        
        try:
            while running:
                start = time.time()
                self.total += 1
                
                # Leer
                data = self.poll()
                
                # Publicar
                if data:
                    if self.publish(data):
                        self.success += 1
                        consecutive_errors = 0
                        
                        # Log
                        values_str = " | ".join([
                            f"{k.upper()[:3]}: {v:7.2f}" 
                            for k, v in data.items()
                        ])
                        logger.info(f"📤 {values_str}")
                    else:
                        consecutive_errors += 1
                        logger.warning(f"⚠️ Fallo MQTT")
                else:
                    consecutive_errors += 1
                    logger.warning(f"⚠️ Sin datos DLMS")
                
                # Circuit breaker
                if consecutive_errors >= 10:
                    logger.error("⚡ Demasiados errores. Reconectando...")
                    if self.dlms:
                        try:
                            self.dlms.close()
                        except:
                            pass
                    if not self.connect():
                        break
                    consecutive_errors = 0
                
                # Stats cada 20 ciclos
                if self.total % 20 == 0:
                    rate = (self.success / self.total * 100) if self.total > 0 else 0
                    logger.info(f"📊 {self.success}/{self.total} ({rate:.1f}%)")
                
                # Esperar
                elapsed = time.time() - start
                sleep_time = max(0, self.interval - elapsed)
                if sleep_time > 0 and running:
                    time.sleep(sleep_time)
        
        except Exception as e:
            logger.error(f"❌ Error fatal: {e}")
            import traceback
            traceback.print_exc()
        
        finally:
            logger.info("\n🛑 Cerrando conexiones...")
            
            if self.dlms:
                try:
                    self.dlms.close()
                except:
                    pass
            
            if self.mqtt:
                try:
                    self.mqtt.disconnect()
                except:
                    pass
            
            rate = (self.success / self.total * 100) if self.total > 0 else 0
            logger.info("=" * 70)
            logger.info(f"📊 FINAL: {self.success}/{self.total} exitosos ({rate:.1f}%)")
            logger.info("=" * 70)
        
        return 0


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--meter-id", type=int, default=1)
    parser.add_argument("--interval", type=float, default=5.0)
    args = parser.parse_args()
    
    bridge = SimpleDLMSBridge(meter_id=args.meter_id, interval=args.interval)
    sys.exit(bridge.run())
