#!/usr/bin/env python3
"""
Reprocesa logs históricos de journalctl para extraer errores HDLC
y poblar la tabla dlms_diagnostics con datos históricos.
"""
import re
import subprocess
import sys
from pathlib import Path
from datetime import datetime

# Añadir el directorio padre al path para importar admin
sys.path.insert(0, str(Path(__file__).parent.parent))

from admin.database import init_db, db, record_dlms_diagnostic

# Patrones de errores HDLC
HDLC_PATTERNS = [
    r'Invalid HDLC frame boundary',
    r'unterminated HDLC address',
    r'Checksum mismatch',
    r'Unexpected receive sequence',
    r'HDLC.*error',
    r'frame boundary',
]

def parse_journalctl_line(line):
    """Extrae timestamp y mensaje de una línea de journalctl"""
    # Formato: Nov 01 16:24:20 hostname service[pid]: message
    match = re.match(r'(\w+ \d+ \d+:\d+:\d+) .* \[.*?\] (.*)', line)
    if match:
        timestamp_str, message = match.groups()
        try:
            # Parsear timestamp (añadir año actual)
            current_year = datetime.now().year
            timestamp = datetime.strptime(f"{current_year} {timestamp_str}", "%Y %b %d %H:%M:%S")
            return timestamp, message
        except:
            return None, message
    return None, line

def extract_hdlc_errors_from_journalctl(service_name='dlms-multi-meter.service', days=7):
    """Extrae errores HDLC de journalctl"""
    
    print(f"📊 Extrayendo errores HDLC de {service_name} (últimos {days} días)...")
    
    cmd = [
        'journalctl',
        '-u', service_name,
        '--since', f'{days} days ago',
        '--no-pager'
    ]
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        lines = result.stdout.split('\n')
        
        errors = []
        for line in lines:
            # Buscar patrones HDLC
            for pattern in HDLC_PATTERNS:
                if re.search(pattern, line, re.IGNORECASE):
                    timestamp, message = parse_journalctl_line(line)
                    errors.append({
                        'timestamp': timestamp or datetime.now(),
                        'message': message,
                        'pattern': pattern
                    })
                    break
        
        print(f"✓ Encontrados {len(errors)} errores HDLC en los logs")
        return errors
    
    except subprocess.CalledProcessError as e:
        print(f"❌ Error ejecutando journalctl: {e}")
        return []
    except Exception as e:
        print(f"❌ Error: {e}")
        return []

def populate_diagnostics(errors, meter_id=1):
    """Pobla la tabla dlms_diagnostics con errores históricos"""
    
    print(f"💾 Guardando {len(errors)} errores en la base de datos...")
    
    init_db('data/admin.db')
    
    saved_count = 0
    for error in errors:
        try:
            with db.get_session() as session:
                record_dlms_diagnostic(
                    session,
                    meter_id=meter_id,
                    category='hdlc',
                    message=error['message'][:500],  # Limitar tamaño
                    severity='error',
                    raw_frame=None
                )
                saved_count += 1
        except Exception as e:
            print(f"⚠️ Error guardando registro: {e}")
    
    print(f"✅ Guardados {saved_count}/{len(errors)} registros")

def main():
    print("=" * 70)
    print("REPROCESAMIENTO DE LOGS HISTÓRICOS - Errores HDLC")
    print("=" * 70)
    
    # Extraer errores de journalctl
    errors = extract_hdlc_errors_from_journalctl(days=7)
    
    if errors:
        # Mostrar resumen
        print("\n📈 Resumen de errores encontrados:")
        
        # Agrupar por patrón
        pattern_counts = {}
        for error in errors:
            pattern = error['pattern']
            pattern_counts[pattern] = pattern_counts.get(pattern, 0) + 1
        
        for pattern, count in sorted(pattern_counts.items(), key=lambda x: x[1], reverse=True):
            print(f"  - {pattern}: {count} ocurrencias")
        
        # Preguntar si poblar
        response = input("\n¿Guardar estos errores en la base de datos? (s/n): ")
        if response.lower() in ['s', 'si', 'y', 'yes']:
            populate_diagnostics(errors)
        else:
            print("❌ Operación cancelada")
    else:
        print("ℹ️ No se encontraron errores HDLC en los logs")

if __name__ == '__main__':
    main()
