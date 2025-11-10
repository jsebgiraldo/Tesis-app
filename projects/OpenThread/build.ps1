# Script para compilar proyecto OpenThread + LwM2M
# Configura automáticamente el entorno ESP-IDF y compila

Write-Host "🚀 Configurando ESP-IDF v5.3.1..." -ForegroundColor Cyan

# Configurar IDF_PATH
$env:IDF_PATH = "D:\esp\v5.3.1"

# Cargar entorno ESP-IDF (usando . para ejecutar en el contexto actual)
Write-Host "📦 Cargando herramientas ESP-IDF..." -ForegroundColor Yellow
. D:\esp\v5.3.1\export.ps1

# Mostrar versión
Write-Host "`n✅ ESP-IDF configurado:" -ForegroundColor Green
Write-Host "   Versión: $env:ESP_IDF_VERSION" -ForegroundColor White
Write-Host "   Ruta: $env:IDF_PATH" -ForegroundColor White

# Compilar proyecto usando la ruta completa
Write-Host "`n🔨 Compilando proyecto..." -ForegroundColor Cyan
& "$env:IDF_PATH\tools\idf.py" build

# Verificar resultado
if ($LASTEXITCODE -eq 0) {
    Write-Host "`n✅ ¡Compilación exitosa!" -ForegroundColor Green
    Write-Host "`nPróximos comandos disponibles:" -ForegroundColor Yellow
    Write-Host "  - idf.py flash        # Flashear a ESP32-C6" -ForegroundColor White
    Write-Host "  - idf.py monitor      # Ver logs serial" -ForegroundColor White
    Write-Host "  - idf.py flash monitor # Flashear y ver logs" -ForegroundColor White
} else {
    Write-Host "`n❌ Error en compilación (código: $LASTEXITCODE)" -ForegroundColor Red
    Write-Host "Revisa los errores arriba" -ForegroundColor Yellow
}
