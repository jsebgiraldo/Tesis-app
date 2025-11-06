#!/usr/bin/env python3
"""
Bridge DLMS → ThingsBoard Gateway usando DlmsClient oficial.

Características:
- Usa dlms-cosem (biblioteca oficial) sin dependencias adicionales
- Se conecta al Gateway MQTT de ThingsBoard (no directamente al servidor)
- Lee mediciones DLMS estándar de medidores
- Publicación con reconnection y buffer offline
- Compatible con servicios systemd
"""

import sys
import time
import signal
import logging
from typing import Dict, Optional
from datetime import datetime

# dlms-cosem - biblioteca oficial
from dlms_cosem.client import DlmsClient
from dlms_cosem.io import BlockingTcpIO, TcpTransport
from dlms_cosem import cosem, enumerations

# Cliente MQTT para ThingsBoard (ya existe en tu proyecto)
from tb_mqtt_client import ThingsBoardMQTTClient

# Database del proyecto
from admin.database import Database, get_meter_by_id

# Configuración logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S'
)
logger = logging.getLogger(__name__)

# Control de ejecución
running = True

def signal_handler(sig, frame):
    """Maneja señales de parada."""
    global running
    logger.info("🛑 Señal de parada recibida...")
    running = False

# Configurar señales
signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)

# Mediciones DLMS estándar
MEASUREMENTS = {
    "voltage_l1": ("1-0:32.7.0*255", "Voltaje Fase A", "V"),
    "current_l1": ("1-0:31.7.0*255", "Corriente Fase A", "A"),
    "frequency": ("1-0:14.7.0*255", "Frecuencia", "Hz"),
    "active_power": ("1-0:1.7.0*255", "Potencia Activa", "W"),
    "active_energy": ("1-0:1.8.0*255", "Energía Activa", "Wh"),
}


