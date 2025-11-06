#!/usr/bin/env python3
"""
Sistema de polling DLMS robusto para producción.

Usa el cliente robusto con auto-recuperación integrado con dlms_reader.py
OPTIMIZADO: Implementa caché de scalers para reducir latencia 50%
"""

import sys
import time
import signal
import socket
import select
import struct
import traceback
import argparse
from typing import Dict, Optional
from dlms_client_robust import RobustDLMSClient, DLMSConfig, ConnectionState, logger
from dlms_reader import DLMSClient as OriginalDLMSClient
from dlms_optimized_reader import OptimizedDLMSReader
from admin.database import record_dlms_diagnostic, db

# Importar mediciones conocidas
MEASUREMENTS = {
    "voltage_l1": ("1-1:32.7.0", "Phase A instantaneous voltage", "V"),
    "current_l1": ("1-1:31.7.0", "Phase A instantaneous current", "A"),
    "frequency": ("1-1:14.7.0", "Supply frequency", "Hz"),
    "active_power": ("1-1:1.7.0", "Sum active power+ (QI+QIV)", "W"),
    "active_energy": ("1-1:1.8.0", "Active energy+ (import)", "Wh"),
    "voltage_l2": ("1-1:52.7.0", "Phase B instantaneous voltage", "V"),
    "current_l2": ("1-1:51.7.0", "Phase B instantaneous current", "A"),
    "voltage_l3": ("1-1:72.7.0", "Phase C instantaneous voltage", "V"),
    "current_l3": ("1-1:71.7.0", "Phase C instantaneous current", "A"),
}

# Variable global para controlar el loop
running = True

def signal_handler(sig, frame):
    """Maneja señales de interrupción."""
    global running
    logger.info("\n[!] Deteniendo polling...")
    running = False

