"""Configuration module using pydantic for environment-based settings."""

from pydantic_settings import BaseSettings, SettingsConfigDict
from typing import List


class Settings(BaseSettings):
    """Application settings loaded from environment variables or .env file."""
    
    # DLMS/TCP Configuration (Microstar meter)
    DLMS_HOST: str = "192.168.5.177"
    DLMS_PORT: int = 3333
    DLMS_TIMEOUT_S: float = 5.0
    
    # DLMS Authentication
    DLMS_CLIENT_SAP: int = 1  # Client Service Access Point
    DLMS_SERVER_LOGICAL: int = 0  # CRITICAL: Must be 0 for Microstar
    DLMS_SERVER_PHYSICAL: int = 1  # Server physical address
    DLMS_PASSWORD: str = "22222222"  # Default password for SAP 1
    DLMS_MAX_INFO_LENGTH: int | None = None  # HDLC max info field length
    
    # OBIS codes to read (Microstar measurements)
    DLMS_MEASUREMENTS: List[str] = [
        "voltage_l1",   # Phase A voltage - OBIS: 1-1:32.7.0
        "current_l1",   # Phase A current - OBIS: 1-1:31.7.0
    ]
    
    # Control settings
    POLL_PERIOD_S: float = 5.0  # Time between readings
    MAX_CONSEC_ERRORS: int = 10  # Max consecutive errors before stopping
    VERBOSE: bool = False  # Enable DLMS frame-level logging
    
    # MQTT Configuration
    MQTT_HOST: str = "localhost"
    MQTT_PORT: int = 1883
    MQTT_USERNAME: str | None = None
    MQTT_PASSWORD: str | None = None
    MQTT_TLS: bool = False
    MQTT_CLIENT_ID: str = "dlms-bridge"
    MQTT_QOS: int = 1
    MQTT_RETAIN: bool = False
    MQTT_TOPIC: str = "meters/{device_id}/telemetry"
    
    # Device identification
    DEVICE_ID: str = "microstar-001"
    
    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        case_sensitive=True,
    )


settings = Settings()
