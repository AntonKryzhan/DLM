[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'tools\TrainingIO.ps1')
. (Join-Path $PSScriptRoot 'tools\CloudIO.ps1')
try {
    Update-DPICloudRanges $PSScriptRoot -Force
    Get-Content -LiteralPath (Join-Path $PSScriptRoot 'cache\cdn-refresh.json')
    Write-Host 'Close and reopen the engine to load new ranges. Do not update ranges during training.'
    exit 0
} catch { Write-Host $_.Exception.Message -ForegroundColor Red; exit 1 }
