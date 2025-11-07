# Solución a Problemas de Compilación SSL/mbedtls

## Problema Encontrado

Durante la compilación inicial con ESP-IDF v5.5.1, encontramos errores relacionados con funciones de mbedtls que no estaban disponibles:

```
error: 'mbedtls_ssl_set_client_transport_id' was not declared
error: 'mbedtls_ssl_conf_handshake_timeout' was not declared  
error: 'mbedtls_ssl_conf_dtls_cookies' was not declared
error: 'mbedtls_ssl_set_hs_ecjpake_password' was not declared
```

## Causa Raíz

ESP-IDF v5.5.1 tiene cambios en la API de mbedtls que aún no son completamente compatibles con la versión de OpenThread incluida en esa release.

## Solución Aplicada ✅

**Cambiar a ESP-IDF v5.4.1** - Esta es una versión LTS (Long Term Support) más estable con mejor compatibilidad con OpenThread.

### Pasos realizados:

1. Limpiar entorno Python anterior:
```powershell
$env:IDF_PYTHON_ENV_PATH = $null
```

2. Activar ESP-IDF v5.4.1:
```powershell
. D:\esp\v5.4.1\esp-idf\export.ps1
```

3. Limpiar build anterior:
```bash
idf.py fullclean
```

4. Reconfigurar target:
```bash
idf.py set-target esp32c6
```

5. Compilar:
```bash
idf.py build
```

## Versiones Recomendadas

Para proyectos con OpenThread en ESP32C6:

| ESP-IDF Version | OpenThread | Estado | Recomendación |
|----------------|------------|---------|---------------|
| **v5.4.1** | ✅ Compatible | Estable | **RECOMENDADO** |
| v5.3.x | ✅ Compatible | Estable | OK |
| v5.2.x | ✅ Compatible | Estable | OK |
| v5.1.x | ⚠️ Limitado | EOL | No recomendado |
| v5.5.x | ❌ Problemas | Beta | Evitar por ahora |

## Alternativas si Necesitas v5.5.1

Si absolutamente necesitas usar ESP-IDF v5.5.1, hay dos opciones:

### Opción 1: Esperar actualización oficial
Espera a que Espressif actualice el componente OpenThread para v5.5.x

### Opción 2: Usar componente OpenThread externo (Avanzado)
```yaml
# idf_component.yml
dependencies:
  openthread:
    git: https://github.com/openthread/openthread.git
    version: main
```

Pero esto requiere configuración manual adicional de mbedtls.

## Scripts de Ayuda Creados

Para facilitar el trabajo futuro, se crearon:

### `idf-env.ps1` (PowerShell)
```powershell
.\idf-env.ps1
```
Activa automáticamente ESP-IDF v5.4.1

### `idf-cmd.bat` (Batch)
```cmd
idf-cmd.bat build
idf-cmd.bat -p COM3 flash monitor
```
Ejecuta comandos idf.py con el entorno correcto

## Verificación de Éxito

La compilación exitosa muestra:
```
Executing "ninja all"...
[1337/1337] Generating binary image...
Project build complete.
```

## Configuración en sdkconfig.defaults

Las configuraciones en `sdkconfig.defaults` ya están optimizadas para OpenThread:

```ini
# OpenThread habilitado
CONFIG_OPENTHREAD_ENABLED=y
CONFIG_OPENTHREAD_CLI=y

# IEEE 802.15.4 radio
CONFIG_IEEE802154_ENABLED=y

# mbedtls configurado automáticamente por ESP-IDF
```

No es necesario configurar mbedtls manualmente ya que ESP-IDF lo maneja automáticamente para OpenThread.

## Problemas Futuros Potenciales

### Si cambias de versión de ESP-IDF:
```bash
# Siempre hacer fullclean
idf.py fullclean
idf.py set-target esp32c6
idf.py build
```

### Si actualizas componentes:
```bash
# Actualizar dependencias
idf.py update-dependencies
idf.py build
```

## Recursos Adicionales

- [ESP-IDF OpenThread Guide](https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32c6/api-guides/openthread.html)
- [OpenThread mbedtls Integration](https://github.com/openthread/openthread/tree/main/third_party/mbedtls)
- [ESP32C6 Release Notes](https://github.com/espressif/esp-idf/releases)

---

**Estado Final:** ✅ Compilación exitosa con ESP-IDF v5.4.1
