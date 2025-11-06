#!/usr/bin/env python3
"""
Cliente MQTT para ThingsBoard con reconexión automática y buffer offline.

Características:
- Reconexión automática con backoff exponencial
- Buffer de mensajes offline (cola)
- Formato de telemetría ThingsBoard
- Logging detallado
- Métricas de conexión y envío
"""

import json
import logging
import time
from typing import Dict, Optional, List, Callable
from queue import Queue, Empty
from collections import deque
import paho.mqtt.client as mqtt

logger = logging.getLogger(__name__)


class ThingsBoardMQTTClient:
    """Cliente MQTT optimizado para ThingsBoard."""
    
    def __init__(
        self,
        host: str,
        port: int = 1883,
        access_token: str = "",
        client_id: str = "dlms_bridge",
        max_offline_messages: int = 1000,
        reconnect_delay_base: float = 1.0,
        reconnect_delay_max: float = 60.0
    ):
        """
        Inicializa el cliente MQTT.
        
        Args:
            host: Dirección del broker MQTT/ThingsBoard
            port: Puerto MQTT (default: 1883, TLS: 8883)
            access_token: Token de acceso del dispositivo en ThingsBoard
            client_id: ID del cliente MQTT
            max_offline_messages: Máximo de mensajes en buffer offline
            reconnect_delay_base: Delay base para reconexión (segundos)
            reconnect_delay_max: Delay máximo para reconexión (segundos)
        """
        self.host = host
        self.port = port
        self.access_token = access_token
        self.client_id = client_id
        
        # Buffer offline
        self.max_offline_messages = max_offline_messages
        self.offline_buffer: deque = deque(maxlen=max_offline_messages)
        
        # Reconexión
        self.reconnect_delay_base = reconnect_delay_base
        self.reconnect_delay_max = reconnect_delay_max
        self.reconnect_delay = reconnect_delay_base
        
        # Estado
        self.connected = False
        self.last_publish_time = 0
        
        # Métricas
        self.messages_sent = 0
        self.messages_failed = 0
        self.messages_buffered = 0
        self.reconnect_count = 0
        
        # Cliente MQTT
        self.client = mqtt.Client(
            client_id=self.client_id,
            protocol=mqtt.MQTTv311
        )
        
        # Configurar autenticación (ThingsBoard usa access_token como username)
        if self.access_token:
            self.client.username_pw_set(self.access_token)
        
        # Configurar callbacks
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_publish = self._on_publish
        
        # Topics de ThingsBoard
        self.telemetry_topic = "v1/devices/me/telemetry"
        self.attributes_topic = "v1/devices/me/attributes"
        
        logger.info(f"Cliente MQTT inicializado: {host}:{port}")
    
    def _on_connect(self, client, userdata, flags, rc):
        """Callback cuando se conecta al broker."""
        if rc == 0:
            self.connected = True
            self.reconnect_delay = self.reconnect_delay_base
            logger.info(f"✓ Conectado a MQTT broker: {self.host}:{self.port}")
            
            # Enviar mensajes buffered
            self._flush_offline_buffer()
        else:
            self.connected = False
            error_messages = {
                1: "Incorrect protocol version",
                2: "Invalid client identifier",
                3: "Server unavailable",
                4: "Bad username or password",
                5: "Not authorized"
            }
            error_msg = error_messages.get(rc, f"Unknown error code: {rc}")
            logger.error(f"✗ Conexión MQTT fallida: {error_msg}")
    
    def _on_disconnect(self, client, userdata, rc):
        """Callback cuando se desconecta del broker."""
        self.connected = False
        
        if rc != 0:
            logger.warning(f"⚠ Desconectado inesperadamente (code: {rc})")
            self.reconnect_count += 1
        else:
            logger.info("Desconectado del broker MQTT")
    
    def _on_publish(self, client, userdata, mid):
        """Callback cuando se publica un mensaje."""
        self.messages_sent += 1
        self.last_publish_time = time.time()
    
    def connect(self, timeout: float = 10.0) -> bool:
        """
        Conecta al broker MQTT.
        
        Args:
            timeout: Timeout de conexión en segundos
            
        Returns:
            True si conectó exitosamente, False en caso contrario
        """
        # ✅ PROTECCIÓN: Si ya está conectado, retornar inmediatamente
        if self.connected:
            logger.debug(f"Ya conectado a MQTT broker: {self.host}:{self.port}")
            return True
        
        try:
            logger.info(f"Conectando a {self.host}:{self.port}...")
            self.client.connect(self.host, self.port, keepalive=60)
            self.client.loop_start()
            
            # Esperar conexión
            start_time = time.time()
            while not self.connected and (time.time() - start_time) < timeout:
                time.sleep(0.1)
            
            if self.connected:
                return True
            else:
                logger.error(f"Timeout conectando a MQTT broker")
                return False
                
        except Exception as e:
            logger.error(f"Error conectando a MQTT: {e}")
            return False
    
    def disconnect(self):
        """Desconecta del broker MQTT."""
        # ✅ PROTECCIÓN: Si ya está desconectado, retornar inmediatamente
        if not self.connected:
            logger.debug("Ya desconectado de MQTT broker")
            return
        
        try:
            self.client.loop_stop()
            self.client.disconnect()
            logger.info("Desconectado de MQTT broker")
        except Exception as e:
            logger.error(f"Error desconectando: {e}")
    
    def publish_telemetry(self, data: Dict, timestamp: Optional[int] = None) -> bool:
        """
        Publica telemetría a ThingsBoard.
        
        Args:
            data: Diccionario con los datos de telemetría
            timestamp: Timestamp en milisegundos (opcional, usa tiempo actual si no se provee)
            
        Returns:
            True si se publicó o bufferó exitosamente, False en caso contrario
            
        Ejemplo:
            data = {
                "voltage": 136.5,
                "current": 1.33,
                "frequency": 60.0,
                "active_power": 181.5,
                "active_energy": 56281.0
            }
        """
        try:
            # Preparar payload ThingsBoard
            if timestamp is None:
                # ThingsBoard acepta timestamp en ms
                timestamp = int(time.time() * 1000)
            
            payload = {
                "ts": timestamp,
                "values": data
            }
            
            payload_json = json.dumps(payload)
            
            # Si está conectado, publicar
            if self.connected:
                result = self.client.publish(
                    self.telemetry_topic,
                    payload_json,
                    qos=1  # At least once
                )
                
                if result.rc == mqtt.MQTT_ERR_SUCCESS:
                    logger.debug(f"📤 Telemetría publicada: {len(data)} campos")
                    return True
                else:
                    logger.warning(f"⚠ Error publicando (rc={result.rc}), buffeando...")
                    self._buffer_message(payload_json)
                    return False
            else:
                # Si no está conectado, buffear
                logger.debug("⚠ Sin conexión MQTT, buffeando mensaje...")
                self._buffer_message(payload_json)
                return False
                
        except Exception as e:
            logger.error(f"Error publicando telemetría: {e}")
            self.messages_failed += 1
            return False
    
    def publish_attributes(self, data: Dict) -> bool:
        """
        Publica attributes (client-side) a ThingsBoard.
        
        Los attributes son propiedades semi-estáticas del dispositivo,
        diferentes de la telemetría que cambia continuamente.
        
        Args:
            data: Diccionario con los attributes a publicar
            
        Returns:
            True si se publicó exitosamente, False en caso contrario
            
        Ejemplo:
            attributes = {
                "model": "DLMS Energy Meter",
                "firmware_version": "1.0.0",
                "location": "Building A",
                "installation_date": "2025-10-30",
                "max_voltage": 140.0,
                "max_current": 10.0,
                "rated_frequency": 60.0
            }
            
        Nota:
            - Attributes no tienen timestamp (son propiedades actuales)
            - Se publican al topic v1/devices/me/attributes
            - Típicamente se envían una vez al inicio o cuando cambian
        """
        try:
            payload_json = json.dumps(data)
            
            # Si está conectado, publicar
            if self.connected:
                result = self.client.publish(
                    self.attributes_topic,
                    payload_json,
                    qos=1  # At least once
                )
                
                if result.rc == mqtt.MQTT_ERR_SUCCESS:
                    logger.info(f"📋 Attributes publicados: {len(data)} campos")
                    return True
                else:
                    logger.warning(f"⚠ Error publicando attributes (rc={result.rc})")
                    return False
            else:
                logger.debug("⚠ Sin conexión MQTT, no se pueden publicar attributes")
                return False
                
        except Exception as e:
            logger.error(f"Error publicando attributes: {e}")
            return False
    
    def _buffer_message(self, message: str):
        """Agrega mensaje al buffer offline."""
        self.offline_buffer.append(message)
        self.messages_buffered += 1
        
        if len(self.offline_buffer) >= self.max_offline_messages:
            logger.warning(f"⚠ Buffer offline lleno ({self.max_offline_messages} mensajes)")
    
    def _flush_offline_buffer(self):
        """Envía mensajes del buffer offline cuando se reconecta."""
        if not self.offline_buffer:
            return
        
        logger.info(f"📤 Enviando {len(self.offline_buffer)} mensajes del buffer offline...")
        
        messages_sent = 0
        messages_failed = 0
        
        while self.offline_buffer and self.connected:
            try:
                message = self.offline_buffer.popleft()
                result = self.client.publish(
                    self.telemetry_topic,
                    message,
                    qos=1
                )
                
                if result.rc == mqtt.MQTT_ERR_SUCCESS:
                    messages_sent += 1
                else:
                    # Regresar al buffer si falla
                    self.offline_buffer.appendleft(message)
                    messages_failed += 1
                    break
                
                # Pequeña pausa para no saturar
                time.sleep(0.01)
                
            except Exception as e:
                logger.error(f"Error enviando mensaje buffered: {e}")
                messages_failed += 1
                break
        
        if messages_sent > 0:
            logger.info(f"✓ {messages_sent} mensajes del buffer enviados exitosamente")
        
        if messages_failed > 0:
            logger.warning(f"⚠ {messages_failed} mensajes fallaron al enviar")
    
    def get_stats(self) -> Dict:
        """Retorna estadísticas del cliente MQTT."""
        return {
            "connected": self.connected,
            "messages_sent": self.messages_sent,
            "messages_failed": self.messages_failed,
            "messages_buffered": len(self.offline_buffer),
            "reconnect_count": self.reconnect_count,
            "last_publish_time": self.last_publish_time
        }
    
    def __enter__(self):
        """Context manager entry."""
        self.connect()
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.disconnect()


if __name__ == "__main__":
    # Prueba básica
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
    )
    
    # Configuración de ejemplo
    print("=== ThingsBoard MQTT Client - Test ===")
    print("Configuración:")
    print("  Host: demo.thingsboard.io")
    print("  Puerto: 1883")
    print("  Token: YOUR_ACCESS_TOKEN")
    print("\nPara probar, actualiza el access_token y descomenta el código de prueba")
    
    # Descomentar para probar:
    # client = ThingsBoardMQTTClient(
    #     host="demo.thingsboard.io",
    #     port=1883,
    #     access_token="YOUR_ACCESS_TOKEN_HERE"
    # )
    # 
    # if client.connect():
    #     # Enviar datos de prueba
    #     test_data = {
    #         "temperature": 25.5,
    #         "humidity": 60.2,
    #         "voltage": 136.5
    #     }
    #     
    #     client.publish_telemetry(test_data)
    #     time.sleep(2)
    #     
    #     print(f"Estadísticas: {client.get_stats()}")
    #     client.disconnect()
