#!/usr/bin/env pwsh
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
if (-not (Test-Path .env)) { Copy-Item .env.example .env }
$envs = Get-Content .env | Where-Object {$_ -and $_ -notmatch '^#'}
$hasHost = $envs -match '^CLOUD_RPC_HOST='
$hasKey = $envs -match '^CLOUD_ROUTING_KEY='
$hasSecret = $envs -match '^CLOUD_ROUTING_SECRET='
if (-not ($hasHost -and $hasKey -and $hasSecret)) { Write-Error 'Configure .env with CLOUD_* first.' }
& docker compose pull
& docker compose up -d
Pop-Location
