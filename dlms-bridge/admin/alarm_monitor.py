#!/usr/bin/env python3
"""
Alarm Monitoring and Tracking Script
Tracks alarms over time to identify patterns and recurring issues
"""

import requests
import time
import sys
from datetime import datetime, timedelta
from collections import defaultdict
from typing import Dict, List
import json


class AlarmMonitor:
    """Monitor and track alarms from the admin API"""
    
    def __init__(self, api_url: str = "http://localhost:8000"):
        self.api_url = api_url
        self.alarm_history: List[Dict] = []
        self.alarm_counts: Dict[str, int] = defaultdict(int)
        self.alarm_by_category: Dict[str, List[Dict]] = defaultdict(list)
        self.alarm_by_meter: Dict[int, List[Dict]] = defaultdict(list)
    
    def fetch_alarms(self) -> List[Dict]:
        """Fetch current alarms from API"""
        try:
            response = requests.get(f"{self.api_url}/alarms", params={"limit": 1000})
            response.raise_for_status()
            return response.json()
        except Exception as e:
            print(f"❌ Error fetching alarms: {e}")
            return []
    
    def analyze_alarms(self, alarms: List[Dict]):
        """Analyze alarm patterns"""
        self.alarm_counts.clear()
        self.alarm_by_category.clear()
        self.alarm_by_meter.clear()
        
        for alarm in alarms:
            # Count by category
            category = alarm.get('category', 'unknown')
            self.alarm_counts[category] += 1
            self.alarm_by_category[category].append(alarm)
            
            # Group by meter
            meter_id = alarm.get('meter_id', 0)
            self.alarm_by_meter[meter_id].append(alarm)
    
    def print_summary(self):
        """Print alarm summary"""
        alarms = self.fetch_alarms()
        
        if not alarms:
            print("✅ No hay alarmas activas")
            return
        
        self.analyze_alarms(alarms)
        
        print("\n" + "="*70)
        print("📊 RESUMEN DE ALARMAS")
        print("="*70)
        print(f"\n🔢 Total de alarmas: {len(alarms)}")
        
        # Summary by severity
        severity_counts = defaultdict(int)
        for alarm in alarms:
            severity_counts[alarm.get('severity', 'unknown')] += 1
        
        print("\n📈 Por severidad:")
        severity_emoji = {
            'critical': '🔴',
            'error': '🟠',
            'warning': '🟡',
            'info': '🔵'
        }
        for severity, count in sorted(severity_counts.items(), key=lambda x: x[1], reverse=True):
            emoji = severity_emoji.get(severity, '⚪')
            print(f"   {emoji} {severity.capitalize()}: {count}")
        
        # Summary by category
        print("\n📂 Por categoría:")
        for category, count in sorted(self.alarm_counts.items(), key=lambda x: x[1], reverse=True):
            print(f"   • {category}: {count}")
        
        # Summary by meter
        print("\n🔌 Por medidor:")
        for meter_id, meter_alarms in sorted(self.alarm_by_meter.items()):
            print(f"   • Medidor {meter_id}: {len(meter_alarms)} alarmas")
        
        # Recent alarms (last 5)
        print("\n🕐 Alarmas más recientes:")
        for i, alarm in enumerate(alarms[:5], 1):
            timestamp = datetime.fromisoformat(alarm['timestamp'].replace('Z', '+00:00'))
            time_ago = self._format_time_ago(timestamp)
            severity_emoji_val = severity_emoji.get(alarm.get('severity', 'info'), '⚪')
            print(f"   {i}. {severity_emoji_val} [{alarm['category']}] {alarm['message']}")
            print(f"      └─ {time_ago} (Medidor {alarm['meter_id']})")
        
        print("\n" + "="*70)
    
    def _format_time_ago(self, timestamp: datetime) -> str:
        """Format time difference as human-readable string"""
        now = datetime.now(timestamp.tzinfo) if timestamp.tzinfo else datetime.now()
        diff = now - timestamp
        
        if diff.total_seconds() < 60:
            return f"hace {int(diff.total_seconds())}s"
        elif diff.total_seconds() < 3600:
            return f"hace {int(diff.total_seconds() / 60)}m"
        elif diff.total_seconds() < 86400:
            return f"hace {int(diff.total_seconds() / 3600)}h"
        else:
            return f"hace {int(diff.total_seconds() / 86400)}d"
    
    def watch(self, interval: int = 10):
        """Continuously monitor alarms"""
        print(f"👀 Monitoreando alarmas cada {interval} segundos...")
        print("   Presiona Ctrl+C para detener\n")
        
        last_count = 0
        
        try:
            while True:
                alarms = self.fetch_alarms()
                current_count = len(alarms)
                
                # Clear screen
                print("\033[H\033[J", end="")
                
                # Print header
                timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                print(f"📊 Monitor de Alarmas - {timestamp}")
                print("="*70)
                
                if current_count != last_count:
                    diff = current_count - last_count
                    if diff > 0:
                        print(f"🔴 NUEVA ALARMA: +{diff} alarma(s)")
                    else:
                        print(f"✅ Alarma resuelta: {-diff} alarma(s) eliminada(s)")
                    last_count = current_count
                
                self.print_summary()
                
                time.sleep(interval)
                
        except KeyboardInterrupt:
            print("\n\n✋ Monitoreo detenido")
    
    def show_patterns(self, hours: int = 24):
        """Show alarm patterns over time"""
        alarms = self.fetch_alarms()
        
        if not alarms:
            print("✅ No hay alarmas para analizar")
            return
        
        cutoff = datetime.now() - timedelta(hours=hours)
        
        print(f"\n🔍 ANÁLISIS DE PATRONES (últimas {hours} horas)")
        print("="*70)
        
        # Find repeated alarms
        message_counts = defaultdict(int)
        for alarm in alarms:
            timestamp = datetime.fromisoformat(alarm['timestamp'].replace('Z', '+00:00'))
            if timestamp.replace(tzinfo=None) > cutoff:
                # Normalize message (remove numbers for pattern matching)
                msg = alarm['message']
                message_counts[msg] += 1
        
        # Show patterns
        patterns = [(msg, count) for msg, count in message_counts.items() if count > 1]
        
        if patterns:
            print(f"\n⚠️  Problemas recurrentes detectados:")
            for msg, count in sorted(patterns, key=lambda x: x[1], reverse=True):
                print(f"   • {msg}")
                print(f"     └─ Ocurrió {count} veces")
        else:
            print("\n✅ No se detectaron patrones de alarmas recurrentes")
        
        print("\n" + "="*70)
    
    def cleanup_old(self, hours: int = 24):
        """Clean up old alarms"""
        try:
            response = requests.delete(
                f"{self.api_url}/alarms",
                params={"older_than_hours": hours}
            )
            response.raise_for_status()
            result = response.json()
            count = result.get('count', 0)
            print(f"✅ Se eliminaron {count} alarmas de más de {hours} horas")
        except Exception as e:
            print(f"❌ Error limpiando alarmas: {e}")


def main():
    """Main entry point"""
    import argparse
    
    parser = argparse.ArgumentParser(description="Monitor de alarmas DLMS")
    parser.add_argument('--url', default='http://localhost:8000', help='URL de la API')
    parser.add_argument('command', choices=['summary', 'watch', 'patterns', 'cleanup'],
                       help='Comando a ejecutar')
    parser.add_argument('--interval', type=int, default=10,
                       help='Intervalo de actualización para watch (segundos)')
    parser.add_argument('--hours', type=int, default=24,
                       help='Horas para patterns o cleanup')
    
    args = parser.parse_args()
    
    monitor = AlarmMonitor(args.url)
    
    if args.command == 'summary':
        monitor.print_summary()
    elif args.command == 'watch':
        monitor.watch(args.interval)
    elif args.command == 'patterns':
        monitor.show_patterns(args.hours)
    elif args.command == 'cleanup':
        monitor.cleanup_old(args.hours)


if __name__ == "__main__":
    main()
