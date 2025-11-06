"""
Database models and ORM for DLMS Multi-Meter Admin System
Uses SQLAlchemy with SQLite for easy deployment
"""

from datetime import datetime
from typing import Optional, List
from sqlalchemy import create_engine, Column, Integer, String, Float, Boolean, DateTime, ForeignKey, Text
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker, relationship, Session
from pathlib import Path

Base = declarative_base()

class Meter(Base):
    """Represents a DLMS meter in the network"""
    __tablename__ = 'meters'
    
    id = Column(Integer, primary_key=True, autoincrement=True)
    name = Column(String(100), unique=True, nullable=False)
    ip_address = Column(String(45), nullable=False)  # IPv4 or IPv6
    port = Column(Integer, default=3333)
    client_id = Column(Integer, default=1)
    server_id = Column(Integer, default=1)
    
    # Status tracking
    status = Column(String(20), default='inactive')  # 'active', 'inactive', 'error', 'discovering'
    last_seen = Column(DateTime, nullable=True)
    last_error = Column(Text, nullable=True)
    error_count = Column(Integer, default=0)
    
    # Process management
    process_id = Column(Integer, nullable=True)  # OS process ID when running
    
    # ThingsBoard/MQTT configuration
    tb_enabled = Column(Boolean, default=True)  # Enable ThingsBoard publishing
    tb_host = Column(String(255), default='thingsboard.cloud')
    tb_port = Column(Integer, default=1883)
    tb_token = Column(String(100), nullable=True)  # Device access token
    tb_device_name = Column(String(100), nullable=True)  # Device name in ThingsBoard
    
    # Metadata
    model = Column(String(100), nullable=True)
    serial_number = Column(String(100), nullable=True)
    firmware_version = Column(String(50), nullable=True)
    
    # Timestamps
    created_at = Column(DateTime, default=datetime.utcnow)
    updated_at = Column(DateTime, default=datetime.utcnow, onupdate=datetime.utcnow)
    
    # Relationships
    configs = relationship("MeterConfig", back_populates="meter", cascade="all, delete-orphan")
    metrics = relationship("MeterMetric", back_populates="meter", cascade="all, delete-orphan")
    alarms = relationship("Alarm", back_populates="meter", cascade="all, delete-orphan")
    
    def __repr__(self):
        return f"<Meter(id={self.id}, name='{self.name}', ip='{self.ip_address}', status='{self.status}')>"


class MeterConfig(Base):
    """Configuration for measurements per meter"""
    __tablename__ = 'meter_configs'
    
    id = Column(Integer, primary_key=True, autoincrement=True)
    meter_id = Column(Integer, ForeignKey('meters.id'), nullable=False)
    
    # Measurement configuration
    measurement_name = Column(String(50), nullable=False)  # 'voltage_l1', 'current_l1', etc
    obis_code = Column(String(20), nullable=False)  # '1.0.32.7.0.255'
    enabled = Column(Boolean, default=True)
    
    # Sampling configuration
    sampling_interval = Column(Float, default=1.0)  # seconds
    
    # ThingsBoard mapping
    tb_key = Column(String(50), nullable=True)  # Custom key for ThingsBoard
    
    # Timestamps
    created_at = Column(DateTime, default=datetime.utcnow)
    updated_at = Column(DateTime, default=datetime.utcnow, onupdate=datetime.utcnow)
    
    # Relationships
    meter = relationship("Meter", back_populates="configs")
    
    def __repr__(self):
        return f"<MeterConfig(meter_id={self.meter_id}, measurement='{self.measurement_name}', enabled={self.enabled})>"


class MeterMetric(Base):
    """Performance metrics for each meter"""
    __tablename__ = 'meter_metrics'
    
    id = Column(Integer, primary_key=True, autoincrement=True)
    meter_id = Column(Integer, ForeignKey('meters.id'), nullable=False)
    
    # Performance metrics
    timestamp = Column(DateTime, default=datetime.utcnow, index=True)
    avg_read_time = Column(Float, nullable=True)  # Average cycle time in seconds
    min_read_time = Column(Float, nullable=True)
    max_read_time = Column(Float, nullable=True)
    
    # Success tracking
    total_reads = Column(Integer, default=0)
    successful_reads = Column(Integer, default=0)
    failed_reads = Column(Integer, default=0)
    success_rate = Column(Float, nullable=True)  # Percentage
    
    # MQTT metrics
    messages_sent = Column(Integer, default=0)
    messages_buffered = Column(Integer, default=0)
    mqtt_reconnections = Column(Integer, default=0)
    
    # Cache performance (Phase 2 optimization)
    cache_hits = Column(Integer, default=0)
    cache_misses = Column(Integer, default=0)
    cache_hit_rate = Column(Float, nullable=True)  # Percentage
    
    # Relationships
    meter = relationship("Meter", back_populates="metrics")
    
    def __repr__(self):
        return f"<MeterMetric(meter_id={self.meter_id}, timestamp={self.timestamp}, success_rate={self.success_rate}%)>"


