"""
REST API for DLMS Multi-Meter Admin System
FastAPI backend for meter management and monitoring
"""

from fastapi import FastAPI, HTTPException, Depends, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field
from typing import List, Optional, Dict, Any
from datetime import datetime
import asyncio
import json
import logging

from sqlalchemy.orm import Session
from admin.database import (
    Database, Meter, MeterConfig, MeterMetric, Alarm,
    get_db, create_meter, get_meter_by_id, get_meter_by_name,
    get_all_meters, get_active_meters, create_alarm,
    get_unacknowledged_alarms, acknowledge_alarm, record_metric,
    delete_alarm, delete_old_alarms, delete_alarms_by_category, delete_all_alarms
)
from admin.network_scanner import DLMSNetworkScanner, DiscoveredMeter
from admin.orchestrator import MeterOrchestrator

logger = logging.getLogger(__name__)

# Initialize FastAPI app
app = FastAPI(
    title="DLMS Multi-Meter Admin API",
    description="REST API for managing multiple DLMS meters",
    version="1.0.0"
)

# CORS middleware
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Global orchestrator instance
orchestrator: Optional[MeterOrchestrator] = None


# Pydantic models for API

class MeterCreate(BaseModel):
    name: str = Field(..., description="Unique meter name")
    ip_address: str = Field(..., description="IP address of meter")
    port: int = Field(3333, description="DLMS port")
    client_id: int = Field(1, description="DLMS client ID")
    server_id: int = Field(1, description="DLMS server ID")


class MeterUpdate(BaseModel):
    name: Optional[str] = None
    ip_address: Optional[str] = None
    port: Optional[int] = None
    status: Optional[str] = None
    error_count: Optional[int] = None


class ThingsBoardConfig(BaseModel):
    tb_enabled: bool = True
    tb_host: str = "thingsboard.cloud"
    tb_port: int = 1883
    tb_token: Optional[str] = None
    tb_device_name: Optional[str] = None


class MeterConfigCreate(BaseModel):
    measurement_name: str
    obis_code: str
    enabled: bool = True
    sampling_interval: float = 1.0
    tb_key: Optional[str] = None


class MeterResponse(BaseModel):
    id: int
    name: str
    ip_address: str
    port: int
    status: str
    last_seen: Optional[datetime]
    error_count: int
    process_id: Optional[int]
    created_at: datetime
    
    class Config:
        orm_mode = True


class AlarmResponse(BaseModel):
    id: int
    meter_id: int
    severity: str
    category: str
    message: str
    acknowledged: bool
    timestamp: datetime
    
    class Config:
        orm_mode = True


class MetricResponse(BaseModel):
    id: int
    meter_id: int
    timestamp: datetime
    avg_read_time: Optional[float]
    success_rate: Optional[float]
    total_reads: int
    successful_reads: int
    messages_sent: int
    
    class Config:
        orm_mode = True


class ScanRequest(BaseModel):
    start_ip: str = Field(..., description="Starting IP address")
    end_ip: str = Field(..., description="Ending IP address")
    ports: List[int] = Field([3333, 4059], description="Ports to scan")


class DiscoveredMeterResponse(BaseModel):
    ip_address: str
    port: int
    response_time: float
    accessible: bool
    model: Optional[str] = None
    serial_number: Optional[str] = None


# Startup/Shutdown events

@app.on_event("startup")
async def startup_event():
    """Initialize database and orchestrator on startup"""
    global orchestrator
    
    logger.info("Starting DLMS Admin API...")
    
    # Initialize database
    from admin.database import init_db
    init_db("data/admin.db")
    
    # Initialize orchestrator
    orchestrator = MeterOrchestrator("data/admin.db")
    orchestrator.start()
    
    # Start health check loop in background
    asyncio.create_task(run_health_checks())
    
    logger.info("✓ API started successfully")


@app.on_event("shutdown")
async def shutdown_event():
    """Cleanup on shutdown"""
    global orchestrator
    
    logger.info("Shutting down DLMS Admin API...")
    
    if orchestrator:
        orchestrator.stop()
    
    logger.info("✓ API shutdown complete")