class GatewayDLMSBridge:
    """Bridge entre medidor DLMS y ThingsBoard Gateway."""
    
    def __init__(self, meter_id: int = 1, polling_interval: float = 5.0):
        """
        Inicializa el bridge.
        
        Args:
            meter_id: ID del medidor en la base de datos
            polling_interval: Intervalo de polling en segundos
        """
        self.meter_id = meter_id
        self.polling_interval = polling_interval
        
        # Cargar configuración del medidor desde database
        db = Database('data/admin.db')
        session = db.get_session()
        
        meter = get_meter_by_id(session, meter_id)
        if not meter:
            session.close()
            raise ValueError(f"Medidor ID {meter_id} no encontrado en database")
        
        # Configuración DLMS
        self.dlms_host = meter.ip_address
        self.dlms_port = meter.port
        self.dlms_password = "22222222"  # Password por defecto DLMS
        
        # Configuración ThingsBoard Gateway
        self.gateway_token = meter.tb_token
        self.gateway_host = meter.tb_host or "localhost"
        self.gateway_port = meter.tb_port or 1883
        self.device_name = meter.name
        
        session.close()
        
        # Clientes
        self.dlms_client: Optional[DlmsClient] = None
        self.mqtt_client: Optional[ThingsBoardMQTTClient] = None
        
        # Métricas
        self.total_cycles = 0
        self.successful_cycles = 0
        self.failed_cycles = 0
        
        logger.info("=" * 70)
        logger.info("🌉 Gateway DLMS Bridge - ThingsBoard")
        logger.info("=" * 70)
        logger.info(f"📊 Dispositivo: {self.device_name}")
        logger.info(f"🔌 DLMS: {self.dlms_host}:{self.dlms_port}")
        logger.info(f"🌐 Gateway: {self.gateway_host}:{self.gateway_port}")
        logger.info(f"📈 Polling: {self.polling_interval}s")
        logger.info("=" * 70)
    
    def connect_dlms(self) -> bool:
        """Conecta al medidor DLMS."""
        try:
            logger.info(f"🔌 Conectando a medidor DLMS {self.dlms_host}:{self.dlms_port}...")
            
            # Importar módulos necesarios
            from dlms_cosem import security
            
            # Crear IO TCP
            io = BlockingTcpIO(host=self.dlms_host, port=self.dlms_port, timeout=10)
            
            # Crear transporte TCP
            transport = TcpTransport(
                client_logical_address=1,
                server_logical_address=1,
                io=io,
                timeout=10
            )
            
            # Conectar el transporte
            transport.connect()
            
            # Crear autenticación
            authentication = security.LowLevelSecurityAuthentication(
                secret=self.dlms_password.encode('ascii')
            )
            
            # Crear cliente DLMS
            self.dlms_client = DlmsClient(
                transport=transport,
                authentication=authentication
            )
            
            # Asociar (conectar)
            self.dlms_client.associate()
            
            logger.info("✅ Conectado al medidor DLMS")
            return True
            
        except Exception as e:
            logger.error(f"❌ Error conectando a DLMS: {e}")
            self.dlms_client = None
            return False
    
    def connect_mqtt(self) -> bool:
        """Conecta al Gateway MQTT de ThingsBoard."""
        try:
            logger.info(f"🔌 Conectando al Gateway MQTT {self.gateway_host}:{self.gateway_port}...")
            
            self.mqtt_client = ThingsBoardMQTTClient(
                host=self.gateway_host,
                port=self.gateway_port,
                access_token=self.gateway_token,
                client_id=f"dlms_bridge_{self.device_name}"
            )
            
            if self.mqtt_client.connect(timeout=10.0):
                logger.info("✅ Conectado al Gateway MQTT")
                
                # Publicar attributes del dispositivo
                attributes = {
                    "device_type": "DLMS Energy Meter",
                    "dlms_host": self.dlms_host,
                    "dlms_port": self.dlms_port,
                    "polling_interval": self.polling_interval,
                    "bridge_version": "2.0.0"
                }
                self.mqtt_client.publish_attributes(attributes)
                
                return True
            else:
                logger.error("❌ No se pudo conectar al Gateway MQTT")
                return False
                
        except Exception as e:
            logger.error(f"❌ Error conectando a MQTT: {e}")
            self.mqtt_client = None
            return False
    
    def read_measurement(self, measurement_key: str) -> Optional[float]:
        """
        Lee una medición del medidor DLMS.
        
        Args:
            measurement_key: Clave de la medición (ej: "voltage_l1")
            
        Returns:
            Valor leído o None si falla
        """
        if not self.dlms_client:
            return None
        
        if measurement_key not in MEASUREMENTS:
            logger.warning(f"⚠️ Medición desconocida: {measurement_key}")
            return None
        
        obis_code, description, unit = MEASUREMENTS[measurement_key]
        
        try:
            # Leer objeto COSEM usando obis code
            # Formato: "1-0:32.7.0*255" -> instancia Register
            # Atributo 2 = value
            
            # Convertir OBIS string a CosemAttribute
            # DLMS/COSEM: Interface=3 (Register), Attribute=2 (value)
            cosem_object = cosem.CosemAttribute(
                interface=enumerations.CosemInterface.REGISTER,
                instance=cosem.Obis.from_string(obis_code),
                attribute=2  # value
            )
            
            # Leer valor
            result = self.dlms_client.get(cosem_object)
            
            if result is None:
                return None
            
            # El resultado puede ser un objeto complejo, extraer valor
            if hasattr(result, 'value'):
                value = result.value
            else:
                value = result
            
            return float(value)
            
        except Exception as e:
            logger.debug(f"⚠️ Error leyendo {measurement_key} ({obis_code}): {e}")
            return None
    
    def poll_once(self) -> Dict[str, Optional[float]]:
        """Realiza un ciclo de polling."""
        results = {}
        
        for key in MEASUREMENTS.keys():
            value = self.read_measurement(key)
            results[key] = value
        
        return results
    
    def publish_telemetry(self, data: Dict[str, float]) -> bool:
        """Publica telemetría al Gateway MQTT."""
        if not self.mqtt_client:
            return False
        
        try:
            # Filtrar valores None
            clean_data = {k: v for k, v in data.items() if v is not None}
            
            if not clean_data:
                logger.warning("⚠️ No hay datos válidos para publicar")
                return False
            
            # Publicar
            success = self.mqtt_client.publish_telemetry(clean_data)
            
            if success:
                # Log resumido
                values_str = " | ".join([
                    f"{k.upper()[:3]}: {v:7.2f}" for k, v in clean_data.items()
                ])
                logger.info(f"📤 {values_str}")
            
            return success
            
        except Exception as e:
            logger.error(f"❌ Error publicando telemetría: {e}")
            return False
    
    def run(self):
        """Ejecuta el bridge en loop continuo."""
        global running
        
        # Conectar
        if not self.connect_dlms():
            logger.error("❌ No se pudo conectar a DLMS. Abortando.")
            return 1
        
        if not self.connect_mqtt():
            logger.error("❌ No se pudo conectar a MQTT. Abortando.")
            return 1
        
        logger.info("🚀 Iniciando polling (Ctrl+C para detener)")
        logger.info("")
        
        consecutive_errors = 0
        max_consecutive_errors = 10
        
        try:
            while running:
                cycle_start = time.time()
                self.total_cycles += 1
                
                # Leer datos
                data = self.poll_once()
                
                # Publicar
                if any(v is not None for v in data.values()):
                    if self.publish_telemetry(data):
                        self.successful_cycles += 1
                        consecutive_errors = 0
                    else:
                        self.failed_cycles += 1
                        consecutive_errors += 1
                else:
                    logger.warning(f"⚠️ Ciclo {self.total_cycles}: Sin datos válidos")
                    self.failed_cycles += 1
                    consecutive_errors += 1
                
                # Circuit breaker
                if consecutive_errors >= max_consecutive_errors:
                    logger.error(f"⚡ {consecutive_errors} errores consecutivos. Reconectando...")
                    
                    # Reconectar DLMS
                    if self.dlms_client:
                        try:
                            self.dlms_client.release()
                        except:
                            pass
                    
                    if not self.connect_dlms():
                        logger.error("❌ No se pudo reconectar a DLMS. Abortando.")
                        break
                    
                    consecutive_errors = 0
                
                # Estadísticas cada 20 ciclos
                if self.total_cycles % 20 == 0:
                    success_rate = (self.successful_cycles / self.total_cycles * 100) if self.total_cycles > 0 else 0
                    logger.info(f"📊 Estadísticas: {self.successful_cycles}/{self.total_cycles} exitosos ({success_rate:.1f}%)")
                
                # Esperar próximo ciclo
                elapsed = time.time() - cycle_start
                sleep_time = max(0, self.polling_interval - elapsed)
                if sleep_time > 0 and running:
                    time.sleep(sleep_time)
        
        except Exception as e:
            logger.error(f"❌ Error fatal: {e}")
            import traceback
            traceback.print_exc()
        
        finally:
            logger.info("")
            logger.info("🛑 Deteniendo bridge...")
            
            # Cerrar conexiones
            if self.dlms_client:
                try:
                    self.dlms_client.release()
                    logger.info("✅ Conexión DLMS cerrada")
                except:
                    pass
            
            if self.mqtt_client:
                try:
                    self.mqtt_client.disconnect()
                    logger.info("✅ Conexión MQTT cerrada")
                except:
                    pass
            
            # Estadísticas finales
            success_rate = (self.successful_cycles / self.total_cycles * 100) if self.total_cycles > 0 else 0
            logger.info("")
            logger.info("=" * 70)
            logger.info("📊 ESTADÍSTICAS FINALES")
            logger.info("=" * 70)
            logger.info(f"  Ciclos totales:    {self.total_cycles}")
            logger.info(f"  Exitosos:          {self.successful_cycles}")
            logger.info(f"  Fallidos:          {self.failed_cycles}")
            logger.info(f"  Tasa de éxito:     {success_rate:.1f}%")
            logger.info("=" * 70)
        
        return 0


def main():
    """Función principal."""
    import argparse
    
    parser = argparse.ArgumentParser(description="Bridge DLMS → ThingsBoard Gateway")
    parser.add_argument("--meter-id", type=int, default=1, help="ID del medidor en database")
    parser.add_argument("--interval", type=float, default=5.0, help="Intervalo de polling (segundos)")
    
    args = parser.parse_args()
    
    try:
        bridge = GatewayDLMSBridge(
            meter_id=args.meter_id,
            polling_interval=args.interval
        )
        return bridge.run()
    except Exception as e:
        logger.error(f"❌ Error inicializando bridge: {e}")
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())
