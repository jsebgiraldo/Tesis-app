Write-Host "[portainer] Stopping Portainer..."
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptPath

docker compose down
Write-Host "[portainer] Done."
