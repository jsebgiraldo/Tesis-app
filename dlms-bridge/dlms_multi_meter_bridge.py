#!/usr/bin/env python3
"""
DLMS Multi-Meter Bridge Service
Scalable service that manages multiple DLMS meters concurrently
- Single process handles all meters
- Async/concurrent connections
- Database-driven configuration
- Centralized MQTT publishing
- Efficient resource usage
"""

import asyncio
import argparse
import logging
import signal
import sys
import time
from datetime import datetime, timedelta
from typing import Dict, List, Optional
from pathlib import Path

# Add project root to path
sys.path.insert(0, str(Path(__file__).parent))

from admin.database import Database, get_all_meters, get_meter_by_id, record_metric, create_alarm, update_meter_status, record_dlms_diagnostic, db
from dlms_poller_production import ProductionDLMSPoller
from tb_mqtt_client import ThingsBoardMQTTClient

# Configure logging (MEJORADO: INFO para reducir I/O)
logging.basicConfig(
    level=logging.INFO,  # Cambiado de DEBUG a INFO para reducir overhead
    format='%(asctime)s - [%(name)s] - %(levelname)s - %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S'
)
logger = logging.getLogger('MultiMeterBridge')

# Initialize network monitor after logger is configured
from network_monitor import get_network_monitor
network_monitor = get_network_monitor()
logger.info("✓ Network monitor initialized")