async def run_health_checks():
    """Background task for health checks"""
    global orchestrator
    while True:
        try:
            if orchestrator:
                orchestrator._perform_health_checks()
            await asyncio.sleep(10)
        except Exception as e:
            logger.error(f"Error in health check task: {e}")
            await asyncio.sleep(5)


# API Endpoints

@app.get("/")
async def root():
    """API root endpoint"""
    return {
        "name": "DLMS Multi-Meter Admin API",
        "version": "1.0.0",
        "status": "running"
    }


@app.get("/health")
async def health_check():
    """Health check endpoint"""
    return {
        "status": "healthy",
        "timestamp": datetime.now().isoformat(),
        "orchestrator_running": orchestrator is not None and orchestrator.running
    }


# Meter Management Endpoints

@app.get("/meters", response_model=List[MeterResponse])
async def list_meters(db: Session = Depends(get_db)):
    """Get list of all meters"""
    meters = get_all_meters(db)
    return meters


@app.get("/meters/active", response_model=List[MeterResponse])
async def list_active_meters(db: Session = Depends(get_db)):
    """Get list of active meters"""
    meters = get_active_meters(db)
    return meters


@app.get("/meters/{meter_id}", response_model=MeterResponse)
async def get_meter(meter_id: int, db: Session = Depends(get_db)):
    """Get specific meter details"""
    meter = get_meter_by_id(db, meter_id)
    if not meter:
        raise HTTPException(status_code=404, detail="Meter not found")
    return meter


@app.post("/meters", response_model=MeterResponse, status_code=201)
async def create_new_meter(meter_data: MeterCreate, db: Session = Depends(get_db)):
    """Create a new meter"""
    # Check if name already exists
    existing = get_meter_by_name(db, meter_data.name)
    if existing:
        raise HTTPException(status_code=400, detail="Meter name already exists")
    
    # Create meter
    meter = create_meter(
        db,
        name=meter_data.name,
        ip_address=meter_data.ip_address,
        port=meter_data.port,
        client_id=meter_data.client_id,
        server_id=meter_data.server_id
    )
    
    return meter


@app.patch("/meters/{meter_id}", response_model=MeterResponse)
async def update_meter(meter_id: int, meter_data: MeterUpdate, db: Session = Depends(get_db)):
    """Update meter details"""
    meter = get_meter_by_id(db, meter_id)
    if not meter:
        raise HTTPException(status_code=404, detail="Meter not found")
    
    # Update fields
    if meter_data.name is not None:
        meter.name = meter_data.name
    if meter_data.ip_address is not None:
        meter.ip_address = meter_data.ip_address
    if meter_data.port is not None:
        meter.port = meter_data.port
    if meter_data.status is not None:
        meter.status = meter_data.status
    if meter_data.error_count is not None:
        meter.error_count = meter_data.error_count
    
    db.commit()
    db.refresh(meter)
    return meter


@app.delete("/meters/{meter_id}", status_code=204)
async def delete_meter(meter_id: int, db: Session = Depends(get_db)):
    """Delete a meter"""
    global orchestrator
    
    meter = get_meter_by_id(db, meter_id)
    if not meter:
        raise HTTPException(status_code=404, detail="Meter not found")
    
    # Stop poller if running
    if orchestrator and meter_id in orchestrator.pollers:
        orchestrator.stop_meter(meter_id)
    
    # Delete from database
    db.delete(meter)
    db.commit()
    
    return None


# Meter Control Endpoints

@app.post("/meters/{meter_id}/start")
async def start_meter(meter_id: int, db: Session = Depends(get_db)):
    """Start polling a meter"""
    global orchestrator
    
    if not orchestrator:
        raise HTTPException(status_code=500, detail="Orchestrator not initialized")
    
    meter = get_meter_by_id(db, meter_id)
    if not meter:
        raise HTTPException(status_code=404, detail="Meter not found")
    
    success = orchestrator.start_meter(meter_id)
    if not success:
        raise HTTPException(status_code=500, detail="Failed to start meter")
    
    return {"status": "started", "meter_id": meter_id}


