param(
    [string]$Port = "COM5"
)

# Helper script to build, flash, and monitor hello_esp32 on Windows PowerShell
# Usage: .\tools\flash.ps1 -Port COM5

Write-Host "Building hello_esp32..." -ForegroundColor Cyan
idf.py build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Flashing on $Port and opening monitor..." -ForegroundColor Cyan
idf.py -p $Port flash monitor