class MeterWorker:
    """Worker that handles a single DLMS meter"""
    
    def __init__(self, meter_id: int, config: Dict):
        self.meter_id = meter_id
        self.meter_name = config['meter_name']
        self.config = config
        self.mqtt_client: Optional[ThingsBoardMQTTClient] = None  # ✅ SDK oficial ThingsBoard
        self.poller: Optional[ProductionDLMSPoller] = None
        self.running = False
        self.task: Optional[asyncio.Task] = None
        
        # Statistics
        self.total_cycles = 0
        self.successful_cycles = 0
        self.failed_cycles = 0
        self.total_messages_sent = 0
        self.start_time = datetime.now()
        
        # Watchdog para errores HDLC (MEJORADO: threshold más tolerante)
        self.consecutive_hdlc_errors = 0
        self.max_consecutive_hdlc_errors = 15  # Aumentado de 5 a 15 para reducir reconexiones
        self.consecutive_read_failures = 0  # NUEVO: Contador para readings=None
        self.max_consecutive_read_failures = 10  # NUEVO: Máximo 10 fallos de lectura consecutivos
        self.last_successful_read = datetime.now()
        self.max_silence_minutes = 10  # Reiniciar si no hay lecturas exitosas en 10 minutos
        
        # Circuit Breaker: Previene loops infinitos de reconexión
        self.reconnect_history = []  # Lista de timestamps de reconexiones
        self.max_reconnects_per_hour = 10  # Máximo 10 reconexiones por hora
        self.circuit_breaker_pause_minutes = 5  # Pausa de 5 minutos si se excede
        self.circuit_breaker_active = False
        self.circuit_breaker_until = None
        
        # Control de ciclo de vida DLMS
        self.use_persistent_connection = False  # Cambiar a False para cerrar/abrir por ciclo
        self.last_connection_time = None
        self.connection_max_age_minutes = 30  # Reconectar cada 30 minutos aunque funcione
        
        self.logger = logging.getLogger(f'Meter[{meter_id}:{self.meter_name}]')
    
    def _check_circuit_breaker(self) -> bool:
        """
        Verifica si el circuit breaker está activo y decide si permitir reconexión.
        
        Returns:
            True si se puede reconectar, False si circuit breaker está activo
        """
        now = datetime.now()
        
        # Limpiar reconexiones antiguas (más de 1 hora)
        self.reconnect_history = [
            t for t in self.reconnect_history 
            if (now - t).total_seconds() < 3600
        ]
        
        # Verificar si se excedió el límite
        if len(self.reconnect_history) >= self.max_reconnects_per_hour:
            if not self.circuit_breaker_active:
                # Activar circuit breaker
                self.circuit_breaker_active = True
                self.circuit_breaker_until = now + timedelta(minutes=self.circuit_breaker_pause_minutes)
                self.logger.error(
                    f"🔴 Circuit Breaker ACTIVADO: {len(self.reconnect_history)} reconexiones en última hora "
                    f"(límite: {self.max_reconnects_per_hour}). Pausa hasta {self.circuit_breaker_until.strftime('%H:%M:%S')}"
                )
                try:
                    with db.get_session() as session:
                        create_alarm(session, self.meter_id, 'critical', 'circuit_breaker', 
                                   f'Circuit breaker activado: {len(self.reconnect_history)} reconexiones/hora')
                except:
                    pass
            
            # Verificar si ya pasó el tiempo de pausa
            if now >= self.circuit_breaker_until:
                self.circuit_breaker_active = False
                self.reconnect_history.clear()
                self.logger.info(f"🟢 Circuit Breaker DESACTIVADO - reiniciando contadores")
                return True
            
            # Circuit breaker todavía activo
            remaining = (self.circuit_breaker_until - now).total_seconds() / 60
            self.logger.warning(f"⏸️  Circuit breaker activo: esperando {remaining:.1f} minutos más")
            return False
        
        # Registrar esta reconexión
        self.reconnect_history.append(now)
        self.logger.debug(f"📊 Circuit breaker: {len(self.reconnect_history)}/{self.max_reconnects_per_hour} reconexiones en última hora")
        
        return True
    
    async def _setup_mqtt(self) -> bool:
        """Setup individual MQTT client for this meter using ThingsBoard SDK or local broker"""
        try:
            # Get ThingsBoard config for this meter
            tb_host = self.config.get('tb_host', 'localhost')
            tb_port = self.config.get('tb_port', 1883)
            tb_token = self.config.get('tb_token', '')
            
            # Check if using local broker (no token) or ThingsBoard direct (with token)
            if not tb_token and tb_port == 1884:
                # Local broker mode (Gateway architecture)
                self.logger.info(f"🔌 Connecting to local broker {tb_host}:{tb_port} (no auth, via Gateway)")
                import paho.mqtt.client as mqtt
                
                self.mqtt_client = mqtt.Client(
                    client_id=f"dlms_meter_{self.meter_id}",
                    clean_session=True,
                    protocol=mqtt.MQTTv311
                )
                
                # Connect to local broker
                connected = await asyncio.to_thread(
                    self.mqtt_client.connect,
                    tb_host,
                    tb_port,
                    60  # keepalive
                )
                
                # Start loop
                self.mqtt_client.loop_start()
                
                # Store flag to know we're using raw MQTT
                self._using_raw_mqtt = True
                
                self.logger.info(f"✅ Connected to local broker (Gateway will forward to ThingsBoard)")
                return True
                
            elif tb_token:
                # ThingsBoard direct mode (legacy, with token)
                self.logger.info(f"🔌 Connecting MQTT to {tb_host}:{tb_port} with ThingsBoard SDK")
                
                # Create ThingsBoard MQTT client using official SDK
                self.mqtt_client = ThingsBoardMQTTClient(
                    host=tb_host,
                    port=tb_port,
                    token=tb_token,
                    client_id=f"dlms_meter_{self.meter_id}"
                )
                
                # Connect with automatic reconnection
                connected = await asyncio.to_thread(
                    self.mqtt_client.connect,
                    timeout=30,
                    keepalive=90
                )
                
                # Store flag to know we're using ThingsBoard SDK
                self._using_raw_mqtt = False
                
                if connected:
                    self.logger.info(f"✅ MQTT client ready for meter {self.meter_id} (Token: {tb_token[:10]}...{tb_token[-4:]})")
                    return True
                else:
                    self.logger.error(f"❌ MQTT connection failed for meter {self.meter_id}")
                    return False
            else:
                self.logger.error("❌ Invalid MQTT config: no token and port != 1884")
                return False
            
        except Exception as e:
            self.logger.error(f"❌ Failed to setup MQTT: {e}")
            import traceback
            self.logger.error(traceback.format_exc())
            return False
        
    def create_poller(self):
        """Create DLMS poller for this meter"""
        try:
            self.poller = ProductionDLMSPoller(
                host=self.config['dlms_host'],
                port=self.config['dlms_port'],
                measurements=self.config['measurements'],
                interval=self.config.get('interval', 1.0),
                verbose=False
            )
            self.logger.info(f"✓ Poller created for {self.config['dlms_host']}:{self.config['dlms_port']}")
            return True
        except Exception as e:
            self.logger.error(f"❌ Failed to create poller: {e}")
            return False
    
    async def poll_and_publish(self):
        """Main polling loop for this meter with watchdog and connection lifecycle"""
        self.logger.info(f"🚀 Starting polling loop (interval: {self.config.get('interval', 1.0)}s)")
        
        while self.running:
            try:
                # WATCHDOG: Verificar si necesitamos reconectar por timeout de silencio
                time_since_success = (datetime.now() - self.last_successful_read).total_seconds() / 60
                if time_since_success > self.max_silence_minutes:
                    self.logger.error(f"🚨 WATCHDOG: Sin lecturas exitosas por {time_since_success:.1f} minutos. Reconectando...")
                    try:
                        with db.get_session() as session:
                            create_alarm(session, self.meter_id, 'critical', 'watchdog', 
                                       f'Sin lecturas exitosas por {time_since_success:.1f} minutos - reconexión forzada')
                    except:
                        pass
                    
                    # Reiniciar conexión DLMS
                    await self._restart_dlms_connection()
                    self.consecutive_hdlc_errors = 0
                    self.last_successful_read = datetime.now()
                    continue
                
                # WATCHDOG: Verificar si hay demasiados errores HDLC consecutivos
                if self.consecutive_hdlc_errors >= self.max_consecutive_hdlc_errors:
                    self.logger.error(f"🚨 WATCHDOG: {self.consecutive_hdlc_errors} errores HDLC consecutivos. Reiniciando conexión...")
                    
                    # Verificar circuit breaker antes de reconectar
                    if not self._check_circuit_breaker():
                        await asyncio.sleep(60)  # Esperar 1 min si circuit breaker bloquea
                        continue
                    
                    try:
                        with db.get_session() as session:
                            create_alarm(session, self.meter_id, 'critical', 'watchdog', 
                                       f'{self.consecutive_hdlc_errors} errores HDLC consecutivos - reinicio forzado')
                    except:
                        pass
                    
                    await self._restart_dlms_connection()
                    self.consecutive_hdlc_errors = 0
                    continue
                
                # CICLO DE VIDA: Verificar edad de la conexión
                if self.last_connection_time:
                    connection_age_minutes = (datetime.now() - self.last_connection_time).total_seconds() / 60
                    if connection_age_minutes > self.connection_max_age_minutes:
                        self.logger.info(f"♻️  Reconexión preventiva: conexión tiene {connection_age_minutes:.1f} minutos")
                        
                        # Verificar circuit breaker (reconexiones preventivas no cuentan tanto)
                        if not self._check_circuit_breaker():
                            self.logger.warning("Circuit breaker bloqueó reconexión preventiva, continuando con conexión existente")
                            self.last_connection_time = datetime.now()  # Reset timer
                            # NO continuar, seguir usando conexión actual
                        else:
                            await self._restart_dlms_connection()
                            continue
                
                # Poll readings
                readings = await asyncio.to_thread(self.poller.poll_once)
                
                self.total_cycles += 1
                
                # DIAGNÓSTICO: Log detallado de readings
                self.logger.debug(f"🔍 poll_once() returned: {readings}")
                
                if readings and any(v is not None for v in readings.values()):
                    self.successful_cycles += 1
                    self.last_successful_read = datetime.now()
                    self.consecutive_hdlc_errors = 0  # Reset contador de errores HDLC
                    self.consecutive_read_failures = 0  # NUEVO: Reset contador de fallos de lectura
                    
                    # Check MQTT connection status
                    if hasattr(self, '_using_raw_mqtt') and self._using_raw_mqtt:
                        # Using raw MQTT (local broker mode)
                        mqtt_connected = self.mqtt_client and self.mqtt_client.is_connected()
                    else:
                        # Using ThingsBoard SDK
                        mqtt_connected = self.mqtt_client and self.mqtt_client.is_connected()
                    
                    self.logger.debug(f"🔍 MQTT connected: {mqtt_connected}")
                    
                    # Publish to MQTT
                    if mqtt_connected:
                        telemetry = {}
                        for key, value in readings.items():
                            if key != 'timestamp' and value is not None:
                                try:
                                    telemetry[key] = float(value)
                                except (ValueError, TypeError) as e:
                                    self.logger.warning(f"⚠️  Error converting {key}={value}: {e}")
                                    telemetry[key] = str(value)
                        
                        self.logger.debug(f"🔍 Telemetry built: {telemetry}")
                        
                        if telemetry:
                            # Publish based on mode
                            if hasattr(self, '_using_raw_mqtt') and self._using_raw_mqtt:
                                # Raw MQTT mode: publish to local broker
                                import json
                                payload = {
                                    "ts": int(time.time() * 1000),
                                    "values": telemetry
                                }
                                result = self.mqtt_client.publish(
                                    "v1/devices/me/telemetry",
                                    json.dumps(payload),
                                    qos=1
                                )
                                success = result.rc == 0
                            else:
                                # ThingsBoard SDK mode
                                success = await asyncio.to_thread(
                                    self.mqtt_client.publish_telemetry,
                                    telemetry,
                                    wait=False  # Non-blocking publish
                                )
                            
                            if success:
                                self.total_messages_sent += 1
                                # Track MQTT network usage
                                mqtt_bytes = len(str(telemetry).encode('utf-8'))
                                try:
                                    network_monitor.record_mqtt_message(mqtt_bytes)
                                    self.logger.info(f"📤 Published + tracked: {mqtt_bytes} bytes MQTT (total: {network_monitor.app_stats['mqtt_messages_sent']} msgs)")
                                except Exception as e:
                                    self.logger.warning(f"Failed to record MQTT metrics: {e}")
                            else:
                                self.logger.warning(f"⚠️  MQTT publish failed")
                        else:
                            self.logger.warning(f"⚠️  Telemetry empty, skipping MQTT publish")
                    else:
                        self.logger.warning(f"⚠️  MQTT not connected, skipping publish")
                    
                    # Log summary every 10 cycles
                    if self.total_cycles % 10 == 0:
                        success_rate = (self.successful_cycles / self.total_cycles * 100) if self.total_cycles > 0 else 0
                        mqtt_rate = (self.total_messages_sent / self.total_cycles * 100) if self.total_cycles > 0 else 0
                        
                        self.logger.info(
                            f"📊 Cycles: {self.total_cycles} | "
                            f"Success: {success_rate:.1f}% | "
                            f"MQTT: {self.total_messages_sent} msgs ({mqtt_rate:.1f}%)"
                        )
                        
                        # Alerta si MQTT rate es bajo
                        if mqtt_rate < 50 and self.total_cycles >= 20:
                            self.logger.error(
                                f"🔴 ALERTA: Solo {mqtt_rate:.1f}% de ciclos publican a MQTT. "
                                f"Verificar conflictos de token MQTT."
                            )
                    
                    # Heartbeat: Actualizar last_seen cada 60 ciclos (~3 minutos)
                    if self.total_cycles % 60 == 0:
                        try:
                            from admin.database import Database
                            import os
                            db_inst = Database(self.config.get('db_path', 'data/admin.db'))
                            session = db_inst.get_session()
                            update_meter_status(
                                session,
                                self.meter_id,
                                status='active',
                                process_id=os.getpid()
                            )
                            session.close()
                            self.logger.debug(f"💓 Heartbeat: Updated last_seen")
                        except Exception as e:
                            self.logger.debug(f"Failed heartbeat update: {e}")
                else:
                    self.failed_cycles += 1
                    self.consecutive_read_failures += 1  # NUEVO: Incrementar contador de fallos
                    self.logger.warning(f"⚠️  No readings returned (failures: {self.failed_cycles}, consecutive: {self.consecutive_read_failures}/{self.max_consecutive_read_failures})")
                    
                    # NUEVO: Watchdog para "No readings returned"
                    if self.consecutive_read_failures >= self.max_consecutive_read_failures:
                        self.logger.error(f"🚨 WATCHDOG: {self.consecutive_read_failures} fallos de lectura consecutivos. Reiniciando conexión...")
                        
                        # Verificar circuit breaker antes de reconectar
                        if not self._check_circuit_breaker():
                            await asyncio.sleep(60)  # Esperar 1 min si circuit breaker bloquea
                            continue
                        
                        try:
                            with db.get_session() as session:
                                create_alarm(session, self.meter_id, 'critical', 'watchdog', 
                                           f'{self.consecutive_read_failures} fallos de lectura consecutivos - reinicio forzado')
                        except:
                            pass
                        
                        await self._restart_dlms_connection()
                        self.consecutive_read_failures = 0
                        continue
                
                # Wait for next interval
                await asyncio.sleep(self.config.get('interval', 1.0))
                
            except asyncio.CancelledError:
                self.logger.info("🛑 Polling cancelled")
                break
            except Exception as e:
                self.failed_cycles += 1
                # Persist DLMS-specific errors for later analysis
                err_text = str(e)
                self.logger.error(f"❌ Error in poll cycle: {err_text}", exc_info=True)
                
                # Detectar errores HDLC y actualizar watchdog
                lc = err_text.lower()
                is_hdlc_error = 'hdlc' in lc or 'invalid hdlc' in lc or 'unterminated' in lc or 'frame boundary' in lc
                is_sequence_error = 'unexpected receive sequence' in lc or 'sequence' in lc
                
                if is_hdlc_error:
                    self.consecutive_hdlc_errors += 1
                    self.logger.warning(f"⚠️  Error HDLC detectado ({self.consecutive_hdlc_errors}/{self.max_consecutive_hdlc_errors})")
                    
                    # C3: Si es error de secuencia, intentar reset explícito
                    if is_sequence_error and self.poller:
                        self.logger.info("🔄 Reset de secuencia HDLC detectado, limpiando estado...")
                        try:
                            # Forzar limpieza del poller
                            if hasattr(self.poller, 'original_client') and self.poller.original_client:
                                client = self.poller.original_client
                                # C3: Reset de contadores de secuencia
                                if hasattr(client, '_send_seq'):
                                    client._send_seq = 0
                                if hasattr(client, '_recv_seq'):
                                    client._recv_seq = 0
                                # C4: Limpiar buffer
                                if hasattr(client, '_chunk_buffer'):
                                    client._chunk_buffer = b''
                                self.logger.info("✓ Secuencia HDLC reseteada")
                        except Exception as reset_err:
                            self.logger.warning(f"Error reseteando secuencia: {reset_err}")
                
                try:
                    # If error looks like HDLC/protocol issue, record diagnostic
                    if is_hdlc_error:
                        try:
                            with db.get_session() as session:
                                record_dlms_diagnostic(
                                    session,
                                    meter_id=self.meter_id,
                                    category='hdlc',
                                    message=err_text,
                                    severity='error',
                                    raw_frame=None
                                )
                                # Also create an alarm so operators see it quickly
                                create_alarm(session, self.meter_id, 'error', 'connection', f'HDLC error: {err_text}')
                        except Exception as inner_db_e:
                            self.logger.warning(f"Failed to record DLMS diagnostic: {inner_db_e}")
                except Exception:
                    pass
                await asyncio.sleep(5)  # Wait before retry
    
    async def _restart_dlms_connection(self):
        """Reinicia la conexión DLMS de forma limpia"""
        self.logger.info("♻️  Reiniciando conexión DLMS...")
        
        try:
            # Cerrar conexión existente de forma limpia
            if self.poller and self.poller.original_client:
                try:
                    await asyncio.to_thread(self.poller.original_client.close)
                    self.logger.debug("✓ Conexión DLMS cerrada")
                except Exception as e:
                    self.logger.warning(f"Error cerrando conexión: {e}")
                
                # Esperar un momento para que el medidor libere la sesión
                await asyncio.sleep(2.0)
            
            # Reconectar
            connected = await asyncio.to_thread(self.poller._connect_with_recovery)
            
            if connected:
                self.last_connection_time = datetime.now()
                self.logger.info("✅ Conexión DLMS reiniciada exitosamente")
            else:
                self.logger.error("❌ Fallo al reiniciar conexión DLMS")
                # Intentar recrear el poller completamente
                self.create_poller()
                connected = await asyncio.to_thread(self.poller._connect_with_recovery)
                if connected:
                    self.last_connection_time = datetime.now()
                    self.logger.info("✅ Poller recreado y conectado")
        
        except Exception as e:
            self.logger.error(f"❌ Error en reinicio de conexión: {e}")
    
    
    async def start(self):
        """Start the meter worker"""
        self.running = True
        
        # ✅ 1. Crear cliente MQTT individual para este medidor
        self.logger.info("🔌 Initializing individual MQTT client...")
        if not await self._setup_mqtt():
            self.logger.error("❌ Cannot start worker without MQTT")
            return False
        
        # 2. Create poller
        if not self.create_poller():
            self.logger.error("❌ Cannot start worker without poller")
            return False
        
        # 3. Connect to meter
        self.logger.info("🔌 Connecting to DLMS meter...")
        connected = await asyncio.to_thread(self.poller._connect_with_recovery)
        
        if not connected:
            self.logger.error("❌ Failed to connect to meter")
            return False
        
        self.last_connection_time = datetime.now()
        self.logger.info("✅ Connected to DLMS meter")
        
        # Update database status to active
        import os
        from admin.database import Database
        try:
            db = Database(self.config.get('db_path', 'data/admin.db'))
            session = db.get_session()
            update_meter_status(
                session, 
                self.meter_id, 
                status='active',
                process_id=os.getpid(),
                error_count=0
            )
            session.close()
            self.logger.info(f"✓ Updated meter status to 'active' (PID: {os.getpid()})")
        except Exception as e:
            self.logger.warning(f"⚠️ Failed to update meter status in database: {e}")
        
        # Start polling task
        self.task = asyncio.create_task(self.poll_and_publish())
        return True
    
    async def stop(self):
        """Stop the meter worker"""
        self.logger.info("⏹️  Stopping worker...")
        self.running = False
        
        if self.task:
            self.task.cancel()
            try:
                await self.task
            except asyncio.CancelledError:
                pass
        
        if self.poller:
            try:
                await asyncio.to_thread(self.poller.stop)
            except Exception as e:
                self.logger.error(f"Error stopping poller: {e}")
        
        # Disconnect MQTT client using SDK
        if self.mqtt_client:
            try:
                await asyncio.to_thread(self.mqtt_client.stop)
                self.logger.info("✓ MQTT client disconnected via SDK")
            except Exception as e:
                self.logger.error(f"Error disconnecting MQTT: {e}")
        
        # Update database status to inactive
        from admin.database import Database
        try:
            db = Database(self.config.get('db_path', 'data/admin.db'))
            session = db.get_session()
            update_meter_status(
                session, 
                self.meter_id, 
                status='inactive',
                process_id=None
            )
            session.close()
            self.logger.debug("✓ Updated meter status to 'inactive'")
        except Exception as e:
            self.logger.warning(f"⚠️ Failed to update meter status in database: {e}")
        
        self.logger.info("✓ Worker stopped")
    
    def get_stats(self) -> Dict:
        """Get worker statistics"""
        runtime = (datetime.now() - self.start_time).total_seconds()
        success_rate = (self.successful_cycles / self.total_cycles * 100) if self.total_cycles > 0 else 0
        
        return {
            'meter_id': self.meter_id,
            'meter_name': self.meter_name,
            'total_cycles': self.total_cycles,
            'successful_cycles': self.successful_cycles,
            'failed_cycles': self.failed_cycles,
            'success_rate': success_rate,
            'messages_sent': self.total_messages_sent,
            'runtime_seconds': runtime,
            'running': self.running
        }


