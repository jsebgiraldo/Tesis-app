@echo off
REM Script para activar ESP-IDF v5.4.1 y ejecutar comandos
REM Uso: idf-cmd.bat [comando]
REM Ejemplo: idf-cmd.bat build
REM          idf-cmd.bat -p COM3 flash monitor

SET IDF_PYTHON_ENV_PATH=
CALL D:\esp\v5.4.1\esp-idf\export.bat

IF "%1"=="" (
    echo Entorno ESP-IDF v5.4.1 activado
    echo Usa: idf.py [comando]
) ELSE (
    idf.py %*
)