class NetworkMetric(Base):
    """Network statistics for each meter"""
    __tablename__ = 'network_metrics'
    
    id = Column(Integer, primary_key=True, autoincrement=True)
    meter_id = Column(Integer, ForeignKey('meters.id'), nullable=False)
    
    # Timestamp
    timestamp = Column(DateTime, default=datetime.utcnow, index=True)
    
    # DLMS protocol metrics
    dlms_requests_sent = Column(Integer, default=0)
    dlms_responses_recv = Column(Integer, default=0)
    dlms_bytes_sent = Column(Integer, default=0)
    dlms_bytes_recv = Column(Integer, default=0)
    dlms_avg_payload_size = Column(Float, default=0.0)
    
    # MQTT protocol metrics
    mqtt_messages_sent = Column(Integer, default=0)
    mqtt_bytes_sent = Column(Integer, default=0)
    
    # Bandwidth metrics (bytes per second)
    bandwidth_tx_bps = Column(Float, default=0.0)
    bandwidth_rx_bps = Column(Float, default=0.0)
    
    # Packet metrics
    packets_tx_ps = Column(Float, default=0.0)
    packets_rx_ps = Column(Float, default=0.0)
    
    # Relationships
    meter = relationship("Meter", foreign_keys=[meter_id])
    
    def __repr__(self):
        return f"<NetworkMetric(meter_id={self.meter_id}, timestamp={self.timestamp}, dlms_req={self.dlms_requests_sent}, mqtt_msg={self.mqtt_messages_sent})>"


class DLMSDiagnostic(Base):
    """Raw diagnostic events captured from DLMS interactions (errors, raw frames)"""
    __tablename__ = 'dlms_diagnostics'

    id = Column(Integer, primary_key=True, autoincrement=True)
    meter_id = Column(Integer, ForeignKey('meters.id'), nullable=False)
    timestamp = Column(DateTime, default=datetime.utcnow, index=True)
    severity = Column(String(20), default='warning')
    category = Column(String(50), nullable=False)  # 'hdlc', 'connection', 'parse', 'other'
    message = Column(Text, nullable=False)
    raw_frame = Column(Text, nullable=True)  # HEX or base64 of raw bytes

    # Relationship convenience
    meter = relationship("Meter", foreign_keys=[meter_id])

    def __repr__(self):
        return f"<DLMSDiagnostic(meter_id={self.meter_id}, ts={self.timestamp}, category={self.category})>"


class Alarm(Base):
    """Alarms and warnings for meter issues"""
    __tablename__ = 'alarms'
    
    id = Column(Integer, primary_key=True, autoincrement=True)
    meter_id = Column(Integer, ForeignKey('meters.id'), nullable=False)
    
    # Alarm details
    severity = Column(String(20), nullable=False)  # 'info', 'warning', 'error', 'critical'
    category = Column(String(50), nullable=False)  # 'connection', 'performance', 'data', 'config'
    message = Column(Text, nullable=False)
    details = Column(Text, nullable=True)  # JSON or additional context
    
    # Status
    acknowledged = Column(Boolean, default=False)
    acknowledged_at = Column(DateTime, nullable=True)
    acknowledged_by = Column(String(100), nullable=True)
    
    # Timestamps
    timestamp = Column(DateTime, default=datetime.utcnow, index=True)
    
    # Relationships
    meter = relationship("Meter", back_populates="alarms")
    
    def __repr__(self):
        return f"<Alarm(id={self.id}, meter_id={self.meter_id}, severity='{self.severity}', message='{self.message[:50]}...')>"


class Database:
    """Database manager for the admin system"""
    
    def __init__(self, db_path: str = "data/admin.db"):
        self.db_path = db_path
        self.engine = None
        self.SessionLocal = None
        
    def initialize(self):
        """Initialize database connection and create tables"""
        # Ensure data directory exists
        db_file = Path(self.db_path)
        db_file.parent.mkdir(parents=True, exist_ok=True)
        
        # Create engine
        self.engine = create_engine(
            f'sqlite:///{self.db_path}',
            connect_args={"check_same_thread": False},  # Needed for SQLite
            echo=False  # Set to True for SQL debugging
        )
        
        # Create all tables
        Base.metadata.create_all(bind=self.engine)
        
        # Create session factory
        self.SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=self.engine)
        
        print(f"✓ Database initialized: {self.db_path}")
        
    def get_session(self) -> Session:
        """Get a new database session, auto-initializing if needed"""
        if self.SessionLocal is None:
            self.initialize()  # Auto-initialize if not already done
        return self.SessionLocal()
    
    def close(self):
        """Close database connection"""
        if self.engine:
            self.engine.dispose()


