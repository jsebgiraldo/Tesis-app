#!/usr/bin/env python3
"""
QoS Health Check System - Diagnóstico rápido del sistema DLMS Bridge
"""

import sys
import time
import socket
import requests
import subprocess
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from admin.database import Database, get_all_meters
import paho.mqtt.client as mqtt


class Colors:
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    BOLD = '\033[1m'
    END = '\033[0m'


def run_health_check():
    """Ejecuta verificación de salud completa"""
    print(f"\n{Colors.BOLD}{Colors.BLUE}╔═══════════════════════════════════════════════════════════════════╗{Colors.END}")
    print(f"{Colors.BOLD}{Colors.BLUE}║     DLMS Bridge - QoS Health Check ({datetime.now().strftime('%Y-%m-%d %H:%M:%S')})     ║{Colors.END}")
    print(f"{Colors.BOLD}{Colors.BLUE}╚═══════════════════════════════════════════════════════════════════╝{Colors.END}\n")
    
    checks_passed = 0
    warnings = 0
    errors = 0
    
    # 1. Check Database
    print(f"{Colors.BOLD}{Colors.CYAN}📊 DATABASE{Colors.END}")
    try:
        db = Database('data/admin.db')
        with db.get_session() as session:
            meters = get_all_meters(session)
            print(f"✅ {Colors.GREEN}Database connected - {len(meters)} meters configured{Colors.END}")
            checks_passed += 1
            
            for meter in meters:
                status_icon = "✅" if meter.tb_enabled else "⚠️ "
                status_color = Colors.GREEN if meter.tb_enabled else Colors.YELLOW
                print(f"  {status_icon} {meter.name}: {meter.ip_address}:{meter.port} | TB: {meter.tb_host}:{meter.tb_port}")
                if not meter.tb_enabled:
                    warnings += 1
    except Exception as e:
        print(f"❌ {Colors.RED}Database error: {e}{Colors.END}")
        errors += 1
    
    # 2. Check Services
    print(f"\n{Colors.BOLD}{Colors.CYAN}🔧 SERVICES{Colors.END}")
    for service in ['dlms-multi-meter.service', 'dlms-admin-api.service']:
        try:
            result = subprocess.run(['systemctl', 'is-active', service],
                                  capture_output=True, text=True, timeout=5)
            if result.stdout.strip() == 'active':
                print(f"✅ {Colors.GREEN}{service} is running{Colors.END}")
                checks_passed += 1
            else:
                print(f"❌ {Colors.RED}{service} is {result.stdout.strip()}{Colors.END}")
                errors += 1
        except Exception as e:
            print(f"❌ {Colors.RED}{service} check failed: {e}{Colors.END}")
            errors += 1
    
    # 3. Check API
    print(f"\n{Colors.BOLD}{Colors.CYAN}🌐 API ENDPOINTS{Colors.END}")
    try:
        response = requests.get('http://localhost:8000/health', timeout=5)
        if response.status_code == 200:
            print(f"✅ {Colors.GREEN}API responding (200 OK){Colors.END}")
            checks_passed += 1
        else:
            print(f"⚠️  {Colors.YELLOW}API returned {response.status_code}{Colors.END}")
            warnings += 1
    except Exception as e:
        print(f"❌ {Colors.RED}API not accessible: {e}{Colors.END}")
        errors += 1
    
    # 4. Check DLMS Connectivity
    print(f"\n{Colors.BOLD}{Colors.CYAN}⚡ DLMS METER{Colors.END}")
    try:
        db = Database('data/admin.db')
        with db.get_session() as session:
            meters = get_all_meters(session)
            for meter in meters:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(3)
                try:
                    result = sock.connect_ex((meter.ip_address, meter.port))
                    if result == 0:
                        print(f"✅ {Colors.GREEN}{meter.name} ({meter.ip_address}:{meter.port}) - Port open{Colors.END}")
                        checks_passed += 1
                    else:
                        print(f"❌ {Colors.RED}{meter.name} - Cannot connect (error {result}){Colors.END}")
                        errors += 1
                except Exception as e:
                    print(f"❌ {Colors.RED}{meter.name} - Connection error: {e}{Colors.END}")
                    errors += 1
                finally:
                    sock.close()
    except Exception as e:
        print(f"❌ {Colors.RED}DLMS check failed: {e}{Colors.END}")
        errors += 1
    
    # 5. Check MQTT/ThingsBoard
    print(f"\n{Colors.BOLD}{Colors.CYAN}📡 MQTT/THINGSBOARD{Colors.END}")
    try:
        db = Database('data/admin.db')
        with db.get_session() as session:
            meters = get_all_meters(session)
            for meter in meters:
                if not meter.tb_enabled:
                    print(f"⚠️  {Colors.YELLOW}{meter.name} - ThingsBoard disabled{Colors.END}")
                    warnings += 1
                    continue
                
                connected = [False]
                error_code = [None]
                
                def on_connect(client, userdata, flags, rc):
                    connected[0] = (rc == 0)
                    error_code[0] = rc
                
                try:
                    client = mqtt.Client(client_id=f"qos_check_{int(time.time())}")
                    client.username_pw_set(meter.tb_token)
                    client.on_connect = on_connect
                    
                    client.connect(meter.tb_host, meter.tb_port, keepalive=10)
                    client.loop_start()
                    time.sleep(2)
                    client.loop_stop()
                    client.disconnect()
                    
                    if connected[0]:
                        print(f"✅ {Colors.GREEN}{meter.name} - Connected to {meter.tb_host}:{meter.tb_port}{Colors.END}")
                        checks_passed += 1
                    else:
                        error_msgs = {
                            7: "Token conflict (another client using same token)",
                            4: "Bad username/password",
                            5: "Not authorized"
                        }
                        msg = error_msgs.get(error_code[0], f"Error code {error_code[0]}")
                        print(f"❌ {Colors.RED}{meter.name} - {msg}{Colors.END}")
                        if error_code[0] == 7:
                            print(f"   {Colors.YELLOW}→ Stop other MQTT clients or use different token{Colors.END}")
                        errors += 1
                        
                except Exception as e:
                    print(f"❌ {Colors.RED}{meter.name} - MQTT error: {e}{Colors.END}")
                    errors += 1
    except Exception as e:
        print(f"❌ {Colors.RED}MQTT check failed: {e}{Colors.END}")
        errors += 1
    
    # 6. Check Network Stats Collection
    print(f"\n{Colors.BOLD}{Colors.CYAN}📊 NETWORK STATISTICS{Colors.END}")
    try:
        from admin.database import NetworkMetric
        from sqlalchemy import func
        
        db = Database('data/admin.db')
        with db.get_session() as session:
            count = session.query(func.count(NetworkMetric.id)).scalar()
            if count > 0:
                latest = session.query(NetworkMetric).order_by(NetworkMetric.timestamp.desc()).first()
                age = (datetime.now() - latest.timestamp).total_seconds()
                print(f"✅ {Colors.GREEN}Network stats collecting - {count} records (last: {age:.0f}s ago){Colors.END}")
                print(f"   DLMS: {latest.dlms_requests_sent:,} requests | MQTT: {latest.mqtt_messages_sent:,} messages")
                checks_passed += 1
            else:
                print(f"⚠️  {Colors.YELLOW}No network stats yet (wait 60s for first cycle){Colors.END}")
                warnings += 1
    except Exception as e:
        print(f"❌ {Colors.RED}Network stats check failed: {e}{Colors.END}")
        errors += 1
    
    # Summary
    total = checks_passed + warnings + errors
    print(f"\n{Colors.BOLD}{Colors.CYAN}{'='*70}{Colors.END}")
    print(f"{Colors.BOLD}SUMMARY{Colors.END}: {total} checks | {Colors.GREEN}{checks_passed} passed{Colors.END} | {Colors.YELLOW}{warnings} warnings{Colors.END} | {Colors.RED}{errors} errors{Colors.END}")
    
    if errors == 0 and warnings == 0:
        print(f"{Colors.GREEN}{Colors.BOLD}🎉 ALL SYSTEMS OPERATIONAL{Colors.END}\n")
        return 0
    elif errors == 0:
        print(f"{Colors.YELLOW}{Colors.BOLD}⚠️  SYSTEM OPERATIONAL WITH WARNINGS{Colors.END}\n")
        return 1
    else:
        print(f"{Colors.RED}{Colors.BOLD}❌ CRITICAL ISSUES DETECTED - ACTION REQUIRED{Colors.END}\n")
        return 2


if __name__ == "__main__":
    sys.exit(run_health_check())
