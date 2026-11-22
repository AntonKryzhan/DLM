[CmdletBinding()]
param([switch]$CheckBaseline)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'tools\TrainingIO.ps1')
. (Join-Path $PSScriptRoot 'tools\CloudIO.ps1')
try {
    Update-DPICloudRanges $PSScriptRoot
    Write-AutoTargets $PSScriptRoot
    if ($CheckBaseline) {
        & (Join-Path $PSScriptRoot 'NoDPIProbe.exe') '--baseline' '--report=logs/probe-baseline-latest.json' '--label=before-engine' '--timeout-ms=2500'
        exit $LASTEXITCODE
    }
    exit 0
} catch { Write-Host $_.Exception.Message; exit 2 }