# Global database instance
db = Database()


def init_db(db_path: str = "data/admin.db"):
    """Initialize the global database instance"""
    db.db_path = db_path
    db.initialize()
    return db


def get_db() -> Session:
    """Dependency for getting database sessions"""
    session = db.get_session()
    try:
        yield session
        session.commit()
    except Exception:
        session.rollback()
        raise
    finally:
        session.close()


# Utility functions for common operations

def create_meter(session: Session, name: str, ip_address: str, port: int = 3333, 
                 client_id: int = 1, server_id: int = 1) -> Meter:
    """Create a new meter entry"""
    meter = Meter(
        name=name,
        ip_address=ip_address,
        port=port,
        client_id=client_id,
        server_id=server_id,
        status='inactive'
    )
    session.add(meter)
    session.commit()
    session.refresh(meter)
    return meter


def get_meter_by_id(session: Session, meter_id: int) -> Optional[Meter]:
    """Get meter by ID"""
    return session.query(Meter).filter(Meter.id == meter_id).first()


def get_meter_by_name(session: Session, name: str) -> Optional[Meter]:
    """Get meter by name"""
    return session.query(Meter).filter(Meter.name == name).first()


def get_all_meters(session: Session) -> List[Meter]:
    """Get all meters"""
    return session.query(Meter).all()


def get_active_meters(session: Session) -> List[Meter]:
    """Get all active meters"""
    return session.query(Meter).filter(Meter.status == 'active').all()


def update_meter_status(session: Session, meter_id: int, status: str, 
                       process_id: Optional[int] = None, 
                       error_count: Optional[int] = None) -> bool:
    """Update meter status and optionally process_id and error_count"""
    meter = session.query(Meter).filter(Meter.id == meter_id).first()
    if meter:
        meter.status = status
        meter.last_seen = datetime.utcnow()
        if process_id is not None:
            meter.process_id = process_id
        if error_count is not None:
            meter.error_count = error_count
        session.commit()
        return True
    return False


def create_alarm(session: Session, meter_id: int, severity: str, category: str, 
                 message: str, details: Optional[str] = None) -> Alarm:
    """Create a new alarm"""
    alarm = Alarm(
        meter_id=meter_id,
        severity=severity,
        category=category,
        message=message,
        details=details
    )
    session.add(alarm)
    session.commit()
    session.refresh(alarm)
    return alarm


def get_unacknowledged_alarms(session: Session, meter_id: Optional[int] = None) -> List[Alarm]:
    """Get unacknowledged alarms, optionally filtered by meter"""
    query = session.query(Alarm).filter(Alarm.acknowledged == False)
    if meter_id:
        query = query.filter(Alarm.meter_id == meter_id)
    return query.order_by(Alarm.timestamp.desc()).all()


def acknowledge_alarm(session: Session, alarm_id: int, acknowledged_by: str = "admin") -> bool:
    """Acknowledge an alarm"""
    alarm = session.query(Alarm).filter(Alarm.id == alarm_id).first()
    if alarm:
        alarm.acknowledged = True
        alarm.acknowledged_at = datetime.utcnow()
        alarm.acknowledged_by = acknowledged_by
        session.commit()
        return True
    return False


def delete_alarm(session: Session, alarm_id: int) -> bool:
    """Delete a specific alarm"""
    alarm = session.query(Alarm).filter(Alarm.id == alarm_id).first()
    if alarm:
        session.delete(alarm)
        session.commit()
        return True
    return False


def delete_old_alarms(session: Session, older_than_hours: int = 24) -> int:
    """Delete alarms older than specified hours"""
    from datetime import timedelta
    cutoff_time = datetime.utcnow() - timedelta(hours=older_than_hours)
    
    deleted = session.query(Alarm).filter(Alarm.timestamp < cutoff_time).delete()
    session.commit()
    return deleted


def delete_alarms_by_category(session: Session, category: str, meter_id: Optional[int] = None) -> int:
    """Delete alarms by category, optionally filtered by meter"""
    query = session.query(Alarm).filter(Alarm.category == category)
    if meter_id:
        query = query.filter(Alarm.meter_id == meter_id)
    
    deleted = query.delete()
    session.commit()
    return deleted


