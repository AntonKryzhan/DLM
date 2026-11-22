[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
. (Join-Path $root 'tools\TrainingIO.ps1')
. (Join-Path $root 'tools\DriverIO.ps1')
. (Join-Path $root 'tools\DriverDiagnostics.ps1')
$logs=Join-Path $root 'logs'; $stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$code=1
try {
    if ($env:OS -ne 'Windows_NT') { throw 'Windows is required' }
    New-Item -ItemType Directory -Path $logs -Force | Out-Null
    Write-Host 'WinDivert startup check: sniff/receive-only handles; no traffic interception or packet changes.'
    Write-Host 'For a complete driver-open test run Check-Driver.cmd as administrator. WinDivert may load its driver normally; Windows security settings are preserved.'
    $report=Join-Path $logs ('driver-check-' + $stamp + '.json')
    try { Test-DPIDriver $root $report; $code=0; Write-Host 'Driver open and filter compilation passed.' -ForegroundColor Green }
    catch { Write-Host $_.Exception.Message -ForegroundColor Red }
    $system=Join-Path $logs ('driver-system-' + $stamp + '.json')
    Write-AtomicText $system (ConvertTo-Json -InputObject (Get-DPIDriverSystem $root) -Depth 8)
    Write-Host ('System report: ' + $system)
    Write-Host 'Send the ZIP from Collect-Diagnostics.cmd. Error 58 alone does not identify a specific driver failure cause.'
} catch { Write-Host $_.Exception.Message -ForegroundColor Red }
exit $code