@app.post("/meters/{meter_id}/stop")
async def stop_meter(meter_id: int, db: Session = Depends(get_db)):
    """Stop polling a meter"""
    global orchestrator
    
    if not orchestrator:
        raise HTTPException(status_code=500, detail="Orchestrator not initialized")
    
    success = orchestrator.stop_meter(meter_id)
    if not success:
        raise HTTPException(status_code=500, detail="Failed to stop meter")
    
    return {"status": "stopped", "meter_id": meter_id}


@app.post("/meters/{meter_id}/restart")
async def restart_meter(meter_id: int, db: Session = Depends(get_db)):
    """Restart a meter poller"""
    global orchestrator
    
    if not orchestrator:
        raise HTTPException(status_code=500, detail="Orchestrator not initialized")
    
    success = orchestrator.restart_meter(meter_id)
    if not success:
        raise HTTPException(status_code=500, detail="Failed to restart meter")
    
    return {"status": "restarted", "meter_id": meter_id}


@app.get("/meters/{meter_id}/status")
async def get_meter_status(meter_id: int):
    """Get current status of a meter"""
    global orchestrator
    
    if not orchestrator:
        raise HTTPException(status_code=500, detail="Orchestrator not initialized")
    
    status = orchestrator.get_meter_status(meter_id)
    if not status:
        raise HTTPException(status_code=404, detail="Meter not found or not running")
    
    return status


# Configuration Endpoints

@app.get("/meters/{meter_id}/config")
async def get_meter_config(meter_id: int, db: Session = Depends(get_db)):
    """Get meter configuration"""
    meter = get_meter_by_id(db, meter_id)
    if not meter:
        raise HTTPException(status_code=404, detail="Meter not found")
    
    return meter.configs


@app.post("/meters/{meter_id}/config")
async def add_meter_config(meter_id: int, config_data: MeterConfigCreate, db: Session = Depends(get_db)):
    """Add measurement configuration to meter"""
    meter = get_meter_by_id(db, meter_id)
    if not meter:
        raise HTTPException(status_code=404, detail="Meter not found")
    
    config = MeterConfig(
        meter_id=meter_id,
        measurement_name=config_data.measurement_name,
        obis_code=config_data.obis_code,
        enabled=config_data.enabled,
        sampling_interval=config_data.sampling_interval,
        tb_key=config_data.tb_key
    )
    
    db.add(config)
    db.commit()
    db.refresh(config)
    
    return config


@app.get("/meters/{meter_id}/thingsboard")
async def get_thingsboard_config(meter_id: int, db: Session = Depends(get_db)):
    """Get ThingsBoard configuration for a meter"""
    meter = get_meter_by_id(db, meter_id)
    if not meter:
        raise HTTPException(status_code=404, detail="Meter not found")
    
    return {
        "tb_enabled": meter.tb_enabled if hasattr(meter, 'tb_enabled') else True,
        "tb_host": meter.tb_host if hasattr(meter, 'tb_host') else "thingsboard.cloud",
        "tb_port": meter.tb_port if hasattr(meter, 'tb_port') else 1883,
        "tb_token": meter.tb_token if hasattr(meter, 'tb_token') else None,
        "tb_device_name": meter.tb_device_name if hasattr(meter, 'tb_device_name') else None
    }


@app.put("/meters/{meter_id}/thingsboard")
async def update_thingsboard_config(
    meter_id: int, 
    tb_config: ThingsBoardConfig, 
    db: Session = Depends(get_db)
):
    """Update ThingsBoard configuration for a meter"""
    meter = get_meter_by_id(db, meter_id)
    if not meter:
        raise HTTPException(status_code=404, detail="Meter not found")
    
    # Update ThingsBoard config
    meter.tb_enabled = tb_config.tb_enabled
    meter.tb_host = tb_config.tb_host
    meter.tb_port = tb_config.tb_port
    meter.tb_token = tb_config.tb_token
    meter.tb_device_name = tb_config.tb_device_name
    
    db.commit()
    db.refresh(meter)
    
    return {
        "status": "updated",
        "meter_id": meter_id,
        "tb_enabled": meter.tb_enabled,
        "tb_host": meter.tb_host,
        "tb_token": "***" if meter.tb_token else None
    }


