#!/usr/bin/env python3
"""
Supervisor QoS como servicio - Ejecuta continuamente con intervalos de descanso.

Este supervisor:
- Ejecuta ciclos de monitoreo de 30 minutos
- Descansa 5 minutos entre ciclos
- Registra todo en journald para consulta
- Toma acciones correctivas automáticas
"""

import requests
import subprocess
import json
import time
import sys
from datetime import datetime
from typing import Dict, Tuple

TB_HOST = "localhost"
TB_PORT = 8080
TB_USERNAME = "tenant@thingsboard.org"
TB_PASSWORD = "tenant"

# Configuración
CHECK_INTERVAL = 10          # Segundos entre checks (reducido de 30s a 10s para detección más rápida)
TELEMETRY_MAX_AGE = 20       # Máximo segundos sin nueva telemetría (reducido de 60s a 20s)
CYCLE_DURATION = 30 * 60     # Duración de cada ciclo de monitoreo (30 minutos)
REST_DURATION = 2 * 60       # Tiempo de descanso entre ciclos (reducido de 5min a 2min)


class QoSSupervisorService:
    def __init__(self):
        self.token = None
        self.cycle_number = 0
        self.failed_checks = 0
        self.last_telemetry_ts = 0
        
    def log(self, level: str, message: str):
        """Log con formato para journald."""
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        prefix = {
            'INFO': 'ℹ️ ',
            'WARN': '⚠️ ',
            'ERROR': '❌',
            'SUCCESS': '✅',
            'ACTION': '⚡'
        }.get(level, '')
        
        print(f"[{timestamp}] {prefix} {message}", flush=True)
    
    def authenticate(self) -> bool:
        """Autenticar en ThingsBoard."""
        try:
            url = f"http://{TB_HOST}:{TB_PORT}/api/auth/login"
            payload = {"username": TB_USERNAME, "password": TB_PASSWORD}
            response = requests.post(url, json=payload, timeout=10)
            response.raise_for_status()
            self.token = response.json()["token"]
            return True
        except Exception as e:
            self.log('ERROR', f"Autenticación falló: {e}")
            return False
    
    def restart_service(self, service_name: str) -> bool:
        """Reinicia un servicio systemd."""
        try:
            self.log('ACTION', f"Reiniciando {service_name}")
            result = subprocess.run(
                ["sudo", "systemctl", "restart", service_name],
                capture_output=True,
                text=True,
                timeout=30
            )
            
            if result.returncode == 0:
                self.log('SUCCESS', f"{service_name} reiniciado")
                time.sleep(5)
                return True
            else:
                self.log('ERROR', f"Fallo reiniciando {service_name}: {result.stderr}")
                return False
        except Exception as e:
            self.log('ERROR', f"Excepción reiniciando {service_name}: {e}")
            return False
    
    def check_service(self, service_name: str) -> Tuple[bool, str]:
        """Verifica estado de un servicio."""
        try:
            result = subprocess.run(
                ["systemctl", "is-active", service_name],
                capture_output=True,
                text=True,
                timeout=5
            )
            active = result.stdout.strip() == "active"
            status = "active" if active else result.stdout.strip()
            return active, status
        except Exception as e:
            return False, f"error: {e}"
    
    def check_telemetry(self) -> Tuple[bool, Dict]:
        """Verifica telemetría."""
        try:
            # Buscar dispositivo
            url = f"http://{TB_HOST}:{TB_PORT}/api/tenant/devices?pageSize=100&page=0"
            headers = {"X-Authorization": f"Bearer {self.token}"}
            response = requests.get(url, headers=headers, timeout=10)
            response.raise_for_status()
            
            devices = response.json().get("data", [])
            device = None
            for d in devices:
                if "DLMS-Meter-01" in d["name"]:
                    device = d
                    break
            
            if not device:
                return False, {"error": "Device not found"}
            
            # Obtener telemetría
            device_id = device["id"]["id"]
            url = f"http://{TB_HOST}:{TB_PORT}/api/plugins/telemetry/DEVICE/{device_id}/values/timeseries"
            response = requests.get(url, headers=headers, timeout=10)
            response.raise_for_status()
            
            telemetry = response.json()
            
            if not telemetry:
                return False, {"error": "No telemetry"}
            
            # Verificar frescura
            latest_ts = 0
            data_points = 0
            
            for key, values in telemetry.items():
                if values and len(values) > 0:
                    data_points += 1
                    ts = values[0]["ts"]
                    if ts > latest_ts:
                        latest_ts = ts
            
            age_seconds = (time.time() * 1000 - latest_ts) / 1000
            fresh = age_seconds < TELEMETRY_MAX_AGE
            
            # Detectar estancamiento
            stale = (self.last_telemetry_ts > 0 and latest_ts == self.last_telemetry_ts)
            self.last_telemetry_ts = latest_ts
            
            return fresh and not stale, {
                "age": age_seconds,
                "points": data_points,
                "timestamp": latest_ts,
                "stale": stale
            }
            
        except Exception as e:
            return False, {"error": str(e)}
    
    def take_corrective_action(self, issue: str):
        """Toma acción correctiva."""
        self.log('WARN', f"Problema detectado: {issue}")
        self.log('INFO', f"Fallo consecutivo #{self.failed_checks}")
        
        if "telemetry" in issue.lower() or "stale" in issue.lower() or "Bridge" in issue:
            if self.failed_checks >= 1:
                if self.restart_service("dlms-mosquitto-bridge.service"):
                    self.failed_checks = 0
                    self.log('SUCCESS', "Bridge recuperado")
                    time.sleep(10)
        
        elif "Gateway" in issue:
            if self.failed_checks >= 2:
                if self.restart_service("thingsboard-gateway.service"):
                    self.failed_checks = 0
                    time.sleep(10)
        
        elif "Mosquitto" in issue:
            if self.failed_checks >= 1:
                if self.restart_service("mosquitto.service"):
                    self.failed_checks = 0
                    time.sleep(5)
                    self.restart_service("dlms-mosquitto-bridge.service")
    
    def perform_check(self, check_number: int) -> bool:
        """Realiza un chequeo del sistema."""
        all_ok = True
        issues = []
        
        # Verificar servicios
        services = {
            "Mosquitto": "mosquitto.service",
            "DLMS Bridge": "dlms-mosquitto-bridge.service",
            "TB Gateway": "thingsboard-gateway.service"
        }
        
        for name, service in services.items():
            active, status = self.check_service(service)
            if not active:
                all_ok = False
                issue = f"{name} inactive ({status})"
                issues.append(issue)
                self.log('ERROR', issue)
        
        # Verificar telemetría
        fresh, data = self.check_telemetry()
        
        if fresh:
            age = data.get('age', 0)
            self.log('SUCCESS', f"Telemetría OK (edad: {age:.1f}s, puntos: {data.get('points', 0)})")
            self.failed_checks = 0
        else:
            all_ok = False
            
            if "error" in data:
                issue = f"Telemetry error: {data['error']}"
            elif data.get('stale'):
                issue = "Telemetry stale (mismo timestamp)"
            else:
                age = data.get('age', 0)
                issue = f"Telemetry obsoleta ({age:.1f}s)"
            
            issues.append(issue)
            self.log('ERROR', issue)
            self.failed_checks += 1
        
        # Tomar acciones
        if not all_ok:
            for issue in issues:
                self.take_corrective_action(issue)
        
        return all_ok
    
    def run_cycle(self):
        """Ejecuta un ciclo de monitoreo."""
        self.cycle_number += 1
        cycle_start = time.time()
        checks_in_cycle = 0
        problems_in_cycle = 0
        actions_in_cycle = 0
        
        self.log('INFO', f"═══════════════════════════════════════════════════════════")
        self.log('INFO', f"CICLO #{self.cycle_number} INICIADO - Duración: {CYCLE_DURATION//60} minutos")
        self.log('INFO', f"═══════════════════════════════════════════════════════════")
        
        # Re-autenticar al inicio de cada ciclo
        if not self.authenticate():
            self.log('ERROR', "No se pudo autenticar. Esperando próximo ciclo.")
            return
        
        try:
            while (time.time() - cycle_start) < CYCLE_DURATION:
                checks_in_cycle += 1
                
                self.log('INFO', f"--- Check #{checks_in_cycle} ---")
                
                all_ok = self.perform_check(checks_in_cycle)
                
                if not all_ok:
                    problems_in_cycle += 1
                
                # Esperar siguiente check
                elapsed = time.time() - cycle_start
                remaining = CYCLE_DURATION - elapsed
                
                if remaining > CHECK_INTERVAL:
                    time.sleep(CHECK_INTERVAL)
                elif remaining > 0:
                    time.sleep(remaining)
                else:
                    break
        
        except Exception as e:
            self.log('ERROR', f"Excepción en ciclo: {e}")
        
        # Resumen del ciclo
        cycle_duration = (time.time() - cycle_start) / 60
        self.log('INFO', f"═══════════════════════════════════════════════════════════")
        self.log('INFO', f"CICLO #{self.cycle_number} COMPLETADO")
        self.log('INFO', f"Duración: {cycle_duration:.1f} min | Checks: {checks_in_cycle} | Problemas: {problems_in_cycle}")
        self.log('INFO', f"═══════════════════════════════════════════════════════════")
    
    def run(self):
        """Loop principal del servicio."""
        self.log('INFO', "🛡️  QoS Supervisor Service INICIADO")
        self.log('INFO', f"Configuración: Check cada {CHECK_INTERVAL}s, Ciclos de {CYCLE_DURATION//60}min")
        
        while True:
            try:
                # Ejecutar ciclo
                self.run_cycle()
                
                # Descanso entre ciclos
                self.log('INFO', f"💤 Descanso de {REST_DURATION//60} minutos antes del próximo ciclo")
                time.sleep(REST_DURATION)
                
            except KeyboardInterrupt:
                self.log('WARN', "Servicio detenido por usuario")
                break
            except Exception as e:
                self.log('ERROR', f"Error inesperado: {e}")
                self.log('INFO', "Esperando 60s antes de reintentar")
                time.sleep(60)
        
        self.log('INFO', "🛡️  QoS Supervisor Service DETENIDO")


def main():
    supervisor = QoSSupervisorService()
    supervisor.run()


if __name__ == "__main__":
    main()
