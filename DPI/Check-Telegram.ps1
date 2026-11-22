[CmdletBinding()]
param([switch]$Quick)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
. (Join-Path $root 'tools\TrainingIO.ps1')
. (Join-Path $root 'tools\TelegramProbe.ps1')
$logs=Join-Path $root 'logs'
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$report=Join-Path $logs ('telegram-check-' + $stamp + '.json')
$rows=@(); $failure=''; $code=1
try {
    if ($env:OS -ne 'Windows_NT') { throw 'Windows is required' }
    New-Item -ItemType Directory -Path $logs -Force | Out-Null
    Write-Host 'Checking Telegram Web pages, MTProto TCP and WSS. Existing engine state is preserved.'
    Write-Host 'No account login, tdata, cookies or passwords are accessed. Keep the engine running to test it.'
    $http=Join-Path $logs ('probe-telegram-' + $stamp + '.json')
    & (Join-Path $root 'NoDPIProbe.exe') '--target=3' ('--report=' + $http) '--timeout-ms=3000'
    if ($LASTEXITCODE -notin @(0,1)) { throw 'HTTPS probe failed to run; rebuild version 2.3' }
    $rows=@(Measure-Telegram -Root $root -Extended:(-not $Quick))
    foreach ($row in $rows) { Write-Host ('{0,-36} pass={1,-5} stage={2,-20} {3} ms' -f $row.target,$row.passed,$row.stage,$row.elapsed_ms) }
    $code=0
    if (@($rows | Where-Object { -not $_.passed }).Count -gt 0) { $code=1 }
    $pages=[IO.File]::ReadAllText($http).TrimStart([char]0xfeff) | ConvertFrom-Json
    if ($pages.critical_passed -ne $pages.critical_total -or @($pages.targets | Where-Object { -not $_.critical -and -not $_.passed }).Count -gt 0) { $code=1 }
} catch { $code=1; $failure=$_.Exception.Message; Write-Host $failure -ForegroundColor Red }
finally {
    if ([IO.Directory]::Exists($logs)) {
        $result=[ordered]@{ schema=1; tool='NoDPI-Telegram-2.3'; created_utc=[DateTime]::UtcNow.ToString('o'); engine_pids=@(Get-Process -Name NoDPI -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id); error=$failure; targets=$rows; account_tested=$false; calls_tested=$false; success_scope='resPQ proves only the unauthenticated protocol exchange; test messages and media manually' }
        Write-AtomicText $report (ConvertTo-Json -InputObject $result -Depth 6)
        Write-Host ('Report: ' + $report)
        Write-Host 'Collect-Diagnostics.cmd includes these reports. Calls and logged-in sessions require a manual check.'
    }
}
exit $code
