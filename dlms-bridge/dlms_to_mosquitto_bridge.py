#!/usr/bin/env python3
"""
Bridge: DLMS Reader → Mosquitto (1884) → ThingsBoard Gateway Connector

Lee datos DLMS y los publica a Mosquitto en el tópico que el connector escucha.
"""

import sys
import time
import signal
import logging
import json
import paho.mqtt.client as mqtt
from typing import Dict, Optional
from datetime import datetime

# DLMS Reader
from dlms_reader import DLMSClient

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


class DLMSToMosquittoBridge:
    """Bridge DLMS → Mosquitto para ThingsBoard Gateway Connector."""
    
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
        session.close()
        
        # Config MQTT Mosquitto
        self.mqtt_host = "localhost"
        self.mqtt_port = 1884
        self.mqtt_topic = "v1/devices/me/telemetry"
        
        self.interval = interval
        self.dlms: Optional[DLMSClient] = None
        self.mqtt_client: Optional[mqtt.Client] = None
        self.mqtt_connected = False
        
        # Stats
        self.total = 0
        self.success = 0
        
        logger.info("=" * 70)
        logger.info(f"🌉 DLMS → Mosquitto Bridge")
        logger.info(f"📊 Medidor: {self.meter_name}")
        logger.info(f"🔌 DLMS: {self.dlms_host}:{self.dlms_port}")
        logger.info(f"🦟 Mosquitto: {self.mqtt_host}:{self.mqtt_port}")
        logger.info(f"📡 Topic: {self.mqtt_topic}")
        logger.info(f"⏱️  Intervalo: {self.interval}s")
        logger.info("=" * 70)
    
    def on_mqtt_connect(self, client, userdata, flags, rc):
        """Callback MQTT conexión."""
        if rc == 0:
            self.mqtt_connected = True
            logger.info("✅ Mosquitto conectado")
        else:
            logger.error(f"❌ Mosquitto error: {rc}")
            self.mqtt_connected = False
    
    def on_mqtt_disconnect(self, client, userdata, rc):
        """Callback MQTT desconexión."""
        self.mqtt_connected = False
        if rc != 0:
            logger.warning(f"⚠️ Mosquitto desconectado inesperadamente: {rc}")
            # Auto-reconectar será manejado por loop_start()
    
    def on_mqtt_publish(self, client, userdata, mid):
        """Callback cuando se publica exitosamente."""
        logger.debug(f"✓ Mensaje {mid} publicado")
    
    def connect(self) -> bool:
        """Conecta DLMS y MQTT con reintentos."""
        # DLMS con reintentos
        max_retries = 5
        retry_delays = [1, 2, 3, 5, 10]  # Backoff progresivo
        
        for attempt in range(max_retries):
            try:
                logger.info(f"🔌 Conectando DLMS (intento {attempt + 1}/{max_retries})...")
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
                break  # Éxito, salir del loop
            except Exception as e:
                error_msg = str(e)
                if attempt < max_retries - 1:
                    delay = retry_delays[attempt]
                    logger.warning(f"⚠️ Error DLMS intento {attempt + 1}: {error_msg}. Reintentando en {delay}s...")
                    time.sleep(delay)
                    # Limpiar objeto dlms
                    if self.dlms:
                        try:
                            self.dlms.close()
                        except:
                            pass
                        self.dlms = None
                else:
                    logger.error(f"❌ Error DLMS después de {max_retries} intentos: {error_msg}")
                    return False
        
        # MQTT Mosquitto
        try:
            logger.info(f"🔌 Conectando Mosquitto...")
            self.mqtt_client = mqtt.Client(
                client_id=f"dlms-bridge-{self.meter_name}",
                protocol=mqtt.MQTTv311,
                clean_session=True
            )
            self.mqtt_client.on_connect = self.on_mqtt_connect
            self.mqtt_client.on_disconnect = self.on_mqtt_disconnect
            self.mqtt_client.on_publish = self.on_mqtt_publish
            
            # Configurar reconexión automática
            self.mqtt_client.reconnect_delay_set(min_delay=1, max_delay=10)
            
            self.mqtt_client.connect(self.mqtt_host, self.mqtt_port, keepalive=60)
            self.mqtt_client.loop_start()
            
            # Esperar conexión
            timeout = 10
            start_wait = time.time()
            while not self.mqtt_connected and (time.time() - start_wait) < timeout:
                time.sleep(0.1)
            
            if not self.mqtt_connected:
                raise Exception("Timeout conectando a Mosquitto")
            
        except Exception as e:
            logger.error(f"❌ Error Mosquitto: {e}")
            return False
        
        return True
    
    def read_measurement(self, key: str, max_retries: int = 3) -> Optional[float]:
        """Lee una medición DLMS con reintentos en caso de error HDLC."""
        if not self.dlms:
            return None
        
        obis, unit = MEASUREMENTS[key]
        retry_delays = [0.1, 0.2, 0.4]  # Exponential backoff: 100ms, 200ms, 400ms
        
        for attempt in range(max_retries):
            try:
                result = self.dlms.read_register(obis)
                if result and len(result) >= 2:
                    value = result[0]
                    if attempt > 0:
                        logger.info(f"✅ {key} recuperado en intento {attempt + 1}")
                    return float(value) if value is not None else None
            except Exception as e:
                error_str = str(e)
                is_hdlc_error = "HDLC" in error_str or "Invalid" in error_str or "boundary" in error_str
                
                if is_hdlc_error and attempt < max_retries - 1:
                    delay = retry_delays[attempt]
                    logger.warning(f"⚠️ Error HDLC en {key} intento {attempt + 1}/{max_retries}, retry en {delay}s")
                    time.sleep(delay)
                    
                    # Reconectar rápido para limpiar estado
                    try:
                        self.dlms.close()
                        time.sleep(0.05)
                        self.dlms.connect()
                    except:
                        pass
                    continue
                else:
                    if attempt == max_retries - 1:
                        logger.error(f"❌ Error {key} después de {max_retries} intentos: {e}")
                    else:
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
        """Publica a Mosquitto con QoS 0 para realtime."""
        if not self.mqtt_client or not data:
            return False
        
        # Si no está conectado, esperar un poco
        if not self.mqtt_connected:
            logger.warning("⚠️ MQTT desconectado, esperando...")
            time.sleep(1)
            return False
        
        try:
            payload = json.dumps(data)
            result = self.mqtt_client.publish(
                self.mqtt_topic,
                payload,
                qos=0,  # QoS 0 - Fire and forget para realtime (menor latencia)
                retain=False
            )
            
            # Con QoS 0 no esperamos confirmación
            return result.rc == mqtt.MQTT_ERR_SUCCESS
                
        except Exception as e:
            logger.error(f"❌ Error publicando: {e}")
            return False
    
    def run(self):
        """Loop principal."""
        global running
        
        if not self.connect():
            logger.error("❌ No se pudo conectar. Abortando.")
            return 1
        
        logger.info("🚀 Iniciando polling...\n")
        
        consecutive_errors = 0
        reads_since_reconnect = 0
        MAX_READS_BEFORE_RECONNECT = 10  # Reconectar preventivamente cada 10 lecturas (~30s con interval=2s)
        
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
                        reads_since_reconnect += 1
                        
                        # Log
                        values_str = " | ".join([
                            f"{k.upper()[:3]}: {v:7.2f}" 
                            for k, v in data.items()
                        ])
                        logger.info(f"📤 {values_str}")
                        
                        # Reconexión preventiva cada 10 lecturas para evitar sesión zombie
                        if reads_since_reconnect >= MAX_READS_BEFORE_RECONNECT:
                            logger.info(f"🔄 Reconexión preventiva después de {reads_since_reconnect} lecturas")
                            if self.dlms:
                                try:
                                    self.dlms.close()
                                    time.sleep(1.0)  # Pausa más larga para limpiar socket
                                except:
                                    pass
                            
                            # Usar connect() completo con reintentos en lugar de dlms.connect()
                            if self.connect():
                                reads_since_reconnect = 0
                                logger.info("✅ DLMS reconectado preventivamente")
                            else:
                                logger.error("❌ Error en reconexión preventiva")
                                consecutive_errors = 5  # Forzar circuit breaker si falla reconexión preventiva
                    else:
                        consecutive_errors += 1
                        logger.warning(f"⚠️ Fallo MQTT")
                else:
                    consecutive_errors += 1
                    logger.warning(f"⚠️ Sin datos DLMS")
                
                # Circuit breaker - reducido a 5 errores consecutivos
                if consecutive_errors >= 5:
                    logger.error("⚡ 5 errores consecutivos. Reconectando...")
                    if self.dlms:
                        try:
                            self.dlms.close()
                        except:
                            pass
                    if not self.connect():
                        break
                    consecutive_errors = 0
                    reads_since_reconnect = 0  # Reset contador
                
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
            
            if self.mqtt_client:
                try:
                    self.mqtt_client.loop_stop()
                    self.mqtt_client.disconnect()
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
    
    bridge = DLMSToMosquittoBridge(meter_id=args.meter_id, interval=args.interval)
    sys.exit(bridge.run())
