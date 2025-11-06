"""
Orchestrator Service - Multi-Meter Process Manager
Manages multiple DLMS poller processes and coordinates meter monitoring
"""

import multiprocessing as mp
import signal
import time
import logging
import sys
import os
from typing import Dict, Optional, List
from dataclasses import dataclass
from datetime import datetime, timedelta
from pathlib import Path
import psutil
import json

# Set multiprocessing start method to spawn to avoid inheritance issues
mp.set_start_method('spawn', force=True)

from admin.database import (
    Database, Meter, MeterMetric, Alarm,
    get_meter_by_id, get_all_meters, get_active_meters,
    create_alarm, record_metric
)

logger = logging.getLogger(__name__)


@dataclass
class PollerProcess:
    """Represents a running poller process"""
    meter_id: int
    process: mp.Process
    started_at: datetime
    last_heartbeat: datetime
    status: str  # 'starting', 'running', 'stopping', 'stopped', 'error'
    pid: Optional[int] = None


class MeterOrchestrator:
    """
    Orchestrates multiple DLMS meter pollers
    Manages process lifecycle, health monitoring, and metrics collection
    """
    
    def __init__(self, db_path: str = "data/admin.db"):
        self.db_path = db_path  # Store only the path, not the database object
        
        # Initialize database (create tables if needed)
        db = Database(db_path)
        db.initialize()
        
        # Process management
        self.pollers: Dict[int, PollerProcess] = {}  # meter_id -> PollerProcess
        self.running = False
        
        # Configuration
        self.health_check_interval = 10  # seconds
        self.restart_on_failure = True
        self.max_restart_attempts = 3
        
        # Metrics
        self.restart_counts: Dict[int, int] = {}  # meter_id -> restart count
        
        # Alarm throttling - track last alarm time per meter/category
        self.last_alarm_time: Dict[str, datetime] = {}  # "meter_id:category" -> datetime
        self.alarm_throttle_seconds = 300  # Only create alarm every 5 minutes for same issue
        
        logger.info("MeterOrchestrator initialized")
    
    def _should_create_alarm(self, meter_id: int, category: str) -> bool:
        """
        Check if we should create an alarm based on throttling rules
        
        Args:
            meter_id: Meter ID
            category: Alarm category
            
        Returns:
            True if alarm should be created
        """
        key = f"{meter_id}:{category}"
        now = datetime.now()
        
        if key not in self.last_alarm_time:
            self.last_alarm_time[key] = now
            return True
        
        time_since_last = (now - self.last_alarm_time[key]).total_seconds()
        if time_since_last >= self.alarm_throttle_seconds:
            self.last_alarm_time[key] = now
            return True
        
        return False
    
    def _create_throttled_alarm(self, session, meter_id: int, severity: str, 
                                category: str, message: str):
        """
        Create an alarm with throttling to prevent spam
        
        Args:
            session: Database session
            meter_id: Meter ID
            severity: Alarm severity
            category: Alarm category
            message: Alarm message
        """
        if self._should_create_alarm(meter_id, category):
            create_alarm(session, meter_id, severity, category, message)
            logger.info(f"Created alarm: [{severity}] {category} - {message}")
        else:
            logger.debug(f"Throttled alarm: [{severity}] {category} - {message}")
    
    def start(self):
        """Start the orchestrator"""
        logger.info("Starting MeterOrchestrator...")
        self.running = True
        
        # Start all active meters from database
        db = Database(self.db_path)
        with db.get_session() as session:
            active_meters = get_active_meters(session)
            for meter in active_meters:
                self._start_meter_poller(meter.id)
        
        logger.info(f"✓ Orchestrator started with {len(self.pollers)} active meters")
    
    def stop(self):
        """Stop the orchestrator and all pollers"""
        logger.info("Stopping MeterOrchestrator...")
        self.running = False
        
        # Stop all pollers
        for meter_id in list(self.pollers.keys()):
            self.stop_meter(meter_id)
        
        logger.info("✓ Orchestrator stopped")
    
    def start_meter(self, meter_id: int) -> bool:
        """
        Start polling for a specific meter
        
        Args:
            meter_id: Database ID of meter
            
        Returns:
            True if started successfully
        """
        db = Database(self.db_path)
        with db.get_session() as session:
            meter = get_meter_by_id(session, meter_id)
            if not meter:
                logger.error(f"Meter {meter_id} not found in database")
                return False
            
            if meter_id in self.pollers:
                logger.warning(f"Meter {meter_id} already running")
                return False
            
            # Update status to active
            meter.status = 'active'
            session.commit()
        
        return self._start_meter_poller(meter_id)
    
    def stop_meter(self, meter_id: int) -> bool:
        """
        Stop polling for a specific meter
        
        Args:
            meter_id: Database ID of meter
            
        Returns:
            True if stopped successfully
        """
        if meter_id not in self.pollers:
            logger.warning(f"Meter {meter_id} not running")
            return False
        
        poller = self.pollers[meter_id]
        
        try:
            logger.info(f"Stopping poller for meter {meter_id} (PID: {poller.pid})...")
            
            # Graceful shutdown
            if poller.process.is_alive():
                poller.process.terminate()
                poller.process.join(timeout=5)
                
                # Force kill if still alive
                if poller.process.is_alive():
                    logger.warning(f"Force killing poller for meter {meter_id}")
                    poller.process.kill()
                    poller.process.join(timeout=2)
            
            poller.status = 'stopped'
            
            # Update database
            db = Database(self.db_path)
            with db.get_session() as session:
                meter = get_meter_by_id(session, meter_id)
                if meter:
                    meter.status = 'inactive'
                    meter.process_id = None
                    session.commit()
            
            # Remove from active pollers
            del self.pollers[meter_id]
            
            logger.info(f"✓ Stopped poller for meter {meter_id}")
            return True
            
        except Exception as e:
            logger.error(f"Error stopping meter {meter_id}: {e}")
            return False
    
    def restart_meter(self, meter_id: int) -> bool:
        """Restart a meter poller"""
        logger.info(f"Restarting meter {meter_id}...")
        self.stop_meter(meter_id)
        time.sleep(1)
        return self.start_meter(meter_id)
    
    def get_meter_status(self, meter_id: int) -> Optional[Dict]:
        """Get current status of a meter"""
        if meter_id not in self.pollers:
            return {'status': 'inactive', 'running': False}
        
        poller = self.pollers[meter_id]
        return {
            'status': poller.status,
            'running': poller.process.is_alive(),
            'pid': poller.pid,
            'started_at': poller.started_at.isoformat(),
            'last_heartbeat': poller.last_heartbeat.isoformat(),
            'uptime': (datetime.now() - poller.started_at).total_seconds()
        }
    
    def get_all_statuses(self) -> Dict[int, Dict]:
        """Get status of all meters"""
        statuses = {}
        
        db = Database(self.db_path)
        with db.get_session() as session:
            all_meters = get_all_meters(session)
            for meter in all_meters:
                if meter.id in self.pollers:
                    statuses[meter.id] = self.get_meter_status(meter.id)
                else:
                    statuses[meter.id] = {
                        'status': meter.status,
                        'running': False
                    }
        
        return statuses
    
    def health_check_loop(self):
        """Main health check loop - runs continuously"""
        logger.info("Health check loop started")
        
        while self.running:
            try:
                self._perform_health_checks()
                time.sleep(self.health_check_interval)
            except Exception as e:
                logger.error(f"Error in health check loop: {e}")
                time.sleep(5)
    
    def _start_meter_poller(self, meter_id: int) -> bool:
        """Internal method to start a poller process"""
        try:
            # Create process - pass db_path instead of self
            process = mp.Process(
                target=self._poller_worker,
                args=(meter_id, self.db_path),  # Pass db_path string
                name=f"poller-meter-{meter_id}"
            )
            process.start()
            
            # Create poller record
            poller = PollerProcess(
                meter_id=meter_id,
                process=process,
                started_at=datetime.now(),
                last_heartbeat=datetime.now(),
                status='starting',
                pid=process.pid
            )
            
            self.pollers[meter_id] = poller
            self.restart_counts[meter_id] = 0
            
            # Update database
            db = Database(self.db_path)
            with db.get_session() as session:
                meter = get_meter_by_id(session, meter_id)
                if meter:
                    meter.process_id = process.pid
                    meter.last_seen = datetime.now()
                    session.commit()
            
            logger.info(f"✓ Started poller for meter {meter_id} (PID: {process.pid})")
            return True
            
        except Exception as e:
            logger.error(f"Failed to start poller for meter {meter_id}: {e}")
            
            # Create alarm (startup errors should always be reported)
            db = Database(self.db_path)
            with db.get_session() as session:
                create_alarm(
                    session, meter_id, 'error', 'startup',
                    f"Failed to start poller: {str(e)}"
                )
            
            return False
    
    def _poller_worker(self, meter_id: int, db_path: str):
        """
        Worker process that runs the DLMS poller
        This runs in a separate process
        Each process creates its own database connection
        """
        # Import here to avoid issues with multiprocessing
        import sys
        import os
        from pathlib import Path
        
        # Ensure we're using the virtual environment if it exists
        venv_path = Path(__file__).parent.parent / 'venv'
        if venv_path.exists():
            venv_site_packages = venv_path / 'lib' / f'python{sys.version_info.major}.{sys.version_info.minor}' / 'site-packages'
            if venv_site_packages.exists():
                sys.path.insert(0, str(venv_site_packages))
        
        sys.path.insert(0, str(Path(__file__).parent.parent / 'app'))
        sys.path.insert(0, str(Path(__file__).parent.parent))
        
        # Create database connection for this process
        db = Database(db_path)
        db.initialize()
        
        try:
            from dlms_poller_production import ProductionDLMSPoller
            from dlms_mqtt_bridge import DLMSMQTTBridge
            from mqtt_publisher import ThingsBoardMQTTClient
            
            # Setup logging for this process
            logging.basicConfig(
                level=logging.INFO,
                format=f'[Meter-{meter_id}] %(asctime)s [%(levelname)s] %(message)s'
            )
            
            logger.info(f"Poller worker started for meter {meter_id}")
            
            # Load meter configuration from database (use the db instance created above)
            with db.get_session() as session:
                meter = get_meter_by_id(session, meter_id)
                if not meter:
                    logger.error(f"Meter {meter_id} not found")
                    return
                
                # Get enabled measurements
                measurements = [
                    config.measurement_name
                    for config in meter.configs
                    if config.enabled
                ]
                
                if not measurements:
                    logger.warning("No measurements configured, using defaults")
                    measurements = ['voltage_l1', 'current_l1', 'frequency', 
                                  'active_power', 'active_energy']
                
                # Get sampling interval (use first config or default)
                interval = meter.configs[0].sampling_interval if meter.configs else 1.0
                
                logger.info(f"Configuration: {len(measurements)} measurements, {interval}s interval")
            
            # Initialize DLMS poller
            logger.info(f"Initializing DLMS poller for {meter.ip_address}:{meter.port}")
            dlms_poller = ProductionDLMSPoller(
                host=meter.ip_address,
                port=meter.port,
                password="22222222",
                interval=interval,
                measurements=measurements,
                verbose=True
            )
            
            # Initialize MQTT client (sanitize client_id - remove spaces and special chars)
            # Add PID to make client_id unique and avoid session conflicts
            mqtt_client_id = f"{meter.name.replace(' ', '_').replace('-', '_')}_{os.getpid()}"
            
            # Get ThingsBoard configuration from database
            tb_enabled = getattr(meter, 'tb_enabled', True)
            tb_host = getattr(meter, 'tb_host', 'localhost')
            tb_port = getattr(meter, 'tb_port', 1883)
            tb_token = getattr(meter, 'tb_token', '')
            
            if not tb_enabled:
                logger.warning("ThingsBoard publishing is disabled for this meter")
            
            if not tb_token:
                logger.warning("ThingsBoard token not configured - messages will not be published")
            
            logger.info(f"Initializing MQTT client (id={mqtt_client_id}, host={tb_host}:{tb_port})")
            mqtt_client = ThingsBoardMQTTClient(
                host=tb_host,
                port=tb_port,
                client_id=mqtt_client_id,
                access_token=tb_token,
                reconnect_delay_max=5.0  # Faster reconnection attempts
            )
            
            # Connect MQTT
            mqtt_client.connect()
            logger.info(f"MQTT client connected to {tb_host}:{tb_port}")
            
            # Main polling loop
            logger.info(f"Starting polling loop (interval={interval}s)...")
            while True:
                try:
                    # Poll all measurements from DLMS meter
                    logger.debug(f"🔄 Calling poll_once()...")
                    readings = dlms_poller.poll_once()
                    logger.debug(f"✓ poll_once() returned: {len(readings) if readings else 0} readings")
                    
                    if readings:
                        # Check if we have actual data or just None values
                        valid_readings = {k: v for k, v in readings.items() if v is not None}
                        
                        if valid_readings:
                            # Publish to MQTT/ThingsBoard
                            success = mqtt_client.publish_telemetry(readings)
                            if success:
                                logger.info(f"✅ Published {len(readings)} readings to ThingsBoard ({len(valid_readings)} valid)")
                            else:
                                if mqtt_client.connected:
                                    logger.warning(f"⚠️  Failed to publish {len(readings)} readings (buffered)")
                                else:
                                    logger.warning(f"⚠️  MQTT disconnected - buffering {len(readings)} readings")
                        else:
                            logger.debug(f"⚠️  No valid readings to publish (all None values)")
                    
                    # Update heartbeat in database
                    with db.get_session() as session:
                        m = get_meter_by_id(session, meter_id)
                        if m:
                            m.last_seen = datetime.now()
                            session.commit()
                    
                    # Sleep until next interval
                    time.sleep(interval)
                    
                except KeyboardInterrupt:
                    logger.info("Received stop signal")
                    break
                except Exception as e:
                    logger.error(f"Error in polling loop: {e}", exc_info=True)
                    
                    # Record error
                    with db.get_session() as session:
                        m = get_meter_by_id(session, meter_id)
                        if m:
                            m.error_count += 1
                            m.last_error = str(e)
                            session.commit()
                        
                        create_alarm(
                            session, meter_id, 'error', 'polling',
                            f"Polling error: {str(e)}"
                        )
                    
                    time.sleep(5)  # Wait before retry
            
            # Cleanup
            logger.info("Stopping poller...")
            if mqtt_client:
                mqtt_client.disconnect()
            logger.info("Poller worker stopped")
            
        except Exception as e:
            logger.error(f"Fatal error in poller worker: {e}", exc_info=True)
    
    def _perform_health_checks(self):
        """Perform health checks on all running pollers"""
        for meter_id, poller in list(self.pollers.items()):
            try:
                # Check if process is alive
                if not poller.process.is_alive():
                    logger.warning(f"Poller for meter {meter_id} died unexpectedly")
                    
                    # Create alarm
                    db = Database(self.db_path)
                    with db.get_session() as session:
                        create_alarm(
                            session, meter_id, 'critical', 'process',
                            "Poller process died unexpectedly"
                        )
                        
                        meter = get_meter_by_id(session, meter_id)
                        if meter:
                            meter.status = 'error'
                            meter.error_count += 1
                            session.commit()
                    
                    # Attempt restart if enabled
                    if self.restart_on_failure:
                        restart_count = self.restart_counts.get(meter_id, 0)
                        if restart_count < self.max_restart_attempts:
                            logger.info(f"Attempting to restart meter {meter_id} (attempt {restart_count + 1})")
                            del self.pollers[meter_id]
                            self._start_meter_poller(meter_id)
                            self.restart_counts[meter_id] = restart_count + 1
                        else:
                            logger.error(f"Max restart attempts reached for meter {meter_id}")
                            del self.pollers[meter_id]
                
                # Check for stale heartbeat
                else:
                    time_since_heartbeat = datetime.now() - poller.last_heartbeat
                    if time_since_heartbeat > timedelta(seconds=60):
                        logger.warning(f"No heartbeat from meter {meter_id} for {time_since_heartbeat.total_seconds():.0f}s")
                        
                        db = Database(self.db_path)
                        with db.get_session() as session:
                            # Use throttled alarm to prevent spam
                            self._create_throttled_alarm(
                                session, meter_id, 'warning', 'heartbeat',
                                f"No heartbeat for {time_since_heartbeat.total_seconds():.0f}s"
                            )
                
            except Exception as e:
                logger.error(f"Error checking health of meter {meter_id}: {e}")


def run_orchestrator(db_path: str = "data/admin.db"):
    """Main function to run the orchestrator"""
    # Setup logging
    logging.basicConfig(
        level=logging.INFO,
        format='[%(asctime)s] [%(levelname)s] %(message)s',
        datefmt='%Y-%m-%d %H:%M:%S'
    )
    
    orchestrator = MeterOrchestrator(db_path)
    
    # Handle signals
    def signal_handler(signum, frame):
        logger.info(f"Received signal {signum}, shutting down...")
        orchestrator.stop()
        exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    # Start orchestrator
    orchestrator.start()
    
    # Run health check loop
    try:
        orchestrator.health_check_loop()
    except KeyboardInterrupt:
        logger.info("Keyboard interrupt received")
    finally:
        orchestrator.stop()


if __name__ == "__main__":
    run_orchestrator()
