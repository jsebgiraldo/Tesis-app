#!/usr/bin/env python3
"""
Script para medir métricas de performance en realtime del sistema DLMS-ThingsBoard
Mide: latencia end-to-end, throughput, tasa de error, disponibilidad
"""
import time
import requests
import json
import sys
from datetime import datetime
from statistics import mean, stdev

TB_URL = "http://localhost:8080"
TB_USERNAME = "tenant@thingsboard.org"
TB_PASSWORD = "tenant"
DEVICE_NAME = "DLMS-Meter-01"

def measure_performance(duration_seconds=120):
    """Medir performance durante N segundos"""
    
    print("=" * 70)
    print("📊 MEDICIÓN DE PERFORMANCE REALTIME")
    print("=" * 70)
    print(f"Duración: {duration_seconds} segundos")
    print(f"Objetivo: <3s latencia, >0.4 Hz throughput, <5% errores\n")
    
    # Login ThingsBoard
    try:
        login_payload = {"username": TB_USERNAME, "password": TB_PASSWORD}
        response = requests.post(f"{TB_URL}/api/auth/login", json=login_payload, timeout=10)
        response.raise_for_status()
        token = response.json().get("token")
        
        if not token:
            print("❌ No se pudo obtener token de autenticación")
            return 1
            
        headers = {"X-Authorization": f"Bearer {token}"}
        print("✅ Autenticado en ThingsBoard")
    except Exception as e:
        print(f"❌ Error autenticando: {e}")
        return 1
    
    # Obtener device ID
    try:
        device_response = requests.get(
            f"{TB_URL}/api/tenant/devices?deviceName={DEVICE_NAME}",
            headers=headers,
            timeout=10
        )
        device_response.raise_for_status()
        device_id = device_response.json().get("id", {}).get("id")
        
        if not device_id:
            print(f"❌ Dispositivo {DEVICE_NAME} no encontrado")
            return 1
            
        print(f"✅ Dispositivo encontrado: {device_id}\n")
    except Exception as e:
        print(f"❌ Error obteniendo dispositivo: {e}")
        return 1
    
    # Métricas
    samples = []
    latencies = []
    errors = 0
    last_ts = None
    start_time = time.time()
    check_interval = 2  # Verificar cada 2 segundos
    
    print(f"🚀 Iniciando medición por {duration_seconds}s...")
    print("─" * 70)
    
    try:
        while time.time() - start_time < duration_seconds:
            try:
                # Obtener última telemetría
                telemetry_response = requests.get(
                    f"{TB_URL}/api/plugins/telemetry/DEVICE/{device_id}/values/timeseries?keys=voltage_l1",
                    headers=headers,
                    timeout=5
                )
                telemetry_response.raise_for_status()
                
                data = telemetry_response.json()
                
                if "voltage_l1" in data and data["voltage_l1"]:
                    ts = data["voltage_l1"][0]["ts"]
                    value = data["voltage_l1"][0]["value"]
                    
                    if ts != last_ts:
                        # Nueva lectura
                        age = (time.time() * 1000 - ts) / 1000
                        samples.append({
                            'timestamp': ts,
                            'age': age,
                            'value': value
                        })
                        latencies.append(age)
                        
                        # Log cada 10 muestras
                        if len(samples) % 10 == 0:
                            avg_lat = mean(latencies[-10:]) if len(latencies) >= 10 else mean(latencies)
                            throughput = len(samples) / (time.time() - start_time)
                            print(f"⏱️  Muestras: {len(samples):3d} | Latencia: {avg_lat:5.2f}s | "
                                  f"Throughput: {throughput:.3f} Hz | Errores: {errors}")
                        
                        last_ts = ts
                
                time.sleep(check_interval)
                
            except requests.exceptions.RequestException as e:
                errors += 1
                if errors % 5 == 0:
                    print(f"⚠️  Error #{errors}: {e}")
                time.sleep(check_interval)
            except Exception as e:
                errors += 1
                if errors % 5 == 0:
                    print(f"❌ Error #{errors}: {e}")
                time.sleep(check_interval)
    
    except KeyboardInterrupt:
        print("\n⚠️  Medición interrumpida por usuario")
    
    # Estadísticas finales
    print("\n" + "=" * 70)
    print("📈 RESULTADOS FINALES")
    print("=" * 70)
    
    if samples:
        total_time = time.time() - start_time
        avg_latency = mean(latencies)
        min_latency = min(latencies)
        max_latency = max(latencies)
        std_latency = stdev(latencies) if len(latencies) > 1 else 0
        throughput = len(samples) / total_time
        total_checks = len(samples) + errors
        error_rate = (errors / total_checks * 100) if total_checks > 0 else 0
        availability = ((total_checks - errors) / total_checks * 100) if total_checks > 0 else 0
        
        print(f"\n📊 Telemetría:")
        print(f"   Muestras recibidas:     {len(samples)}")
        print(f"   Tiempo total:           {total_time:.1f}s")
        print(f"   Throughput:             {throughput:.3f} Hz (objetivo: >0.4 Hz)")
        
        print(f"\n⏱️  Latencia:")
        print(f"   Promedio:               {avg_latency:.2f}s (objetivo: <3s)")
        print(f"   Mínima:                 {min_latency:.2f}s")
        print(f"   Máxima:                 {max_latency:.2f}s")
        print(f"   Desviación estándar:    {std_latency:.2f}s")
        
        print(f"\n❌ Errores:")
        print(f"   Total de errores:       {errors}")
        print(f"   Tasa de error:          {error_rate:.2f}% (objetivo: <5%)")
        print(f"   Disponibilidad:         {availability:.2f}% (objetivo: >95%)")
        
        # Evaluación
        print(f"\n🎯 Evaluación:")
        
        success_criteria = []
        
        if avg_latency < 3:
            print(f"   ✅ Latencia: EXCELENTE ({avg_latency:.2f}s < 3s)")
            success_criteria.append(True)
        elif avg_latency < 5:
            print(f"   ⚠️  Latencia: ACEPTABLE ({avg_latency:.2f}s < 5s)")
            success_criteria.append(True)
        else:
            print(f"   ❌ Latencia: MEJORAR ({avg_latency:.2f}s >= 5s)")
            success_criteria.append(False)
        
        if throughput >= 0.4:
            print(f"   ✅ Throughput: EXCELENTE ({throughput:.3f} Hz >= 0.4 Hz)")
            success_criteria.append(True)
        elif throughput >= 0.2:
            print(f"   ⚠️  Throughput: ACEPTABLE ({throughput:.3f} Hz >= 0.2 Hz)")
            success_criteria.append(True)
        else:
            print(f"   ❌ Throughput: MEJORAR ({throughput:.3f} Hz < 0.2 Hz)")
            success_criteria.append(False)
        
        if error_rate < 5:
            print(f"   ✅ Tasa de error: EXCELENTE ({error_rate:.2f}% < 5%)")
            success_criteria.append(True)
        elif error_rate < 10:
            print(f"   ⚠️  Tasa de error: ACEPTABLE ({error_rate:.2f}% < 10%)")
            success_criteria.append(True)
        else:
            print(f"   ❌ Tasa de error: MEJORAR ({error_rate:.2f}% >= 10%)")
            success_criteria.append(False)
        
        if availability >= 95:
            print(f"   ✅ Disponibilidad: EXCELENTE ({availability:.2f}% >= 95%)")
            success_criteria.append(True)
        elif availability >= 90:
            print(f"   ⚠️  Disponibilidad: ACEPTABLE ({availability:.2f}% >= 90%)")
            success_criteria.append(True)
        else:
            print(f"   ❌ Disponibilidad: MEJORAR ({availability:.2f}% < 90%)")
            success_criteria.append(False)
        
        # Resultado final
        if all(success_criteria):
            print(f"\n🎉 SISTEMA EN MODO REALTIME - Todos los objetivos alcanzados")
            print("=" * 70)
            return 0
        elif sum(success_criteria) >= len(success_criteria) * 0.75:
            print(f"\n⚠️  SISTEMA FUNCIONAL - Objetivos mayormente alcanzados")
            print("=" * 70)
            return 0
        else:
            print(f"\n❌ SISTEMA REQUIERE OPTIMIZACIÓN - Varios objetivos no alcanzados")
            print("=" * 70)
            return 1
    else:
        print("❌ No se recibieron muestras durante la medición")
        print(f"   Errores totales: {errors}")
        print("=" * 70)
        return 1


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description='Medir performance del sistema DLMS-ThingsBoard')
    parser.add_argument('--duration', type=int, default=120, help='Duración de la medición en segundos (default: 120)')
    args = parser.parse_args()
    
    sys.exit(measure_performance(args.duration))