@app.get("/meters/{meter_id}/thingsboard/test")
async def test_thingsboard_connection(meter_id: int, db: Session = Depends(get_db)):
    """Test ThingsBoard connection and verify if meter is publishing data"""
    meter = get_meter_by_id(db, meter_id)
    if not meter:
        raise HTTPException(status_code=404, detail="Meter not found")
    
    # Check if meter is running
    if not meter.process_id or meter.status != 'active':
        return {
            "status": "meter_not_running",
            "message": "Meter must be running to test ThingsBoard connection",
            "can_publish": False
        }
    
    # Check if ThingsBoard is configured
    if not hasattr(meter, 'tb_token') or not meter.tb_token:
        return {
            "status": "not_configured",
            "message": "ThingsBoard token not configured",
            "can_publish": False
        }
    
    # Try to verify MQTT connection (simple check)
    try:
        import paho.mqtt.client as mqtt
        import socket
        
        # Test DNS resolution
        try:
            socket.gethostbyname(meter.tb_host if hasattr(meter, 'tb_host') else "thingsboard.cloud")
            dns_ok = True
        except socket.gaierror:
            dns_ok = False
        
        # Test port connectivity
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5)
            port = meter.tb_port if hasattr(meter, 'tb_port') else 1883
            host = meter.tb_host if hasattr(meter, 'tb_host') else "thingsboard.cloud"
            result = sock.connect_ex((host, port))
            sock.close()
            port_ok = result == 0
        except Exception:
            port_ok = False
        
        # Check recent metrics to see if messages are being sent
        recent_metrics = db.query(MeterMetric).filter(
            MeterMetric.meter_id == meter_id
        ).order_by(MeterMetric.timestamp.desc()).first()
        
        messages_sent = recent_metrics.messages_sent if recent_metrics else 0
        
        return {
            "status": "test_complete",
            "dns_resolution": "ok" if dns_ok else "failed",
            "port_connectivity": "ok" if port_ok else "failed",
            "configured": True,
            "messages_sent_total": messages_sent,
            "can_publish": dns_ok and port_ok,
            "tb_host": meter.tb_host if hasattr(meter, 'tb_host') else "thingsboard.cloud",
            "tb_port": meter.tb_port if hasattr(meter, 'tb_port') else 1883
        }
        
    except ImportError:
        return {
            "status": "error",
            "message": "paho-mqtt library not installed",
            "can_publish": False
        }
    except Exception as e:
        return {
            "status": "error",
            "message": str(e),
            "can_publish": False
        }


# Network Scanning Endpoints

@app.post("/scan/network")
async def scan_network(scan_request: ScanRequest):
    """Scan network for DLMS meters"""
    scanner = DLMSNetworkScanner()
    
    try:
        discovered = await asyncio.get_event_loop().run_in_executor(
            None,
            scanner.scan_ip_range,
            scan_request.start_ip,
            scan_request.end_ip,
            scan_request.ports
        )
        
        return {
            "discovered_count": len(discovered),
            "meters": [
                {
                    "ip_address": m.ip_address,
                    "port": m.port,
                    "response_time": m.response_time,
                    "accessible": m.accessible,
                    "model": m.model,
                    "serial_number": m.serial_number
                }
                for m in discovered
            ]
        }
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Scan failed: {str(e)}")


