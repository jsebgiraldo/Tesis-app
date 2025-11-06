#!/usr/bin/env python3
"""
Monitor de QoS para el sistema DLMS → ThingsBoard.

Verifica:
- Conectividad DLMS
- Estado de servicios
- Flujo de datos MQTT
- Telemetría en ThingsBoard
- Latencias y tasas de éxito
"""

import requests
import subprocess
import json
import time
import sys
from datetime import datetime, timedelta
from typing import Dict, Optional

TB_HOST = "localhost"
TB_PORT = 8080
TB_USERNAME = "tenant@thingsboard.org"
TB_PASSWORD = "tenant"


class QoSMonitor:
    def __init__(self):
        self.token = None
        self.results = {}
    
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
    
    def check_service(self, service_name: str) -> Dict:
        """Verifica estado de un servicio systemd."""
        try:
            result = subprocess.run(
                ["systemctl", "is-active", service_name],
                capture_output=True,
                text=True,
                timeout=5
            )
            active = result.stdout.strip() == "active"
            
            # Obtener uptime
            result = subprocess.run(
                ["systemctl", "show", service_name, "--property=ActiveEnterTimestamp"],
                capture_output=True,
                text=True,
                timeout=5
            )
            uptime = result.stdout.strip().split("=")[1] if "=" in result.stdout else "N/A"
            
            return {
                "status": "✅ Active" if active else "❌ Inactive",
                "active": active,
                "uptime": uptime
            }
        except Exception as e:
            return {
                "status": f"❌ Error: {e}",
                "active": False,
                "uptime": "N/A"
            }
    
    def check_mqtt_broker(self) -> Dict:
        """Verifica que Mosquitto esté escuchando."""
        try:
            result = subprocess.run(
                ["netstat", "-tuln"],
                capture_output=True,
                text=True,
                timeout=5
            )
            listening = ":1884" in result.stdout
            
            return {
                "status": "✅ Listening" if listening else "❌ Not listening",
                "listening": listening,
                "port": 1884
            }
        except Exception as e:
            return {
                "status": f"❌ Error: {e}",
                "listening": False,
                "port": 1884
            }
    
    def check_dlms_connectivity(self) -> Dict:
        """Verifica conectividad con el medidor DLMS."""
        try:
            result = subprocess.run(
                ["ping", "-c", "3", "-W", "2", "192.168.1.127"],
                capture_output=True,
                text=True,
                timeout=10
            )
            
            # Parsear latencia
            lines = result.stdout.split("\n")
            latency = "N/A"
            packet_loss = "N/A"
            
            for line in lines:
                if "avg" in line:
                    parts = line.split("/")
                    if len(parts) >= 5:
                        latency = f"{float(parts[4]):.2f} ms"
                if "packet loss" in line:
                    packet_loss = line.split(",")[2].strip()
            
            reachable = result.returncode == 0
            
            return {
                "status": "✅ Reachable" if reachable else "❌ Unreachable",
                "reachable": reachable,
                "latency": latency,
                "packet_loss": packet_loss
            }
        except Exception as e:
            return {
                "status": f"❌ Error: {e}",
                "reachable": False,
                "latency": "N/A",
                "packet_loss": "N/A"
            }
    
    def check_telemetry(self) -> Dict:
        """Verifica telemetría en ThingsBoard."""
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
                return {
                    "status": "❌ Device not found",
                    "device_found": False
                }
            
            # Obtener telemetría
            device_id = device["id"]["id"]
            url = f"http://{TB_HOST}:{TB_PORT}/api/plugins/telemetry/DEVICE/{device_id}/values/timeseries"
            response = requests.get(url, headers=headers, timeout=10)
            response.raise_for_status()
            
            telemetry = response.json()
            
            if not telemetry:
                return {
                    "status": "❌ No telemetry",
                    "device_found": True,
                    "has_data": False
                }
            
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
            age_str = f"{age_seconds:.1f}s ago"
            
            fresh = age_seconds < 30  # Datos frescos si < 30s
            
            return {
                "status": f"✅ Fresh ({age_str})" if fresh else f"⚠️ Stale ({age_str})",
                "device_found": True,
                "has_data": True,
                "data_points": data_points,
                "age_seconds": age_seconds,
                "fresh": fresh,
                "latest_timestamp": datetime.fromtimestamp(latest_ts / 1000).strftime("%Y-%m-%d %H:%M:%S")
            }
            
        except Exception as e:
            return {
                "status": f"❌ Error: {e}",
                "device_found": False,
                "has_data": False
            }
    
    def run_check(self):
        """Ejecuta todas las verificaciones."""
        print("=" * 80)
        print(f"🔍 QoS Monitor - {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print("=" * 80)
        print()
        
        # Autenticar
        print("🔑 Autenticando...")
        if not self.authenticate():
            return False
        print("✅ Autenticado\n")
        
        # 1. Servicios
        print("📊 Estado de Servicios:")
        services = {
            "Mosquitto": "mosquitto.service",
            "DLMS Bridge": "dlms-mosquitto-bridge.service",
            "TB Gateway": "thingsboard-gateway.service"
        }
        
        for name, service in services.items():
            result = self.check_service(service)
            print(f"   {name:20s}: {result['status']}")
            if result['active']:
                print(f"      {'Uptime:':<18s} {result['uptime']}")
        print()
        
        # 2. Mosquitto
        print("🦟 Broker MQTT:")
        mqtt = self.check_mqtt_broker()
        print(f"   Port 1884:            {mqtt['status']}")
        print()
        
        # 3. DLMS
        print("🔌 Medidor DLMS:")
        dlms = self.check_dlms_connectivity()
        print(f"   192.168.1.127:        {dlms['status']}")
        if dlms['reachable']:
            print(f"      {'Latency:':<18s} {dlms['latency']}")
            print(f"      {'Packet Loss:':<18s} {dlms['packet_loss']}")
        print()
        
        # 4. Telemetría
        print("📈 Telemetría ThingsBoard:")
        telem = self.check_telemetry()
        print(f"   Status:               {telem['status']}")
        if telem.get('has_data'):
            print(f"      {'Data Points:':<18s} {telem['data_points']}")
            print(f"      {'Last Update:':<18s} {telem['latest_timestamp']}")
            print(f"      {'Age:':<18s} {telem['age_seconds']:.1f}s")
        print()
        
        # Resumen
        print("=" * 80)
        all_ok = (
            all(self.check_service(s)['active'] for s in services.values()) and
            mqtt['listening'] and
            dlms['reachable'] and
            telem.get('fresh', False)
        )
        
        if all_ok:
            print("✅ SISTEMA OPERANDO NORMALMENTE")
        else:
            print("⚠️ SE DETECTARON PROBLEMAS")
        print("=" * 80)
        
        return all_ok


def main():
    monitor = QoSMonitor()
    
    if len(sys.argv) > 1 and sys.argv[1] == "--watch":
        # Modo monitoreo continuo
        print("🔄 Modo monitoreo continuo (Ctrl+C para detener)\n")
        try:
            while True:
                monitor.run_check()
                print("\n⏳ Esperando 30 segundos...\n")
                time.sleep(30)
        except KeyboardInterrupt:
            print("\n\n⚠️ Monitoreo detenido por usuario")
    else:
        # Verificación única
        success = monitor.run_check()
        sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
