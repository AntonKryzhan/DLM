[CmdletBinding()]
param([ValidateRange(0,300)][int]$ObserveSeconds=90)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
. (Join-Path $root 'tools\TrainingIO.ps1')
$logs=Join-Path $root 'logs'; $stamp=Get-Date -Format 'yyyyMMdd-HHmmss'; $code=1
$result=[ordered]@{ schema=1; tool='NoDPI-MWO-2.4'; created_utc=[DateTime]::UtcNow.ToString('o'); http=@(); dns=@(); tcp=@(); udp_local=@(); errors=@(); account_authentication_tested=$false; gameplay_tested=$false; observed_seconds=0; interpretation='Public HTTPS and socket reachability only. Test game login manually and record the exact error.' }
try {
    if ($env:OS -ne 'Windows_NT') { throw 'Windows is required' }
    New-Item -ItemType Directory -Path $logs -Force | Out-Null
    Write-Host 'Keep NoDPI enabled for this test. The check preserves the existing engine state.'
    foreach ($protocol in @('h2','h1')) {
        $report=Join-Path $logs ('probe-mwo-' + $stamp + '-' + $protocol + '.json')
        $arguments=@('--target=5',('--report=' + $report),'--timeout-ms=3000'); if ($protocol -eq 'h1') { $arguments += '--http1' }
        & (Join-Path $root 'NoDPIProbe.exe') @arguments
        if ($LASTEXITCODE -notin @(0,1) -or -not [IO.File]::Exists($report)) { throw 'MWO HTTPS probe failed to run. Rebuild version 2.4.' }
        $value=[IO.File]::ReadAllText($report).TrimStart([char]0xfeff) | ConvertFrom-Json
        $result.http += $value
        Write-Host ('{0}: MWO public HTTPS {1}/{2}; game login not tested.' -f $protocol,$value.critical_passed,$value.critical_total)
    }
    foreach ($name in @('mwomercs.com','ac.mwomercs.com','static.mwomercs.com')) {
        try { $answer=@(Resolve-DnsName -Name $name -DnsOnly -QuickTimeout -ErrorAction Stop | Select-Object Name,Type,IPAddress,NameHost); $result.dns += [pscustomobject]@{ host=$name; records=$answer; error='' } }
        catch { $result.dns += [pscustomobject]@{ host=$name; records=@(); error=$_.Exception.Message } }
    }
    Write-Host ('Now launch MWO and attempt login. Observing only MWOClient / MWOPortal sockets for ' + $ObserveSeconds + ' seconds. No credentials or traffic payloads are read.')
    $seen=[Collections.Generic.HashSet[string]]::new(); $udpSeen=[Collections.Generic.HashSet[string]]::new(); $watch=[Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $ObserveSeconds) {
        $owners=@(Get-Process -Name MWOClient,MWOPortal -ErrorAction SilentlyContinue | Select-Object ProcessName,Id)
        foreach ($owner in $owners) {
            foreach ($connection in @(Get-NetTCPConnection -OwningProcess $owner.Id -ErrorAction SilentlyContinue)) {
                if ($connection.State -eq 'Listen' -or $connection.RemotePort -eq 0 -or $result.tcp.Count -ge 2048) { continue }
                $key=@($owner.Id,$connection.LocalAddress,$connection.LocalPort,$connection.RemoteAddress,$connection.RemotePort,$connection.State) -join '|'
                if ($seen.Add($key)) { $result.tcp += [pscustomobject]@{ process=$owner.ProcessName; pid=$owner.Id; observed_utc=[DateTime]::UtcNow.ToString('o'); local_address=$connection.LocalAddress; local_port=$connection.LocalPort; peer_ip=$connection.RemoteAddress; peer_port=$connection.RemotePort; state=[string]$connection.State } }
            }
            foreach ($endpoint in @(Get-NetUDPEndpoint -OwningProcess $owner.Id -ErrorAction SilentlyContinue)) {
                $key=@($owner.Id,$endpoint.LocalAddress,$endpoint.LocalPort) -join '|'
                if ($result.udp_local.Count -lt 256 -and $udpSeen.Add($key)) { $result.udp_local += [pscustomobject]@{ process=$owner.ProcessName; pid=$owner.Id; local_address=$endpoint.LocalAddress; local_port=$endpoint.LocalPort; remote_peer_observed=$false } }
            }
        }
        Start-Sleep -Milliseconds 500
    }
    $result.observed_seconds=[int]$watch.Elapsed.TotalSeconds
    $code=0
    foreach ($http in $result.http) { if ($http.critical_total -eq 0 -or $http.critical_passed -ne $http.critical_total -or @($http.targets | Where-Object { -not $_.critical -and -not $_.passed }).Count -gt 0) { $code=1 } }
} catch { $result.errors += $_.Exception.Message; Write-Host $_.Exception.Message -ForegroundColor Red }
finally {
    if ([IO.Directory]::Exists($logs)) {
        $file=Join-Path $logs ('mwo-check-' + $stamp + '.json')
        Write-AtomicText $file (ConvertTo-Json -InputObject $result -Depth 9)
        Write-Host ('Report: ' + $file)
        Write-Host 'Add the actual game login result / error to MANUAL_RESULTS.txt and run Collect-Diagnostics.cmd.'
    }
}
exit $code