class MultiMeterBridge:
    """Main service that manages multiple meter workers"""
    
    def __init__(self, db_path: str = "data/admin.db"):
        self.db_path = db_path
        self.db = Database(db_path)
        self.workers: Dict[int, MeterWorker] = {}
        # ✅ Remover mqtt_client compartido - cada worker tiene el suyo
        self.running = False
        
        logger.info(f"🏗️  Multi-Meter Bridge initialized (DB: {db_path})")
        logger.info(f"   Architecture: Individual MQTT per meter (QoS=1)")
    
    def load_meters_from_db(self) -> List[Dict]:
        """Load all active meters from database"""
        self.db.initialize()
        
        with self.db.get_session() as session:
            meters = get_all_meters(session)
            
            logger.info(f"📊 Found {len(meters)} meters in database")
            
            configs = []
            for meter in meters:
                # Get enabled measurements
                measurements = [cfg.measurement_name for cfg in meter.configs if cfg.enabled]
                
                if not measurements:
                    logger.warning(f"⚠️  Meter {meter.id} ({meter.name}) has no enabled measurements, skipping")
                    continue
                
                if not meter.tb_enabled:
                    logger.info(f"ℹ️  Meter {meter.id} ({meter.name}) has ThingsBoard disabled, skipping")
                    continue
                
                # Get sampling interval from MeterConfig
                # Optimized to 2.0s for maximum performance while maintaining reliability
                sampling_interval = 2.0  # Optimal: 30 readings/minute, safe margin for DLMS
                if meter.configs:
                    # Use first config's sampling interval
                    sampling_interval = meter.configs[0].sampling_interval
                
                config = {
                    'meter_id': meter.id,
                    'meter_name': meter.name,
                    'dlms_host': meter.ip_address,
                    'dlms_port': meter.port,
                    'measurements': measurements,
                    'interval': sampling_interval,  # C2: Usa interval de BD (5.0s) en vez de hardcoded 3.0s
                    'tb_enabled': meter.tb_enabled,
                    'tb_host': meter.tb_host,
                    'tb_port': meter.tb_port,
                    'tb_token': meter.tb_token,
                    'db_path': self.db.db_path  # Pass database path to worker
                }
                
                configs.append(config)
                logger.info(
                    f"  ✓ Meter {meter.id}: {meter.name} @ {meter.ip_address}:{meter.port} "
                    f"({len(measurements)} measurements)"
                )
            
            return configs
    
    async def start_workers(self, meter_configs: List[Dict]):
        """Start workers for all meters"""
        logger.info(f"🚀 Starting {len(meter_configs)} meter workers...")
        
        tasks = []
        for config in meter_configs:
            # ✅ Cada worker crea su propio cliente MQTT internamente
            worker = MeterWorker(
                meter_id=config['meter_id'],
                config=config  # Sin mqtt_client compartido
            )
            
            self.workers[config['meter_id']] = worker
            tasks.append(worker.start())
        
        # Start all workers concurrently
        results = await asyncio.gather(*tasks, return_exceptions=True)
        
        successful = sum(1 for r in results if r is True)
        logger.info(f"✅ Started {successful}/{len(meter_configs)} workers successfully")
    
    async def stop_workers(self):
        """Stop all workers"""
        logger.info(f"⏹️  Stopping {len(self.workers)} workers...")
        
        tasks = [worker.stop() for worker in self.workers.values()]
        await asyncio.gather(*tasks, return_exceptions=True)
        
        self.workers.clear()
        logger.info("✓ All workers stopped")
    
    async def monitor_loop(self):
        """Background monitoring and statistics"""
        logger.info("📊 Starting monitor loop (reporting every 60s)")
        
        while self.running:
            await asyncio.sleep(60)
            
            if not self.running:
                break
            
            # Collect statistics
            logger.info("=" * 70)
            logger.info("📊 SYSTEM STATUS REPORT")
            logger.info("=" * 70)
            
            for worker in self.workers.values():
                stats = worker.get_stats()
                logger.info(
                    f"  Meter {stats['meter_id']} ({stats['meter_name']}): "
                    f"Cycles={stats['total_cycles']}, "
                    f"Success={stats['success_rate']:.1f}%, "
                    f"MQTT={stats['messages_sent']}, "
                    f"Runtime={stats['runtime_seconds']:.0f}s"
                )
                
                # Save network metrics to database
                try:
                    from admin.database import Database, record_network_metric
                    
                    db = Database(self.db_path)
                    session = db.get_session()
                    
                    # Get network monitor stats
                    current_stats = network_monitor.get_current_stats()
                    app_stats = current_stats['application']
                    rate_stats = current_stats['rates']
                    
                    # Record network metrics
                    record_network_metric(
                        session,
                        meter_id=stats['meter_id'],
                        dlms_requests_sent=app_stats['dlms_requests_sent'],
                        dlms_responses_recv=app_stats['dlms_responses_recv'],
                        dlms_bytes_sent=app_stats['dlms_total_bytes_sent'],
                        dlms_bytes_recv=app_stats['dlms_total_bytes_recv'],
                        dlms_avg_payload=app_stats['dlms_avg_payload_size'],
                        mqtt_messages_sent=app_stats['mqtt_messages_sent'],
                        mqtt_bytes_sent=app_stats['mqtt_total_bytes_sent'],
                        bandwidth_tx_bps=rate_stats['bandwidth_tx_kbps'] * 1000,
                        bandwidth_rx_bps=rate_stats['bandwidth_rx_kbps'] * 1000,
                        packets_tx_ps=rate_stats['packets_tx_ps'],
                        packets_rx_ps=rate_stats['packets_rx_ps']
                    )
                    session.close()
                    
                    logger.info(
                        f"  └─ Network: DLMS {app_stats['dlms_requests_sent']} req, "
                        f"MQTT {app_stats['mqtt_messages_sent']} msg ({app_stats['mqtt_total_bytes_sent']} bytes)"
                    )
                except Exception as e:
                    logger.warning(f"  └─ Failed to save network metrics: {e}")
            
            logger.info("=" * 70)
    
    async def run(self):
        """Main service loop"""
        self.running = True
        
        try:
            # Load meters from database
            meter_configs = self.load_meters_from_db()
            
            if not meter_configs:
                logger.error("❌ No meters configured in database")
                return
            
            # ✅ Ya no se necesita setup_mqtt compartido
            # Cada worker configurará su propio cliente MQTT
            logger.info("✅ All workers will create individual MQTT connections")
            
            # Start all workers
            await self.start_workers(meter_configs)
            
            # Start monitor
            monitor_task = asyncio.create_task(self.monitor_loop())
            
            # Wait for shutdown signal
            logger.info("✅ Service running. Press Ctrl+C to stop.")
            
            try:
                await asyncio.Future()  # Run forever
            except asyncio.CancelledError:
                logger.info("🛑 Shutdown signal received")
            
            # Cleanup
            monitor_task.cancel()
            await self.stop_workers()
            
            # ✅ Ya no hay mqtt_client compartido para desconectar
            # Cada worker desconecta su propio cliente
            
        except Exception as e:
            logger.error(f"❌ Service error: {e}", exc_info=True)
        finally:
            self.running = False
            logger.info("✓ Service stopped")


def signal_handler(sig, frame):
    """Handle shutdown signals"""
    logger.info(f"🛑 Received signal {sig}, shutting down...")
    raise KeyboardInterrupt


def main():
    parser = argparse.ArgumentParser(description='DLMS Multi-Meter Bridge Service')
    parser.add_argument('--db-path', type=str, default='data/admin.db',
                        help='Path to database file')
    
    args = parser.parse_args()
    
    # Setup signal handlers
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    logger.info("=" * 70)
    logger.info("🌉 DLMS MULTI-METER BRIDGE SERVICE")
    logger.info("=" * 70)
    logger.info(f"Database: {args.db_path}")
    logger.info("Mode: Concurrent multi-meter management")
    logger.info("=" * 70)
    
    # Create and run service
    bridge = MultiMeterBridge(db_path=args.db_path)
    
    try:
        asyncio.run(bridge.run())
    except KeyboardInterrupt:
        logger.info("🛑 Service interrupted by user")
    except Exception as e:
        logger.error(f"❌ Fatal error: {e}", exc_info=True)
        sys.exit(1)
    
    logger.info("👋 Goodbye!")


if __name__ == "__main__":
    main()
