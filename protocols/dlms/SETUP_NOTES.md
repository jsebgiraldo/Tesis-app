# Resumen de Configuración - Medidor Microstar DLMS

## ✅ Conexión Exitosa

**Fecha:** 10 de octubre de 2025  
**Medidor:** Microstar Electric Company Limited  
**Protocolo:** DLMS/COSEM sobre HDLC/TCP

## 📋 Parámetros de Conexión

| Parámetro | Valor | Notas |
|-----------|-------|-------|
| IP | `192.168.5.177` | Dirección del medidor en la red del laboratorio |
| Puerto | `3333` | Puerto TCP |
| Cliente SAP | `1` | Service Access Point del cliente |
| Server Logical | `0` | **CRÍTICO:** Debe ser 0 (no 1 por defecto) |
| Server Physical | `1` | Dirección física del servidor |
| Password | `22222222` | Contraseña para SAP 1 |

## 🔐 Credenciales Disponibles

Según la etiqueta del medidor:

| SAP | Password |
|-----|----------|
| 1   | 22222222 |
| 32  | 11111111 |
| 17  | 00000000 |

## 📊 Lecturas Obtenidas

### Voltaje Fase A (OBIS: 1-1:32.7.0)
- **Valor:** ~135 V
- **Código de unidad reportado:** 35 (Hz) - se sobreescribe a V
- **Valor crudo:** ~13500 (scaler: -2)

### Corriente Fase A (OBIS: 1-1:31.7.0)
- **Valor:** ~1.3 A
- **Código de unidad:** 33 (A)
- **Valor crudo:** ~1300 (scaler: -3)

## 🚀 Uso Rápido

### Opción 1: Script Helper (Recomendado)
```bash
cd /home/pci/Documents/sebas_giraldo/Tesis-app/protocols/dlms
./read_meter.sh
```

### Opción 2: Comando Completo
```bash
python3 dlms_reader.py \
  --host 192.168.5.177 \
  --port 3333 \
  --client-sap 1 \
  --server-logical 0 \
  --server-physical 1 \
  --password 22222222 \
  --measurement voltage_l1 current_l1
```

## 🔧 Troubleshooting

### Error más común: Timeout
**Solución:** Esperar 10-15 segundos entre intentos de conexión. El medidor puede necesitar tiempo para reiniciar su interfaz de comunicación.

### Error: "Invalid HDLC frame boundary"
**Causa:** El medidor está procesando una conexión anterior.  
**Solución:** Esperar unos segundos antes de reintentar.

### No responde
1. Verificar conectividad: `ping 192.168.5.177`
2. Verificar puerto: `nc -vz 192.168.5.177 3333`
3. Confirmar `--server-logical 0` (el valor más importante)

## 📝 Notas Importantes

1. **`--server-logical 0` es crítico** - Este medidor NO funciona con el valor por defecto de 1
2. El medidor reporta voltaje con código de unidad 35 (Hz) en lugar de 32 (V) - esto es normal y se corrige automáticamente
3. Los valores crudos usan scalers (potencias de 10) para representar decimales
4. Esperar entre conexiones evita problemas de timeout

## 🔍 Proceso de Depuración

Para ver los frames HDLC en detalle:
```bash
./read_meter.sh --verbose
```

Esto mostrará:
- Frames TX (transmitidos al medidor)
- Frames RX (recibidos del medidor)
- Proceso de handshake SNRM/UA
- Asociación AARQ/AARE
- Requests y responses GET

## 📚 Referencias

- Código OBIS para voltaje: `1-1:32.7.0`
- Código OBIS para corriente: `1-1:31.7.0`
- Clase DLMS: 3 (Register)
- Atributo valor: 2
- Atributo scaler/unit: 3

## ✅ Estado

- [x] Conexión TCP establecida
- [x] Handshake HDLC (SNRM/UA) exitoso
- [x] Asociación DLMS (AARQ/AARE) exitosa
- [x] Lectura de voltaje funcional
- [x] Lectura de corriente funcional
- [x] Documentación completa
- [x] Script helper creado