@app.get("/scan/subnet/{subnet}")
async def scan_subnet(subnet: str):
    """Scan entire subnet (e.g., 192.168.1.0/24)"""
    scanner = DLMSNetworkScanner()
    
    try:
        discovered = await asyncio.get_event_loop().run_in_executor(
            None,
            scanner.scan_subnet,
            subnet
        )
        
        return {
            "discovered_count": len(discovered),
            "meters": [
                {
                    "ip_address": m.ip_address,
                    "port": m.port,
                    "response_time": m.response_time
                }
                for m in discovered
            ]
        }
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Scan failed: {str(e)}")


# Metrics Endpoints

@app.get("/meters/{meter_id}/metrics", response_model=List[MetricResponse])
async def get_meter_metrics(meter_id: int, limit: int = 100, db: Session = Depends(get_db)):
    """Get recent metrics for a meter"""
    metrics = db.query(MeterMetric)\
        .filter(MeterMetric.meter_id == meter_id)\
        .order_by(MeterMetric.timestamp.desc())\
        .limit(limit)\
        .all()
    
    return metrics


@app.get("/meters/{meter_id}/network_stats")
async def get_meter_network_stats(meter_id: int, limit: int = 100, db: Session = Depends(get_db)):
    """Get network statistics for a specific meter"""
    from admin.database import NetworkMetric
    
    meter = get_meter_by_id(db, meter_id)
    if not meter:
        raise HTTPException(status_code=404, detail="Meter not found")
    
    # Get latest network metric
    latest = db.query(NetworkMetric)\
        .filter(NetworkMetric.meter_id == meter_id)\
        .order_by(NetworkMetric.timestamp.desc())\
        .first()
    
    # Get historical metrics
    history_metrics = db.query(NetworkMetric)\
        .filter(NetworkMetric.meter_id == meter_id)\
        .order_by(NetworkMetric.timestamp.desc())\
        .limit(limit)\
        .all()
    
    if not latest:
        # No data yet
        return {
            "meter_id": meter_id,
            "meter_name": meter.name,
            "timestamp": datetime.now().isoformat(),
            "current": {
                "bandwidth_tx_kbps": 0.0,
                "bandwidth_rx_kbps": 0.0,
                "bandwidth_total_kbps": 0.0,
                "bandwidth_tx_mbps": 0.0,
                "bandwidth_rx_mbps": 0.0,
                "bandwidth_total_mbps": 0.0,
                "packets_tx_ps": 0.0,
                "packets_rx_ps": 0.0,
                "packets_total_ps": 0.0,
            },
            "application": {
                "dlms_requests_sent": 0,
                "dlms_responses_recv": 0,
                "dlms_avg_payload_size": 0.0,
                "dlms_total_bytes_sent": 0,
                "dlms_total_bytes_recv": 0,
                "mqtt_messages_sent": 0,
                "mqtt_total_bytes_sent": 0,
            },
            "totals": {
                "bytes_sent": 0,
                "bytes_recv": 0,
                "packets_sent": 0,
                "packets_recv": 0,
                "errors": 0,
                "drops": 0,
            },
            "averages": {
                "bandwidth_tx_mbps": 0.0,
                "bandwidth_rx_mbps": 0.0,
                "bandwidth_total_mbps": 0.0,
            },
            "peaks": {
                "bandwidth_tx_mbps": 0.0,
                "bandwidth_rx_mbps": 0.0,
            },
            "history": {
                "timestamp": [],
                "bandwidth_tx_mbps": [],
                "bandwidth_rx_mbps": [],
                "packets_tx_ps": [],
                "packets_rx_ps": [],
                "errors": [],
            },
        }
    
    # Calculate averages and peaks from history
    if history_metrics:
        avg_bw_tx = sum(m.bandwidth_tx_bps for m in history_metrics) / len(history_metrics)
        avg_bw_rx = sum(m.bandwidth_rx_bps for m in history_metrics) / len(history_metrics)
        max_bw_tx = max(m.bandwidth_tx_bps for m in history_metrics)
        max_bw_rx = max(m.bandwidth_rx_bps for m in history_metrics)
    else:
        avg_bw_tx = avg_bw_rx = max_bw_tx = max_bw_rx = 0.0
    
    # Build history time series (reversed for chronological order)
    history_reversed = list(reversed(history_metrics))
    
    return {
        "meter_id": meter_id,
        "meter_name": meter.name,
        "timestamp": datetime.now().isoformat(),
        
        # Current rates (from latest metric)
        "current": {
            "bandwidth_tx_kbps": latest.bandwidth_tx_bps / 1000,
            "bandwidth_rx_kbps": latest.bandwidth_rx_bps / 1000,
            "bandwidth_total_kbps": (latest.bandwidth_tx_bps + latest.bandwidth_rx_bps) / 1000,
            "bandwidth_tx_mbps": latest.bandwidth_tx_bps / 1_000_000,
            "bandwidth_rx_mbps": latest.bandwidth_rx_bps / 1_000_000,
            "bandwidth_total_mbps": (latest.bandwidth_tx_bps + latest.bandwidth_rx_bps) / 1_000_000,
            "packets_tx_ps": latest.packets_tx_ps,
            "packets_rx_ps": latest.packets_rx_ps,
            "packets_total_ps": latest.packets_tx_ps + latest.packets_rx_ps,
        },
        
        # Application-level stats
        "application": {
            "dlms_requests_sent": latest.dlms_requests_sent,
            "dlms_responses_recv": latest.dlms_responses_recv,
            "dlms_avg_payload_size": latest.dlms_avg_payload_size,
            "dlms_total_bytes_sent": latest.dlms_bytes_sent,
            "dlms_total_bytes_recv": latest.dlms_bytes_recv,
            "mqtt_messages_sent": latest.mqtt_messages_sent,
            "mqtt_total_bytes_sent": latest.mqtt_bytes_sent,
        },
        
        # Cumulative totals (from latest)
        "totals": {
            "bytes_sent": latest.dlms_bytes_sent + latest.mqtt_bytes_sent,
            "bytes_recv": latest.dlms_bytes_recv,
            "packets_sent": 0,  # Not tracked at app level
            "packets_recv": 0,
            "errors": 0,
            "drops": 0,
        },
        
        # Averages
        "averages": {
            "bandwidth_tx_mbps": avg_bw_tx / 1_000_000,
            "bandwidth_rx_mbps": avg_bw_rx / 1_000_000,
            "bandwidth_total_mbps": (avg_bw_tx + avg_bw_rx) / 1_000_000,
        },
        
        # Peak values
        "peaks": {
            "bandwidth_tx_mbps": max_bw_tx / 1_000_000,
            "bandwidth_rx_mbps": max_bw_rx / 1_000_000,
        },
        
        # Time series history for graphing
        "history": {
            "timestamp": [m.timestamp.isoformat() for m in history_reversed],
            "bandwidth_tx_mbps": [m.bandwidth_tx_bps / 1_000_000 for m in history_reversed],
            "bandwidth_rx_mbps": [m.bandwidth_rx_bps / 1_000_000 for m in history_reversed],
            "packets_tx_ps": [m.packets_tx_ps for m in history_reversed],
            "packets_rx_ps": [m.packets_rx_ps for m in history_reversed],
            "errors": [0] * len(history_reversed),  # Not tracked yet
        },
    }


