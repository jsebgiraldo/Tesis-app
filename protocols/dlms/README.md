# Microstar DLMS reader helper

This folder contains a lightweight Python script that sends the HDLC/DLMS frames
required to query a Microstar meter over TCP/IP and extract instantaneous
register values such as the phase voltage. The implementation follows the frame
examples published in the _Microstar DLMS Protocol Guide_ (see the PDF in this
folder) and the sample capture `TRAMA DLMS CURRENT.txt`.

## Quick Start

### Usando el script helper (recomendado)

Para leer el voltaje y corriente del medidor del laboratorio de forma rápida:

```bash
./read_meter.sh           # Lectura normal
./read_meter.sh --verbose # Lectura con debug detallado
```

**Resultado esperado:**
```
Phase A instantaneous voltage (1-1:32.7.0): 135.19 V  [raw=13519, scaler unit code=35]
Phase A instantaneous current (1-1:31.7.0): 1.329 A  [raw=1329, scaler unit code=33]
```

### Usando el script Python directamente

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

⚠️ **Notas importantes:** 
- Este medidor Microstar requiere `--server-logical 0` para establecer la conexión correctamente.
- Si el medidor no responde, espera 10-15 segundos entre intentos de conexión.

## Files

- `dlms_reader.py` – executable script that opens a TCP connection, performs the
  SNRM/AARQ handshake, sends DLMS GET requests, parses the responses (including
  scaler/unit structures) and prints the resolved value.
- `read_meter.sh` – script helper preconfigurado con los parámetros del medidor del laboratorio (IP: 192.168.5.177, Puerto: 3333).
- `tests/test_dlms_reader.py` – quick unit tests that validate the frame builder
  and basic DLMS data parsing helpers.

## Usage

### Ejemplo básico (configuración de laboratorio)

Para leer voltaje y corriente del medidor Microstar en el laboratorio:

```bash
python3 dlms_reader.py \
  --host 192.168.5.177 \
  --port 3333 \
  --client-sap 1 \
  --server-logical 0 \
  --server-physical 1 \
  --password 22222222 \
  --measurement voltage_l1 current_l1 \
  --verbose
```

**Salida esperada:**
```
Phase A instantaneous voltage (1-1:32.7.0): 135.43 V  [raw=13543, scaler unit code=35]
Phase A instantaneous current (1-1:31.7.0): 1.324 A  [raw=1324, scaler unit code=33]
```

### Credenciales del medidor

Según la etiqueta del medidor Microstar, las credenciales por defecto son:

| Cliente SAP | Clave DLMS |
|-------------|------------|
| SAP 1       | 22222222   |
| SAP 32      | 11111111   |
| SAP 17      | 00000000   |

### Parámetros clave

- `--host` – Dirección IP del medidor (ej: `192.168.5.177`)
- `--port` – Puerto TCP del medidor (ej: `3333` o `4059` según configuración)
- `--client-sap` – Cliente SAP (usar `1` para SAP 1 con password 22222222)
- `--server-logical` – Dirección lógica del servidor (**importante: usar `0` para este medidor**)
- `--server-physical` – Dirección física del servidor (por defecto `1`)
- `--password` – Contraseña de autenticación según el SAP elegido
- `--measurement` – Mediciones a leer: `voltage_l1` (voltaje), `current_l1` (corriente), o ambas
- `--verbose` – Mostrar frames HDLC en hexadecimal para debug

### Opciones adicionales

- `--measurement` determina el código OBIS a leer. Las opciones disponibles son:
  - `voltage_l1` – Voltaje instantáneo Fase A (OBIS: 1-1:32.7.0)
  - `current_l1` – Corriente instantánea Fase A (OBIS: 1-1:31.7.0)
  
  Extiende el diccionario `MEASUREMENTS` en el código para agregar más atributos.

- `--server-logical` / `--server-physical` controlan el direccionamiento HDLC. 
  Para este medidor específico, usar `--server-logical 0` es crítico para establecer la conexión.
  
- `--max-info-length` define la longitud del campo de información HDLC negociado. 
  Si se omite, el script envía un frame SNRM mínimo.

Ejecuta `python3 dlms_reader.py --help` para la lista completa de argumentos.

## Output

A successful execution prints the resolved value, the interpreted unit (when
known) and the raw integer returned by the register, for example:

```
Phase A instantaneous voltage (1-1:32.7.0): 228.54 V  [raw=22854, scaler unit code=32]
```

Add the `--verbose` switch to inspect each HDLC frame (hex encoded) as it is
sent or received.

## Tests

The helper functions can be validated locally:

```bash
python3 -m unittest discover -s tests -v
```

The tests only rely on the captured frames and do not require a live meter.

## Troubleshooting

### El medidor no responde

1. **Verificar conectividad de red:**
   ```bash
   ping 192.168.5.177
   ```

2. **Verificar que el puerto está abierto:**
   ```bash
   nc -vz 192.168.5.177 3333
   ```

3. **Verificar parámetros de conexión:**
   - Confirmar que estás usando `--server-logical 0` (crítico para este modelo)
   - Verificar la contraseña según el SAP seleccionado
   - Probar con `--verbose` para ver los frames enviados/recibidos

### Error "timed out" al conectar

- El medidor puede estar usando una dirección física diferente. Intenta con los últimos 4 dígitos del número de serie del medidor:
  ```bash
  --server-physical 0x1234  # Reemplaza con tu número de serie
  ```

### Advertencia "meter reported unit ... overriding to ..."

Esta advertencia es normal y ocurre cuando el medidor reporta un código de unidad diferente al esperado (ej: Hz en lugar de V). El script aplica la unidad correcta basándose en el tipo de medición solicitada.

## Notes

- El script implementa autenticación de **bajo nivel (LLS)** con contraseña. Para acceso público sin autenticación, usa `--client-sap 16` (cliente público por defecto).
- DLMS es altamente configurable; algunos medidores pueden esperar diferentes tamaños de ventana o parámetros HDLC. Actualiza `_build_snrm_info` o los límites negociados según tu instalación.
- Solo el subconjunto de tipos de datos DLMS necesarios para recuperar registros/escaladores está implementado. Extiende `_parse_data` si planeas parsear objetos más complejos.
