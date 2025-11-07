# Script PowerShell para activar ESP-IDF v5.4.1
# Uso: .\idf-env.ps1
# Luego puedes usar: idf.py build, idf.py flash, etc.

$env:IDF_PYTHON_ENV_PATH = $null
. D:\esp\v5.4.1\esp-idf\export.ps1

Write-Host "`n✅ Entorno ESP-IDF v5.4.1 activado" -ForegroundColor Green
Write-Host "Ahora puedes usar comandos como:" -ForegroundColor Cyan
Write-Host "  idf.py build" -ForegroundColor Yellow
Write-Host "  idf.py -p COM3 flash monitor" -ForegroundColor Yellow
Write-Host "  idf.py menuconfig" -ForegroundColor Yellow