@app.get("/network_stats")
async def get_global_network_stats():
    """Get global network statistics (all interfaces)"""
    from network_monitor import get_network_monitor
    
    monitor = get_network_monitor()
    current_stats = monitor.get_current_stats()
    summary = monitor.get_summary()
    
    return {
        "timestamp": datetime.now().isoformat(),
        "current": current_stats['rates'],
        "application": current_stats['application'],
        "totals": current_stats['totals'],
        "averages": summary['averages'],
        "peaks": summary['peaks'],
    }


@app.get("/metrics/summary")
async def get_metrics_summary(db: Session = Depends(get_db)):
    """Get summary metrics for all meters"""
    meters = get_all_meters(db)
    
    summary = []
    for meter in meters:
        latest_metric = db.query(MeterMetric)\
            .filter(MeterMetric.meter_id == meter.id)\
            .order_by(MeterMetric.timestamp.desc())\
            .first()
        
        summary.append({
            "meter_id": meter.id,
            "meter_name": meter.name,
            "status": meter.status,
            "latest_metric": latest_metric,
            "error_count": meter.error_count
        })
    
    return summary


# Alarm Endpoints

@app.get("/alarms", response_model=List[AlarmResponse])
async def get_alarms(
    acknowledged: Optional[bool] = None,
    severity: Optional[str] = None,
    meter_id: Optional[int] = None,
    limit: int = 100,
    db: Session = Depends(get_db)
):
    """Get alarms with optional filters"""
    query = db.query(Alarm)
    
    if acknowledged is not None:
        query = query.filter(Alarm.acknowledged == acknowledged)
    if severity:
        query = query.filter(Alarm.severity == severity)
    if meter_id:
        query = query.filter(Alarm.meter_id == meter_id)
    
    alarms = query.order_by(Alarm.timestamp.desc()).limit(limit).all()
    return alarms


