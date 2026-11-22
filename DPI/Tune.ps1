[CmdletBinding()]
param(
    [ValidateRange(1,3)][int]$Repeats = 2,
    [string]$MediaUrlFile = '',
    [switch]$ApplyBest
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$logs = Join-Path $root 'logs'
$enginePath = Join-Path $root 'NoDPI.exe'
$probePath = Join-Path $root 'NoDPIProbe.exe'
$configPath = Join-Path $root 'config.ini'
$engineProcess = $null
$runId = Get-Date -Format 'yyyyMMdd-HHmmss'
$transcribing = $false
$resultCode = 1

function Quote-WindowsArgument([string]$Value) {
    $quoted = '"'
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') { $slashes++; continue }
        if ($character -eq '"') { $quoted += ('\' * ($slashes * 2 + 1)); $quoted += '"' }
        else { $quoted += ('\' * $slashes); $quoted += $character }
        $slashes = 0
    }
    $quoted += ('\' * ($slashes * 2))
    return ($quoted + '"')
}
function Stop-OwnEngine($Process) {
    if ($null -eq $Process) { return }
    try {
        if (-not $Process.HasExited) {
            try {
                $event = [Threading.EventWaitHandle]::OpenExisting('Local\NoDPI.Stop.' + $Process.Id)
                try { [void]$event.Set() } finally { $event.Dispose() }
            } catch { Write-Host 'The engine stop event was unavailable.' }
            if (-not $Process.WaitForExit(5000)) { $Process.Kill(); [void]$Process.WaitForExit(2000) }
        }
    } finally { $Process.Dispose() }
}
function Wait-EngineReady($Process) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.ElapsedMilliseconds -lt 25000) {
        if ($Process.HasExited) { throw ('Engine exited before interception was ready. Exit code: ' + $Process.ExitCode) }
        try {
            $event = [Threading.EventWaitHandle]::OpenExisting('Local\NoDPI.Ready.' + $Process.Id)
            try { if ($event.WaitOne(0)) { return } } finally { $event.Dispose() }
        } catch [Threading.WaitHandleCannotBeOpenedException] { }
        Start-Sleep -Milliseconds 100
    }
    throw 'Engine readiness timeout. See NoDPI.log and logs/engine-*.jsonl.'
}
. (Join-Path $root 'tools\Settings.ps1')

try {
    if ($env:OS -ne 'Windows_NT') { throw 'Windows is required.' }
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run Tune.cmd as administrator.' }
    foreach ($path in @($enginePath,$probePath,$configPath)) { if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw ('Missing file: ' + $path) } }
    if ($null -ne (Get-Process -Name 'NoDPI' -ErrorAction SilentlyContinue)) { throw 'Stop the engine in the GUI before tuning. Other processes will not be terminated.' }
    if ($MediaUrlFile) { $MediaUrlFile = [IO.Path]::GetFullPath($MediaUrlFile); if (-not (Test-Path -LiteralPath $MediaUrlFile -PathType Leaf)) { throw 'Media URL file not found.' } }
    New-Item -ItemType Directory -Path $logs -Force | Out-Null
    Start-Transcript -LiteralPath (Join-Path $logs ('tune-' + $runId + '.txt')) -Force | Out-Null
    $transcribing = $true
    Write-Host 'Testing 8 TCP profiles, including control (0). This may take several minutes.'
    Write-Host 'HTTP probe results do not verify video playback or Discord voice.'
    $rows = [Collections.Generic.List[object]]::new()
    foreach ($profile in 0..7) {
        $passCount = 0
        $totalCount = 0
        $controlFailures = 0
        $latency = 0L
        $errors = [Collections.Generic.List[string]]::new()
        try {
            Write-Host ('Profile {0}/7' -f $profile)
            $arguments = @('--config=' + $configPath), '--adaptive-strategies', '--no-automatic', ('--strategy-profile=' + $profile), '--quic-mode=auto', '--diagnostic-log', '--aggressive-frag'
            $commandLine = ($arguments | ForEach-Object { Quote-WindowsArgument ([string]$_) }) -join ' '
            $engineProcess = Start-Process -FilePath $enginePath -ArgumentList $commandLine -WorkingDirectory $root -PassThru
            Wait-EngineReady $engineProcess
            foreach ($attempt in 1..$Repeats) {
                $report = Join-Path $logs ('probe-' + $runId + '-p' + $profile + '-r' + $attempt + '.json')
                $probeArgs = @('--report=' + $report), ('--label=p' + $profile + '-r' + $attempt), '--timeout-ms=3500'
                if ($MediaUrlFile) { $probeArgs += '--media-url-file=' + $MediaUrlFile }
                $probeCommand = ($probeArgs | ForEach-Object { Quote-WindowsArgument ([string]$_) }) -join ' '
                $stdout = Join-Path $logs ('probe-' + $runId + '-p' + $profile + '-r' + $attempt + '.txt')
                $stderr = $stdout + '.err'
                $probeProcess = Start-Process -FilePath $probePath -ArgumentList $probeCommand -WorkingDirectory $root -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
                try {
                    if (-not $probeProcess.WaitForExit(150000)) { $probeProcess.Kill(); throw 'Probe process exceeded 150 seconds.' }
                    if ($probeProcess.ExitCode -eq 2) { throw 'Probe could not run or save its report.' }
                } finally { $probeProcess.Dispose() }
                if (-not (Test-Path -LiteralPath $report -PathType Leaf)) { throw 'Probe report was not produced.' }
                $measurement = Get-Content -LiteralPath $report -Raw -Encoding UTF8 | ConvertFrom-Json
                $passCount += [int]$measurement.critical_passed
                $totalCount += [int]$measurement.critical_total
                $latency += [long]$measurement.successful_latency_ms
                foreach ($target in $measurement.targets) { if (-not $target.critical -and -not $target.passed) { $controlFailures++ } }
                Write-Host ('  attempt {0}: {1}/{2} critical HTTP checks; controls failed: {3}' -f $attempt,$measurement.critical_passed,$measurement.critical_total,$controlFailures)
            }
        } catch { $errors.Add($_.Exception.Message); Write-Host ('  profile error: ' + $_.Exception.Message) -ForegroundColor Yellow }
        finally { Stop-OwnEngine $engineProcess; $engineProcess = $null }
        $expected = 4 * $Repeats
        if ($MediaUrlFile) { $expected = 5 * $Repeats }
        $rows.Add([pscustomobject]@{ profile=$profile; critical_passed=$passCount; critical_expected=$expected; measured_total=$totalCount; control_failures=$controlFailures; successful_latency_ms=$latency; errors=@($errors.ToArray()); complete=($totalCount -eq $expected -and $errors.Count -eq 0) })
    }
    $ranked = @($rows | Sort-Object @{Expression='critical_passed';Descending=$true},@{Expression='control_failures';Ascending=$true},@{Expression='successful_latency_ms';Ascending=$true},@{Expression='profile';Ascending=$true})
    $best = $ranked[0]
    $baseline = @($rows | Where-Object { $_.profile -eq 0 })[0]
    $better = $best.critical_passed -gt $baseline.critical_passed -and $best.control_failures -le $baseline.control_failures
    $verifiedHttp = $best.complete -and $best.critical_passed -eq $best.critical_expected -and $best.control_failures -eq 0
    $summary = [ordered]@{ schema=2; run=$runId; repeats=$Repeats; media_tested=[bool]$MediaUrlFile; voice_tested=$false; recommended_profile=$best.profile; improved_over_control=$better; all_http_checks_passed=$verifiedHttp; applied=$false; profiles=@($rows.ToArray()); interpretation='Measured HTTP/SChannel probes. Playback, browser stack and Discord voice still require manual verification.' }
    if ($ApplyBest -and $verifiedHttp -and $better) {
        Copy-Item -LiteralPath $configPath -Destination ($configPath + '.before-tune-' + $runId + '.bak')
        Set-NoDPIValue $configPath 'SafeAutomatic' 'false'
        Set-NoDPIValue $configPath 'AdaptiveStrategies' 'true'
        Set-NoDPIValue $configPath 'StrategyProfile' ([string]$best.profile)
        Set-NoDPIValue $configPath 'QuicMode' 'auto'
        Set-NoDPIValue $configPath 'DiagnosticLog' 'true'
        $summary.applied = $true
        Write-Host ('Applied profile {0}; config backup saved. Reopen the GUI before starting it.' -f $best.profile) -ForegroundColor Green
    } elseif ($ApplyBest) { Write-Host 'No profile met the apply threshold; configuration was preserved.' -ForegroundColor Yellow }
    $summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $logs ('tune-results-' + $runId + '.json')) -Encoding UTF8
    $ranked | Format-Table profile,critical_passed,critical_expected,control_failures,complete -AutoSize
    Write-Host ('Best measured HTTP profile: {0}. Improved over control: {1}.' -f $best.profile,$better)
    Write-Host 'Run Collect-Diagnostics.cmd after testing a video and a Discord voice call.'
    if ($verifiedHttp) { $resultCode = 0 }
} catch { Write-Host ('TUNING FAILED: ' + $_.Exception.Message) -ForegroundColor Red }
finally {
    Stop-OwnEngine $engineProcess
    if ($transcribing) { Stop-Transcript | Out-Null }
}
exit $resultCode