class ProductionDLMSPoller:
    """Poller DLMS para producción con auto-recuperación."""
    
    def __init__(self, host: str, port: int = 3333, password: str = "22222222",
                 interval: float = 1.0, measurements: list = None, verbose: bool = False):
        
        # Configuración (MEJORADA: timeouts más tolerantes y circuit breaker efectivo)
        self.config = DLMSConfig(
            host=host,
            port=port,
            client_sap=1,
            server_logical=0,
            server_physical=1,
            password=password.encode('ascii'),
            timeout=7.0,  # Aumentado de 5.0s a 7.0s para reducir timeouts
            max_retries=3,
            retry_delay=3.0,  # Aumentado de 1.5s para dar tiempo de recuperación
            reconnect_threshold=15,  # Aumentado de 5 para reducir reconexiones
            circuit_breaker_threshold=15,  # Abrir circuito después de 15 errores
            circuit_breaker_timeout=30.0,  # Esperar 30s antes de reintentar
            heartbeat_interval=15.0,  # Heartbeat cada 15s
            buffer_clear_on_error=True
        )
        
        self.interval = interval
        self.measurements = measurements or ["voltage_l1", "current_l1"]
        self.verbose = verbose
        
        # Cliente original para las lecturas
        self.original_client: Optional[OriginalDLMSClient] = None
        
        # Cliente optimizado con caché de scalers
        self.optimized_reader: Optional[OptimizedDLMSReader] = None
        
        # Métricas
        self.start_time: Optional[float] = None
        self.total_cycles = 0
        self.successful_cycles = 0
        self.reconnect_count = 0
        
    def _create_original_client(self) -> OriginalDLMSClient:
        """Crea una instancia del cliente original."""
        return OriginalDLMSClient(
            host=self.config.host,
            port=self.config.port,
            client_sap=self.config.client_sap,
            server_logical=self.config.server_logical,
            server_physical=self.config.server_physical,
            password=self.config.password,
            timeout=self.config.timeout,
            verbose=self.verbose,
            max_info_length=None
        )
    
    def _connect_with_recovery(self) -> bool:
        """Conecta con lógica de recuperación mejorada."""
        max_attempts = 3
        
        for attempt in range(1, max_attempts + 1):
            try:
                # Cerrar cliente anterior si existe
                if self.original_client:
                    try:
                        # Forzar cierre TCP con SO_LINGER para resetear la conexión
                        if self.original_client._sock:
                            try:
                                # SO_LINGER con timeout 0 causa TCP RST en lugar de FIN
                                self.original_client._sock.setsockopt(
                                    socket.SOL_SOCKET, 
                                    socket.SO_LINGER,
                                    struct.pack('ii', 1, 0)  # linger ON, timeout 0
                                )
                                self.original_client._sock.close()
                                logger.debug("🔌 Socket cerrado con TCP RST")
                            except Exception as sock_err:
                                logger.debug(f"Error cerrando socket: {sock_err}")
                        self.original_client.close()
                    except Exception as close_err:
                        logger.debug(f"Error cerrando cliente: {close_err}")
                    finally:
                        self.original_client = None
                        self.optimized_reader = None  # Limpiar también el reader optimizado
                
                # Pausa más larga en reintentos para dejar que el medidor libere la sesión TCP
                if attempt > 1:
                    delay = 2.0 * attempt  # 2s, 4s, 6s
                    logger.debug(f"Esperando {delay}s para estabilización del medidor...")
                    time.sleep(delay)
                elif self.reconnect_count > 0:
                    # En la primera conexión esperar 1.5s para que el medidor se estabilice
                    time.sleep(1.5)
                
                # Crear nuevo cliente
                self.original_client = self._create_original_client()
                
                # Resetear secuencias ANTES de conectar
                self.original_client._send_seq = 0
                self.original_client._recv_seq = 0
                self.original_client._invoke_id = 1
                
                # Conectar
                logger.info(f"🔌 Intentando conectar a {self.config.host}:{self.config.port} (timeout={self.config.timeout}s)...")
                self.original_client.connect()
                logger.info("✓ Conexión DLMS establecida")
                
                # Crear cliente optimizado con FASE 2 (caché de scalers)
                # Nota: Fase 3 (batch) no soportada por este medidor
                self.optimized_reader = OptimizedDLMSReader(self.original_client, use_batch=False)
                
                # Precalentar caché de scalers (primera vez)
                if self.reconnect_count == 0:  # Solo la primera vez
                    obis_codes = [MEASUREMENTS[m][0] for m in self.measurements]
                    logger.info("🔥 Precalentando caché de scalers...")
                    try:
                        self.optimized_reader.warmup_cache(obis_codes)
                        cache_stats = self.optimized_reader.get_cache_stats()
                        logger.info(f"✓ Caché precalentada: {cache_stats['cache_size']} entradas")
                        logger.info(f"⚡ Modo OPTIMIZADO: Caché de scalers activo (Fase 2)")
                    except Exception as e:
                        logger.warning(f"⚠ Error precalentando caché: {e}")
                
                self.reconnect_count += 1
                return True
                
            except Exception as e:
                error_str = str(e)
                logger.warning(f"✗ Intento {attempt}/{max_attempts} falló: {error_str}")
                # Persistir errores HDLC para diagnóstico
                try:
                    lc = error_str.lower()
                    if 'hdlc' in lc or 'invalid hdlc' in lc or 'unterminated' in lc or 'frame boundary' in lc:
                        try:
                            with db.get_session() as session:
                                record_dlms_diagnostic(session, meter_id=0, category='hdlc', message=error_str, severity='warning')
                        except Exception as _inner:
                            logger.debug(f"No se pudo grabar diagnóstico DLMS: {_inner}")
                except Exception:
                    pass
                
                # Si es error de frame boundary, el medidor tiene basura - esperar más
                if "Invalid HDLC frame boundary" in error_str or "Incomplete HDLC frame" in error_str:
                    if attempt < max_attempts:
                        extra_delay = 3.0  # Aumentado de 2.0 a 3.0
                        logger.debug(f"Detectada basura en buffer del medidor, esperando {extra_delay}s extra...")
                        time.sleep(extra_delay)
                
                if attempt < max_attempts:
                    delay = 2.0 * attempt
                    logger.info(f"Reintentando en {delay:.1f}s...")
                    time.sleep(delay)
        
        logger.error("✗ No se pudo establecer conexión después de múltiples intentos")
        return False
    
    def _read_measurement(self, measurement: str) -> Optional[float]:
        """Lee una medición con manejo de errores (OPTIMIZADO con caché)."""
        if measurement not in MEASUREMENTS:
            logger.error(f"Medición desconocida: {measurement}")
            return None
        
        obis_code, description, unit = MEASUREMENTS[measurement]
        
        try:
            if not self.optimized_reader:
                return None
            
            # Pre-limpieza reducida: solo si hay muchos datos esperando
            if self.original_client and self.original_client._sock:
                try:
                    # Check si hay MUCHOS datos esperando (>512 bytes indica problema)
                    ready = select.select([self.original_client._sock], [], [], 0)
                    if ready[0]:
                        # Peek para ver cuántos bytes hay
                        self.original_client._sock.settimeout(0.01)
                        try:
                            garbage = self.original_client._sock.recv(512, socket.MSG_PEEK)
                            if len(garbage) > 100:  # Solo limpiar si hay >100 bytes
                                garbage = self.original_client._sock.recv(1024)
                                logger.debug(f"🧹 Pre-limpieza: {len(garbage)} bytes descartados")
                        except socket.timeout:
                            pass
                        except Exception as peek_error:
                            logger.debug(f"Error en peek/recv buffer: {peek_error}")
                        finally:
                            self.original_client._sock.settimeout(3.0)
                except Exception as select_error:
                    logger.debug(f"Error en select pre-limpieza: {select_error}")
            
            # Leer registro OPTIMIZADO (usa caché de scaler)
            result = self.optimized_reader.read_register_optimized(obis_code)
            
            if result is None:
                logger.warning(f"⚠️ read_register_optimized retornó None para {measurement} ({obis_code})")
                return None
            
            if isinstance(result, tuple) and len(result) >= 1:
                # Primer elemento es el valor ya escalado
                return float(result[0])
            elif isinstance(result, (int, float)):
                return float(result)
            else:
                logger.warning(f"⚠️ Formato de respuesta inesperado para {measurement}: {type(result)} - {result}")
                return None
            
        except Exception as e:
            error_msg = str(e)
            
            # Detectar errores que requieren reconexión
            reconnect_errors = [
                "Unexpected receive sequence",
                "Invalid HDLC frame boundary",
                "Checksum mismatch",
                "timed out",
                "Invoke-ID mismatch",
                "unterminated HDLC address",
                "Socket closed"
            ]
            
            needs_reconnect = any(err in error_msg for err in reconnect_errors)
            
            if needs_reconnect:
                logger.warning(f"⚠ Error crítico en {measurement}: {error_msg}")
                return None
            else:
                logger.error(f"✗ Error leyendo {measurement}: {error_msg}")
                return None
    
    def poll_once(self) -> Dict[str, Optional[float]]:
        """Realiza un ciclo de polling (OPTIMIZADO con CACHE - Fase 2)."""
        results = {}
        start_time = time.time()
        errors_in_cycle = 0
        
        if not self.optimized_reader:
            logger.debug("OptimizedDLMSReader no inicializado - usando valores simulados")
            return {m: None for m in self.measurements}
        
        # LECTURA INDIVIDUAL con CACHE de scalers (Fase 2)
        # Más compatible - no requiere soporte de batch reading
        for measurement in self.measurements:
            obis = MEASUREMENTS[measurement][0]
            
            try:
                # Lee valor individual (usa caché de scaler automáticamente)
                result = self.optimized_reader.read_register_optimized(obis)
                
                if result is not None:
                    value, unit_code, raw = result
                    results[measurement] = float(value)
                else:
                    logger.warning(f"⚠️ Lectura falló para {measurement} ({obis}): result=None")
                    results[measurement] = None
                    errors_in_cycle += 1
                    
            except Exception as e:
                logger.warning(f"⚠️ Excepción leyendo {measurement} ({obis}): {e}")
                results[measurement] = None
                errors_in_cycle += 1
        
        # Verificar si necesitamos reconectar (MEJORADO: 100% errores, no 80%)
        # Solo reconectar si TODAS las lecturas fallaron, no parcialmente
        if errors_in_cycle >= len(self.measurements):  # 100% de errores (antes era 80%)
            logger.warning(f"⚠ Demasiados errores ({errors_in_cycle}/{len(self.measurements)}), reconectando...")
            if self._connect_with_recovery():
                # Reintentar lectura después de reconectar
                logger.info("Reintentando lecturas después de reconexión...")
                return self.poll_once()
        elif errors_in_cycle > 0:
            # Errores parciales: log pero NO reconectar
            logger.warning(f"⚠️ {errors_in_cycle}/{len(self.measurements)} lecturas fallaron (parcial, NO reconectando)")
        
        elapsed = time.time() - start_time
        
        # Log resultados
        values_str = " | ".join([
            f"{k.upper()[:1]}: {v:7.2f} {MEASUREMENTS[k][2]}" if v is not None else f"{k.upper()[:1]}: ---"
            for k, v in results.items()
        ])
        
        logger.info(f"| {values_str} | ({elapsed:.3f}s)")
        
        return results
    
    def run(self):
        """Ejecuta el polling continuo."""
        global running
        
        # Registrar handler de señales
        signal.signal(signal.SIGINT, signal_handler)
        signal.signal(signal.SIGTERM, signal_handler)
        
        logger.info("=" * 70)
        logger.info(f"POLLING CONTINUO DLMS ROBUSTO - {self.config.host}:{self.config.port}")
        logger.info(f"Intervalo: {self.interval}s | Mediciones: {', '.join(self.measurements)}")
        logger.info("Presiona Ctrl+C para detener")
        logger.info("=" * 70)
        
        # Conectar inicialmente
        logger.info("\nConectando al medidor...")
        if not self._connect_with_recovery():
            logger.error("No se pudo conectar al medidor. Abortando.")
            return 1
        
        logger.info("✓ Iniciando polling continuo...\n")
        self.start_time = time.time()
        consecutive_errors = 0
        max_consecutive_errors = 10
        last_drain_time = time.time()
        drain_interval_seconds = 45.0  # Drenar buffer cada 45 segundos (optimizado para tiempo real)
        
        try:
            while running:
                cycle_start = time.time()
                self.total_cycles += 1
                
                # Drenaje preventivo periódico basado en tiempo
                time_since_drain = time.time() - last_drain_time
                if time_since_drain >= drain_interval_seconds:
                    if self.original_client and self.original_client._sock:
                        logger.info(f"🧹 Drenaje preventivo ejecutándose (cada {drain_interval_seconds}s)...")
                        bytes_drained = 0
                        
                        # Limpiar socket
                        self.original_client._sock.settimeout(0.03)
                        try:
                            for _ in range(3):  # Hasta 3 intentos
                                chunk = self.original_client._sock.recv(2048)
                                if not chunk:
                                    break
                                bytes_drained += len(chunk)
                        except socket.timeout:
                            pass
                        finally:
                            self.original_client._sock.settimeout(self.config.timeout)
                        
                        if bytes_drained > 0:
                            logger.warning(f"🧹 Drenaje preventivo: {bytes_drained} bytes residuales eliminados")
                        else:
                            logger.info(f"🧹 Drenaje preventivo completado: Buffer limpio (0 bytes)")
                    
                    last_drain_time = time.time()  # Actualizar timestamp del último drenaje
                    time.sleep(0.1)  # Dar 100ms al medidor para estabilizar después del drenaje
                
                # Realizar lectura
                results = self.poll_once()
                
                # Verificar si hubo éxito
                if any(v is not None for v in results.values()):
                    self.successful_cycles += 1
                    consecutive_errors = 0
                else:
                    consecutive_errors += 1
                    
                    # Si hay demasiados errores consecutivos, abrir circuit breaker
                    if consecutive_errors >= max_consecutive_errors:
                        logger.error(f"⚡ Demasiados errores consecutivos ({consecutive_errors}). Pausando polling por 30s...")
                        time.sleep(30)
                        consecutive_errors = 0
                        
                        # Intentar reconectar
                        if not self._connect_with_recovery():
                            logger.error("No se pudo reconectar. Abortando.")
                            break
                
                # Esperar hasta el próximo ciclo
                elapsed = time.time() - cycle_start
                sleep_time = max(0, self.interval - elapsed)
                if sleep_time > 0 and running:
                    time.sleep(sleep_time)
        
        except Exception as e:
            logger.error(f"Error fatal en polling: {e}")
            traceback.print_exc()
        
        finally:
            # Estadísticas finales
            total_time = time.time() - self.start_time if self.start_time else 0
            success_rate = (self.successful_cycles / self.total_cycles * 100) if self.total_cycles > 0 else 0
            
            logger.info("\n" + "=" * 70)
            logger.info("ESTADÍSTICAS FINALES")
            logger.info("=" * 70)
            logger.info(f"Tiempo total:        {total_time:.2f} segundos")
            logger.info(f"Ciclos totales:      {self.total_cycles}")
            logger.info(f"Ciclos exitosos:     {self.successful_cycles}")
            logger.info(f"Ciclos fallidos:     {self.total_cycles - self.successful_cycles}")
            logger.info(f"Tasa de éxito:       {success_rate:.1f}%")
            logger.info(f"Reconexiones:        {self.reconnect_count}")
            logger.info("=" * 70)
            
            # Cerrar conexión
            if self.original_client:
                try:
                    logger.info("\nCerrando conexión DLMS...")
                    self.original_client.close()
                    logger.info("✓ Conexión cerrada correctamente")
                except Exception as e:
                    logger.warning(f"Error cerrando conexión: {e}")
        
        return 0

def main():
    parser = argparse.ArgumentParser(description="Polling DLMS robusto para producción")
    parser.add_argument("--host", default="192.168.1.127", help="Host del medidor")
    parser.add_argument("--port", type=int, default=3333, help="Puerto del medidor")
    parser.add_argument("--password", default="22222222", help="Contraseña DLMS")
    parser.add_argument("--interval", type=float, default=1.0, help="Intervalo de polling (segundos)")
    parser.add_argument("--measurements", nargs="+", default=["voltage_l1", "current_l1"],
                       help="Mediciones a leer")
    parser.add_argument("--verbose", action="store_true", help="Modo verbose")
    
    args = parser.parse_args()
    
    poller = ProductionDLMSPoller(
        host=args.host,
        port=args.port,
        password=args.password,
        interval=args.interval,
        measurements=args.measurements,
        verbose=args.verbose
    )
    
    return poller.run()

if __name__ == "__main__":
    sys.exit(main())
