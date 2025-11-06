#!/usr/bin/env python3
"""
Supervisor QoS con acciones correctivas automáticas.

Monitorea cada 20 segundos y toma acciones cuando detecta problemas:
- Reinicia servicios caídos
- Detecta telemetría estancada
- Reinicia bridge si no hay datos DLMS
- Alertas y registro de eventos
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
CHECK_INTERVAL = 20  # Segundos entre checks
TELEMETRY_MAX_AGE = 45  # Máximo tiempo sin datos nuevos (segundos)
MAX_FAILED_CHECKS = 3  # Fallos consecutivos antes de acción drástica


class QoSSupervisor:
    def __init__(self, duration_minutes: int = 10):
        self.token = None
        self.duration = duration_minutes * 60  # Convertir a segundos
        self.start_time = time.time()
        self.checks_performed = 0
        self.issues_detected = 0
        self.actions_taken = 0
        self.failed_checks = 0
        self.last_telemetry_ts = 0
        
        print("=" * 80)
        print("🛡️  QoS SUPERVISOR - Monitoreo con Acciones Correctivas")
        print("=" * 80)
        print(f"⏱️  Duración: {duration_minutes} minutos")
        print(f"🔍 Intervalo de chequeo: {CHECK_INTERVAL}s")
        print(f"⚠️  Max edad de telemetría: {TELEMETRY_MAX_AGE}s")
        print(f"🚨 Fallos antes de reinicio: {MAX_FAILED_CHECKS}")
        print("=" * 80)
        print()
    
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
            print(f"❌ Error autenticando: {e}")
            return False
    
    def restart_service(self, service_name: str) -> bool:
        """Reinicia un servicio systemd."""
        try:
            print(f"   🔄 Reiniciando {service_name}...")
            result = subprocess.run(
                ["sudo", "systemctl", "restart", service_name],
                capture_output=True,
                text=True,
                timeout=30
            )
            
            if result.returncode == 0:
                print(f"   ✅ {service_name} reiniciado exitosamente")
                self.actions_taken += 1
                time.sleep(5)  # Esperar que el servicio inicie
                return True
            else:
                print(f"   ❌ Error reiniciando {service_name}: {result.stderr}")
                return False
        except Exception as e:
            print(f"   ❌ Excepción reiniciando {service_name}: {e}")
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
            status = "Active" if active else result.stdout.strip()
            return active, status
        except Exception as e:
            return False, f"Error: {e}"
    
    def check_telemetry(self) -> Tuple[bool, Dict]:
        """Verifica telemetría y retorna si está fresca y los datos."""
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
            
            # Verificar frescura de datos
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
            
            # Detectar si los datos están estancados (mismo timestamp)
            stale = (self.last_telemetry_ts > 0 and latest_ts == self.last_telemetry_ts)
            self.last_telemetry_ts = latest_ts
            
            return fresh and not stale, {
                "age_seconds": age_seconds,
                "data_points": data_points,
                "timestamp": latest_ts,
                "stale": stale
            }
            
        except Exception as e:
            return False, {"error": str(e)}
    
    def take_corrective_action(self, issue: str):
        """Toma acción correctiva según el problema detectado."""
        print(f"\n⚡ ACCIÓN CORRECTIVA REQUERIDA: {issue}")
        print(f"   Fallo #{self.failed_checks} de {MAX_FAILED_CHECKS}")
        
        if "Bridge" in issue or "telemetry" in issue.lower() or "stale" in issue.lower():
            # Problema con el bridge DLMS
            print("   📋 Diagnóstico: Problema con bridge DLMS o datos")
            
            if self.failed_checks >= 1:  # Más agresivo - reiniciar después de 1 fallo
                if self.restart_service("dlms-mosquitto-bridge.service"):
                    self.failed_checks = 0  # Reset contador
                    print("   ✅ Bridge reiniciado, esperando estabilización...")
                    time.sleep(10)
                else:
                    print("   ⚠️  No se pudo reiniciar el bridge")
        
        elif "Gateway" in issue:
            # Problema con ThingsBoard Gateway
            print("   📋 Diagnóstico: Problema con ThingsBoard Gateway")
            if self.failed_checks >= 2:
                if self.restart_service("thingsboard-gateway.service"):
                    self.failed_checks = 0
                    time.sleep(10)
        
        elif "Mosquitto" in issue:
            # Problema con broker MQTT
            print("   📋 Diagnóstico: Problema con Mosquitto")
            if self.failed_checks >= 1:
                if self.restart_service("mosquitto.service"):
                    self.failed_checks = 0
                    time.sleep(5)
                    # También reiniciar el bridge que depende de Mosquitto
                    self.restart_service("dlms-mosquitto-bridge.service")
    
    def perform_check(self) -> bool:
        """Realiza un chequeo completo del sistema."""
        self.checks_performed += 1
        elapsed = time.time() - self.start_time
        remaining = self.duration - elapsed
        
        print(f"\n{'='*80}")
        print(f"🔍 Check #{self.checks_performed} - {datetime.now().strftime('%H:%M:%S')}")
        print(f"⏱️  Tiempo restante: {int(remaining/60)}m {int(remaining%60)}s")
        print(f"{'='*80}")
        
        all_ok = True
        issues = []
        
        # 1. Verificar servicios
        services = {
            "Mosquitto": "mosquitto.service",
            "DLMS Bridge": "dlms-mosquitto-bridge.service",
            "TB Gateway": "thingsboard-gateway.service"
        }
        
        print("\n📊 Servicios:")
        for name, service in services.items():
            active, status = self.check_service(service)
            symbol = "✅" if active else "❌"
            print(f"   {symbol} {name:20s}: {status}")
            
            if not active:
                all_ok = False
                issues.append(f"{name} service inactive")
                self.issues_detected += 1
        
        # 2. Verificar telemetría
        print("\n📈 Telemetría:")
        fresh, data = self.check_telemetry()
        
        if fresh:
            print(f"   ✅ Datos frescos ({data.get('age_seconds', 0):.1f}s)")
            print(f"      Data points: {data.get('data_points', 0)}")
            self.failed_checks = 0  # Reset en caso de éxito
        else:
            all_ok = False
            
            if "error" in data:
                print(f"   ❌ Error: {data['error']}")
                issues.append(f"Telemetry error: {data['error']}")
            elif data.get('stale'):
                print(f"   ⚠️  Datos estancados (mismo timestamp)")
                issues.append("Telemetry stale - Bridge not sending data")
            else:
                age = data.get('age_seconds', 0)
                print(f"   ❌ Datos obsoletos ({age:.1f}s)")
                issues.append(f"Telemetry too old ({age:.1f}s)")
            
            self.issues_detected += 1
            self.failed_checks += 1
        
        # 3. Tomar acciones si es necesario
        if not all_ok:
            print(f"\n⚠️  {len(issues)} problema(s) detectado(s):")
            for issue in issues:
                print(f"   • {issue}")
            
            # Decidir acción
            if self.failed_checks >= 1:
                self.take_corrective_action(issues[0])
        else:
            print("\n✅ Sistema operando normalmente")
        
        # Resumen
        print(f"\n📊 Resumen:")
        print(f"   Checks: {self.checks_performed}")
        print(f"   Problemas detectados: {self.issues_detected}")
        print(f"   Acciones tomadas: {self.actions_taken}")
        print(f"   Fallos consecutivos: {self.failed_checks}")
        
        return all_ok
    
    def run(self):
        """Ejecuta el supervisor por la duración especificada."""
        # Autenticar
        if not self.authenticate():
            print("❌ No se pudo autenticar. Abortando.")
            return 1
        
        print("🚀 Supervisor iniciado\n")
        
        try:
            while (time.time() - self.start_time) < self.duration:
                self.perform_check()
                
                # Calcular tiempo restante
                elapsed = time.time() - self.start_time
                remaining = self.duration - elapsed
                
                if remaining > CHECK_INTERVAL:
                    print(f"\n⏳ Próximo check en {CHECK_INTERVAL}s...")
                    time.sleep(CHECK_INTERVAL)
                elif remaining > 0:
                    print(f"\n⏳ Último check en {int(remaining)}s...")
                    time.sleep(remaining)
                else:
                    break
            
        except KeyboardInterrupt:
            print("\n\n⚠️  Supervisor detenido por usuario")
            return 1
        
        # Resumen final
        print("\n" + "=" * 80)
        print("📋 RESUMEN FINAL")
        print("=" * 80)
        duration_mins = (time.time() - self.start_time) / 60
        print(f"⏱️  Duración real: {duration_mins:.1f} minutos")
        print(f"🔍 Checks realizados: {self.checks_performed}")
        print(f"⚠️  Problemas detectados: {self.issues_detected}")
        print(f"🔧 Acciones correctivas: {self.actions_taken}")
        
        if self.issues_detected == 0:
            print("\n✅ Sistema estuvo estable todo el tiempo")
            status = 0
        elif self.actions_taken > 0:
            print(f"\n⚡ Se tomaron {self.actions_taken} acciones correctivas")
            status = 0 if self.failed_checks == 0 else 1
        else:
            print("\n⚠️  Problemas detectados pero sin acciones requeridas")
            status = 1
        
        print("=" * 80)
        
        return status


def main():
    if len(sys.argv) < 2:
        print("Uso: python3 qos_supervisor.py <minutos>")
        print("Ejemplo: python3 qos_supervisor.py 10")
        sys.exit(1)
    
    try:
        duration = int(sys.argv[1])
        if duration <= 0 or duration > 120:
            print("❌ Duración debe estar entre 1 y 120 minutos")
            sys.exit(1)
    except ValueError:
        print("❌ Duración debe ser un número entero")
        sys.exit(1)
    
    supervisor = QoSSupervisor(duration_minutes=duration)
    sys.exit(supervisor.run())


if __name__ == "__main__":
    main()
