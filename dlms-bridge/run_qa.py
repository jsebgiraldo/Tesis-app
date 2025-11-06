#!/usr/bin/env python3
"""
Script de QA automático para dlms_poller_production.py

Ejecuta pruebas y genera reporte detallado.
"""

import subprocess
import time
import re
import sys
from datetime import datetime, timedelta

def run_qa_test(duration_seconds=60, interval=2.0):
    """Ejecuta prueba de QA por duración especificada."""
    
    print("╔══════════════════════════════════════════════════════════════╗")
    print("║           DLMS POLLER - AUTOMATED QA SUITE                  ║")
    print("╚══════════════════════════════════════════════════════════════╝")
    print()
    print(f"📅 Inicio: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"⏱️  Duración: {duration_seconds}s ({duration_seconds//60}min {duration_seconds%60}s)")
    print(f"🔄 Intervalo: {interval}s")
    print(f"📊 Ciclos esperados: ~{duration_seconds // interval}")
    print()
    print("Iniciando prueba...")
    print("=" * 66)
    
    # Archivo de log
    log_file = f"qa_auto_{int(time.time())}.log"
    
    # Ejecutar con timeout
    cmd = [
        "timeout", str(duration_seconds),
        "python3", "dlms_poller_production.py",
        "--host", "192.168.1.127",
        "--interval", str(interval)
    ]
    
    start_time = time.time()
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=duration_seconds + 10
        )
        output = result.stdout + result.stderr
    except subprocess.TimeoutExpired as e:
        output = e.stdout.decode() if e.stdout else ""
        output += e.stderr.decode() if e.stderr else ""
    except Exception as e:
        print(f"❌ Error ejecutando prueba: {e}")
        return None
    
    elapsed = time.time() - start_time
    
    # Guardar log
    with open(log_file, 'w') as f:
        f.write(output)
    
    print(f"\n✓ Prueba completada en {elapsed:.1f}s")
    print(f"📁 Log guardado: {log_file}")
    print()
    
    # Analizar resultados
    return analyze_log(output, log_file, elapsed)

def analyze_log(log_content, log_file, elapsed_time):
    """Analiza el log y extrae métricas."""
    
    lines = log_content.split('\n')
    
    # Extraer métricas
    ciclos_exitosos = len([l for l in lines if '| V:' in l and '| C:' in l])
    errores_checksum = len([l for l in lines if 'Checksum mismatch' in l])
    errores_boundary = len([l for l in lines if 'Invalid HDLC frame boundary' in l])
    errores_unterminated = len([l for l in lines if 'unterminated HDLC address' in l])
    warnings = len([l for l in lines if '[WARNING]' in l])
    errors = len([l for l in lines if '[ERROR]' in l])
    reconexiones = len([l for l in lines if 'reconectando...' in l])
    drenajes = len([l for l in lines if '🧹 Drenaje preventivo' in l])
    
    # Extraer tiempos
    tiempos = []
    for line in lines:
        match = re.search(r'\((\d+\.\d+)s\)', line)
        if match:
            tiempos.append(float(match.group(1)))
    
    # Extraer voltajes y corrientes
    voltages = []
    currents = []
    for line in lines:
        match = re.search(r'V:\s+([\d.]+)\s+V.*C:\s+([\d.]+)\s+A', line)
        if match:
            voltages.append(float(match.group(1)))
            currents.append(float(match.group(2)))
    
    # Generar reporte
    print("=" * 66)
    print("📊 REPORTE DE QA")
    print("=" * 66)
    print()
    
    print("⏱️  DURACIÓN Y CICLOS:")
    print(f"   Tiempo transcurrido:      {elapsed_time:.1f}s")
    print(f"   Ciclos exitosos:          {ciclos_exitosos}")
    if ciclos_exitosos > 0:
        print(f"   Tiempo promedio/ciclo:    {elapsed_time/ciclos_exitosos:.2f}s")
    print()
    
    print("✅ TASA DE ÉXITO:")
    if ciclos_exitosos > 0:
        tasa_exito = (ciclos_exitosos / (ciclos_exitosos + errors)) * 100 if (ciclos_exitosos + errors) > 0 else 100
        print(f"   {'✓' if tasa_exito >= 70 else '✗'} Tasa de éxito:              {tasa_exito:.1f}%")
    print()
    
    print("🐛 ERRORES DETECTADOS:")
    print(f"   Checksum mismatch:        {errores_checksum}")
    print(f"   Frame boundary:           {errores_boundary}")
    print(f"   Unterminated address:     {errores_unterminated}")
    print(f"   Warnings totales:         {warnings}")
    print(f"   Errors totales:           {errors}")
    print(f"   Reconexiones:             {reconexiones}")
    print()
    
    print("🧹 MANTENIMIENTO:")
    print(f"   Drenajes preventivos:     {drenajes}")
    if drenajes > 0 and ciclos_exitosos > 0:
        print(f"   Frecuencia:               Cada ~{ciclos_exitosos // drenajes} ciclos")
    print()
    
    if tiempos:
        print("⚡ RENDIMIENTO:")
        print(f"   Tiempo mínimo:            {min(tiempos):.3f}s")
        print(f"   Tiempo máximo:            {max(tiempos):.3f}s")
        print(f"   Tiempo promedio:          {sum(tiempos)/len(tiempos):.3f}s")
        print()
    
    if voltages:
        print("📈 DATOS LEÍDOS:")
        print(f"   Voltage:    {min(voltages):.2f}V - {max(voltages):.2f}V (Δ {max(voltages)-min(voltages):.2f}V)")
        print(f"   Current:    {min(currents):.2f}A - {max(currents):.2f}A (Δ {max(currents)-min(currents):.2f}A)")
        print()
    
    # Validaciones
    print("✅ VALIDACIONES QA:")
    checks = []
    
    if ciclos_exitosos > 0:
        tasa = (ciclos_exitosos / (ciclos_exitosos + errors)) * 100 if (ciclos_exitosos + errors) > 0 else 100
        checks.append(("Tasa de éxito > 70%", tasa >= 70))
    
    if ciclos_exitosos > 0:
        checks.append(("Reconexiones < 10%", reconexiones < ciclos_exitosos * 0.1))
    
    checks.append(("Sin errores críticos", errors == 0))
    checks.append(("Drenaje preventivo activo", drenajes > 0 or ciclos_exitosos < 10))
    
    for check_name, passed in checks:
        symbol = "✓" if passed else "✗"
        print(f"   [{symbol}] {check_name}")
    print()
    
    # Veredicto final
    print("=" * 66)
    passed_count = sum(1 for _, p in checks if p)
    total_checks = len(checks)
    
    if passed_count == total_checks:
        print("🎉 VEREDICTO: APROBADO")
        print("   El sistema cumple con todos los estándares de calidad.")
        verdict = "PASS"
    elif passed_count >= total_checks * 0.7:
        print("⚠️  VEREDICTO: APROBADO CON RESERVAS")
        print("   El sistema funciona pero requiere ajustes menores.")
        verdict = "PASS_WITH_WARNINGS"
    else:
        print("❌ VEREDICTO: REPROBADO")
        print("   El sistema no cumple con estándares mínimos.")
        verdict = "FAIL"
    
    print("=" * 66)
    print()
    print(f"📁 Log completo disponible en: {log_file}")
    print()
    
    return verdict

if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description="QA automatizado para DLMS Poller")
    parser.add_argument("--duration", type=int, default=120, help="Duración en segundos (default: 120)")
    parser.add_argument("--interval", type=float, default=2.0, help="Intervalo entre lecturas (default: 2.0)")
    
    args = parser.parse_args()
    
    try:
        verdict = run_qa_test(args.duration, args.interval)
        
        if verdict == "PASS":
            sys.exit(0)
        elif verdict == "PASS_WITH_WARNINGS":
            sys.exit(1)
        else:
            sys.exit(2)
    
    except KeyboardInterrupt:
        print("\n\n⚠️  Prueba interrumpida por usuario")
        sys.exit(130)
