#!/usr/bin/env pwsh
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
& docker compose down
Pop-Location