@app.post("/alarms/{alarm_id}/acknowledge")
async def ack_alarm(alarm_id: int, db: Session = Depends(get_db)):
    """Acknowledge an alarm"""
    success = acknowledge_alarm(db, alarm_id, "api_user")
    if not success:
        raise HTTPException(status_code=404, detail="Alarm not found")
    
    return {"status": "acknowledged", "alarm_id": alarm_id}


@app.delete("/alarms/{alarm_id}")
async def delete_single_alarm(alarm_id: int, db: Session = Depends(get_db)):
    """Delete a specific alarm"""
    success = delete_alarm(db, alarm_id)
    if not success:
        raise HTTPException(status_code=404, detail="Alarm not found")
    
    return {"status": "deleted", "alarm_id": alarm_id}


@app.delete("/alarms")
async def delete_alarms(
    category: Optional[str] = None,
    meter_id: Optional[int] = None,
    older_than_hours: Optional[int] = None,
    delete_all: bool = False,
    db: Session = Depends(get_db)
):
    """
    Delete alarms with various filters:
    - category: Delete alarms of a specific category (e.g., 'heartbeat')
    - meter_id: Filter by meter ID
    - older_than_hours: Delete alarms older than X hours
    - delete_all: Delete all alarms (use with caution)
    """
    deleted_count = 0
    
    if delete_all:
        deleted_count = delete_all_alarms(db, meter_id)
    elif older_than_hours is not None:
        deleted_count = delete_old_alarms(db, older_than_hours)
    elif category:
        deleted_count = delete_alarms_by_category(db, category, meter_id)
    else:
        raise HTTPException(
            status_code=400, 
            detail="Must specify category, older_than_hours, or delete_all=true"
        )
    
    return {
        "status": "deleted",
        "count": deleted_count,
        "filters": {
            "category": category,
            "meter_id": meter_id,
            "older_than_hours": older_than_hours,
            "delete_all": delete_all
        }
    }


# WebSocket for real-time updates

@app.websocket("/ws/metrics")
async def websocket_metrics(websocket: WebSocket):
    """WebSocket endpoint for real-time metrics"""
    await websocket.accept()
    
    try:
        while True:
            # Send current status of all meters
            if orchestrator:
                statuses = orchestrator.get_all_statuses()
                await websocket.send_json({
                    "type": "status_update",
                    "timestamp": datetime.now().isoformat(),
                    "data": statuses
                })
            
            await asyncio.sleep(2)  # Update every 2 seconds
            
    except WebSocketDisconnect:
        logger.info("WebSocket client disconnected")
    except Exception as e:
        logger.error(f"WebSocket error: {e}")


if __name__ == "__main__":
    import uvicorn
    
    logging.basicConfig(level=logging.INFO)
    
    uvicorn.run(
        "api:app",
        host="0.0.0.0",
        port=8000,
        reload=True,
        log_level="info"
    )
