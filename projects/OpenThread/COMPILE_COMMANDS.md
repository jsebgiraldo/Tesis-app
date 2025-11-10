# Comando para verificar warnings de SSL

Para compilar y verificar que los warnings de mbedtls desaparecieron, ejecuta:

```powershell
idf.py build 2>&1 | Select-String -Pattern "mbedtls_ssl|warning: implicit"
```

Si no aparece ninguna línea, significa que los warnings fueron suprimidos exitosamente ✅

## Compilación Normal

Para compilar sin filtros:

```powershell
idf.py build
```

## Compilación Limpia (si es necesario)

Si los cambios en CMakeLists.txt no se reflejan:

```powershell
idf.py fullclean
idf.py build
```

## Verificación de la Solución

Para verificar que el CMakeLists.txt tiene las flags correctas:

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