def delete_all_alarms(session: Session, meter_id: Optional[int] = None) -> int:
    """Delete all alarms, optionally filtered by meter"""
    query = session.query(Alarm)
    if meter_id:
        query = query.filter(Alarm.meter_id == meter_id)
    
    deleted = query.delete()
    session.commit()
    return deleted


def record_metric(session: Session, meter_id: int, avg_read_time: float, 
                  total_reads: int, successful_reads: int, failed_reads: int,
                  messages_sent: int, mqtt_reconnections: int = 0,
                  cache_hits: int = 0, cache_misses: int = 0) -> MeterMetric:
    """Record performance metrics for a meter"""
    success_rate = (successful_reads / total_reads * 100) if total_reads > 0 else 0
    cache_total = cache_hits + cache_misses
    cache_hit_rate = (cache_hits / cache_total * 100) if cache_total > 0 else None
    
    metric = MeterMetric(
        meter_id=meter_id,
        avg_read_time=avg_read_time,
        total_reads=total_reads,
        successful_reads=successful_reads,
        failed_reads=failed_reads,
        success_rate=success_rate,
        messages_sent=messages_sent,
        mqtt_reconnections=mqtt_reconnections,
        cache_hits=cache_hits,
        cache_misses=cache_misses,
        cache_hit_rate=cache_hit_rate
    )
    session.add(metric)
    session.commit()
    return metric


def record_network_metric(session: Session, meter_id: int,
                          dlms_requests_sent: int, dlms_responses_recv: int,
                          dlms_bytes_sent: int, dlms_bytes_recv: int,
                          dlms_avg_payload: float,
                          mqtt_messages_sent: int, mqtt_bytes_sent: int,
                          bandwidth_tx_bps: float = 0.0, bandwidth_rx_bps: float = 0.0,
                          packets_tx_ps: float = 0.0, packets_rx_ps: float = 0.0) -> NetworkMetric:
    """Record network statistics for a meter"""
    metric = NetworkMetric(
        meter_id=meter_id,
        dlms_requests_sent=dlms_requests_sent,
        dlms_responses_recv=dlms_responses_recv,
        dlms_bytes_sent=dlms_bytes_sent,
        dlms_bytes_recv=dlms_bytes_recv,
        dlms_avg_payload_size=dlms_avg_payload,
        mqtt_messages_sent=mqtt_messages_sent,
        mqtt_bytes_sent=mqtt_bytes_sent,
        bandwidth_tx_bps=bandwidth_tx_bps,
        bandwidth_rx_bps=bandwidth_rx_bps,
        packets_tx_ps=packets_tx_ps,
        packets_rx_ps=packets_rx_ps
    )
    session.add(metric)
    session.commit()
    return metric


def record_dlms_diagnostic(session: Session, meter_id: int, category: str, message: str,
                           severity: str = 'warning', raw_frame: Optional[str] = None) -> DLMSDiagnostic:
    """Record a DLMS diagnostic event (e.g., HDLC error, raw frame) for later analysis."""
    diag = DLMSDiagnostic(
        meter_id=meter_id,
        category=category,
        message=message,
        severity=severity,
        raw_frame=raw_frame
    )
    session.add(diag)
    session.commit()
    session.refresh(diag)
    return diag


def get_recent_diagnostics(session: Session, meter_id: int, limit: int = 100):
    """Return recent DLMS diagnostics for a meter"""
    return session.query(DLMSDiagnostic).filter(DLMSDiagnostic.meter_id == meter_id).order_by(DLMSDiagnostic.timestamp.desc()).limit(limit).all()


if __name__ == "__main__":
    # Test database creation
    print("Testing database models...")
    init_db("data/test_admin.db")
    
    with db.get_session() as session:
        # Create test meter
        meter = create_meter(session, "test_meter_1", "192.168.1.127", 3333)
        print(f"Created: {meter}")
        
        # Create test config
        config = MeterConfig(
            meter_id=meter.id,
            measurement_name="voltage_l1",
            obis_code="1.0.32.7.0.255",
            enabled=True
        )
        session.add(config)
        session.commit()
        print(f"Created config: {config}")
        
        # Create test alarm
        alarm = create_alarm(
            session, meter.id, "warning", "connection",
            "High response time detected"
        )
        print(f"Created alarm: {alarm}")
        
        # Record test metric
        metric = record_metric(
            session, meter.id, 
            avg_read_time=1.5, total_reads=100, successful_reads=98, 
            failed_reads=2, messages_sent=98
        )
        print(f"Created metric: {metric}")
        
        print("\n✓ Database test successful!")
