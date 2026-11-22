Set-StrictMode -Version Latest
function Get-DPIStartupFailure([string]$Root, [int]$EngineId) {
    $path=Join-Path $Root ('logs\startup-' + $EngineId + '.json')
    if ([IO.File]::Exists($path)) {
        try {
            $v=[IO.File]::ReadAllText($path).TrimStart([char]0xfeff) | ConvertFrom-Json
            if ([int]$v.pid -eq $EngineId -and -not $v.ready) { return ('Startup component=' + $v.component + '; Win32=' + $v.win32_error + '. Run Check-Driver.cmd and collect diagnostics.') }
        } catch { }
    }
    return 'No structured startup result. Run Check-Driver.cmd and collect diagnostics.'
}
function Invoke-DPIDriverCheck([string]$Root, [string]$Report) {
    & (Join-Path $Root 'NoDPI.exe') ('--config=' + (Join-Path $Root 'config.ini')) '--driver-check' '--automatic' ('--driver-report=' + $Report) | Out-Host
    return $LASTEXITCODE
}
function Test-DPIDriver([string]$Root, [string]$Report) {
    if ([IO.File]::Exists($Report)) { [IO.File]::Delete($Report) }
    $exitCode=Invoke-DPIDriverCheck $Root $Report
    if (-not [IO.File]::Exists($Report)) { throw ('Driver preflight exited ' + $exitCode + ' without a report. Rebuild version 2.4 or check application loading.') }
    $v=[IO.File]::ReadAllText($Report).TrimStart([char]0xfeff) | ConvertFrom-Json
    if ($v.PSObject.Properties.Name -notcontains 'ready' -or $v.ready -isnot [bool]) { throw 'Invalid driver preflight report' }
    if ($exitCode -ne 0 -or -not $v.ready) { throw ('WinDivert preflight: component=' + $v.component + '; Win32=' + $v.win32_error + '. No training candidate was started. Run Check-Driver.cmd.') }
}
