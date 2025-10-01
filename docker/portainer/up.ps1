Write-Host "[portainer] Starting Portainer..."
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptPath

docker compose up -d
Write-Host "[portainer] Ready. UI: http://localhost:9000 (Primera vez: crea usuario admin)"
