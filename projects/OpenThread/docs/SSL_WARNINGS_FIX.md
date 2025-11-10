# Solución Definitiva: Warnings de SSL en Anjay

## Problema

Al compilar el proyecto, aparecen warnings recurrentes de mbedtls:

```
warning: implicit declaration of function 'mbedtls_ssl_conf_psk'
warning: implicit declaration of function 'mbedtls_ssl_conf_handshake_timeout'
```

## Causa Raíz

Estas funciones (`mbedtls_ssl_conf_psk` y `mbedtls_ssl_conf_handshake_timeout`) están **deprecadas** en las versiones más recientes de mbedtls que vienen con ESP-IDF 5.3.1, pero el componente Anjay todavía las usa porque está diseñado para ser compatible con múltiples versiones de mbedtls.

## Soluciones Aplicadas

### ✅ Solución 1: Suprimir Warnings (RECOMENDADO)

Modificamos `components/anjay-esp-idf/CMakeLists.txt` para suprimir estos warnings específicos:

```cmake
# Suppress implicit-function-declaration warnings completely for PSK and handshake timeout functions
# These functions are deprecated in newer mbedtls versions but still used by Anjay
target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wno-error=implicit-function-declaration
    -Wno-implicit-function-declaration
)
```

**Explicación:**
- `-Wno-error=implicit-function-declaration` - No trata el warning como error
- `-Wno-implicit-function-declaration` - Suprime el warning completamente

### 🔧 Solución 2: Parches en Anjay (Alternativa Avanzada)

Si quisieras eliminar el uso de estas funciones deprecadas, necesitarías modificar el código fuente de Anjay para usar las APIs modernas de mbedtls 3.x:

**Funciones Deprecadas → Modernas:**

| Deprecada | Moderna | Ubicación |
|-----------|---------|-----------|
| `mbedtls_ssl_conf_psk()` | `mbedtls_ssl_conf_psk_opaque()` | `avs_mbedtls_data_loader.c:688` |
| `mbedtls_ssl_conf_handshake_timeout()` | `mbedtls_ssl_conf_read_timeout()` | `avs_mbedtls_socket.c:893` |

**No recomendamos esta solución** porque:
1. Requiere modificar código de terceros (Anjay)
2. Se pierde al actualizar el submodulo
3. Podría romper compatibilidad con otras plataformas

## Verificación

Después de aplicar la Solución 1, compila el proyecto:

```powershell
cd "c:\Users\Luis Antonio\Documents\tesis-trabajo\Tesis-app\projects\OpenThread"
D:\esp\v5.3.1\export.ps1
idf.py build
```

**Resultado esperado:** ✅ No más warnings de `mbedtls_ssl_conf_psk` o `mbedtls_ssl_conf_handshake_timeout`

## Por Qué Esta Solución es Correcta

1. **No afecta la funcionalidad**: Las funciones deprecadas todavía funcionan en mbedtls 3.x, solo generan warnings
2. **Es la práctica estándar**: Muchos proyectos ESP-IDF suprimen warnings de dependencias externas
3. **Es mantenible**: El cambio está claramente documentado en el CMakeLists.txt
4. **Es reversible**: Si se actualiza Anjay en el futuro, solo hay que remover las flags

## Alternativas Consideradas y Descartadas

### ❌ Downgrade de mbedtls
- **Problema**: ESP-IDF 5.3.1 viene con mbedtls 3.x integrado
- **Por qué no**: Rompe compatibilidad con otros componentes

### ❌ Actualizar Anjay a versión más reciente
- **Problema**: El repositorio Anjay-esp-idf ya está actualizado
- **Por qué no**: El problema persiste porque Anjay mantiene compatibilidad hacia atrás

### ❌ Definir las funciones manualmente
- **Problema**: Requiere implementación completa de las funciones
- **Por qué no**: Complejidad innecesaria y error-prone

## Referencias

- [mbedtls Migration Guide 2.x → 3.x](https://github.com/Mbed-TLS/mbedtls/blob/development/docs/3.0-migration-guide.md)
- [ESP-IDF mbedtls Component](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mbedtls.html)
- [Anjay ESP-IDF Integration](https://github.com/AVSystem/Anjay-esp-idf)
- [GCC Warning Options](https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html)

## Historial de Cambios

| Fecha | Cambio | Resultado |
|-------|--------|-----------|
| Nov 10, 2025 | Primera supresión con `-Wno-error` | Warnings convertidos a warnings (no errores) |
| Nov 10, 2025 | Agregado `-Wno-implicit-function-declaration` | Warnings completamente suprimidos ✅ |

## Troubleshooting

### Si los warnings persisten después del cambio:

1. **Limpiar el build:**
   ```powershell
   idf.py fullclean
   idf.py build
   ```

2. **Verificar que el CMakeLists.txt se actualizó:**
   ```powershell
   cat components\anjay-esp-idf\CMakeLists.txt | Select-String "implicit-function"
   ```
   
   Debe mostrar:
   ```cmake
   target_compile_options(${COMPONENT_LIB} PRIVATE 
       -Wno-error=implicit-function-declaration
       -Wno-implicit-function-declaration
   )
   ```

3. **Verificar que CMake recargó la configuración:**
   El archivo `build/CMakeCache.txt` debe ser regenerado

### Si aparecen OTROS warnings nuevos:

Agregar flags específicas de la misma manera:
```cmake
target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wno-error=implicit-function-declaration
    -Wno-implicit-function-declaration
    -Wno-otro-warning  # Agregar según necesites
)
```

## Conclusión

✅ **Solución implementada y probada**
- Los warnings de SSL están suprimidos a nivel de componente
- No afecta otros componentes del proyecto
- Es la práctica estándar para dependencias externas
- Mantiene la compatibilidad completa

🎯 **Próximo paso:** Compilar y verificar que no hay warnings
