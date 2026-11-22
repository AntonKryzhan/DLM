[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$logs = Join-Path $root 'logs'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$transcribing = $false
$resultCode = 1
$data = $null
function Read-Section([scriptblock]$Command) {
    try { return @(& $Command) } catch { return @([pscustomobject]@{ read_error=$_.Exception.Message }) }
}
function Test-DirectSocket([string]$Address) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $socket = [Net.Sockets.TcpClient]::new()
    try {
        $task = $socket.ConnectAsync([Net.IPAddress]::Parse($Address),443)
        if (-not $task.Wait(2500)) { throw 'TCP connect timeout' }
        [void]$task.GetAwaiter().GetResult()
        return [pscustomobject]@{ address=$Address; port=443; connected=$socket.Connected; elapsed_ms=$watch.ElapsedMilliseconds; error='' }
    } catch { return [pscustomobject]@{ address=$Address; port=443; connected=$false; elapsed_ms=$watch.ElapsedMilliseconds; error=$_.Exception.Message } }
    finally { $socket.Dispose() }
}
function Test-SystemName([string]$Name) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    try {
        $task = [Net.Dns]::GetHostAddressesAsync($Name)
        if (-not $task.Wait(3000)) { throw 'System DNS lookup timeout' }
        $addresses = @($task.GetAwaiter().GetResult() | Sort-Object @{Expression={ if ($_.AddressFamily -eq [Net.Sockets.AddressFamily]::InterNetwork) { 0 } else { 1 } }})
        $resolved = @($addresses | ForEach-Object { $_.ToString() })
        $dnsMs = $watch.ElapsedMilliseconds
        $connections = @(foreach ($address in ($resolved | Select-Object -First 2)) { Test-DirectSocket $address })
        return [pscustomobject]@{ host=$Name; resolved=$resolved; dns_ms=$dnsMs; dns_error=''; tcp=$connections }
    } catch { return [pscustomobject]@{ host=$Name; resolved=@(); dns_ms=$watch.ElapsedMilliseconds; dns_error=$_.Exception.Message; tcp=@() } }
}
function Invoke-Probe([string[]]$Arguments) {
    & (Join-Path $root 'NoDPIProbe.exe') @Arguments
    if ($LASTEXITCODE -eq 2) { throw 'NoDPIProbe could not run or save its report.' }
}
try {
    if ($env:OS -ne 'Windows_NT') { throw 'Windows is required.' }
    New-Item -ItemType Directory -Path $logs -Force | Out-Null
    Start-Transcript -LiteralPath (Join-Path $logs ('network-check-' + $stamp + '.txt')) -Force | Out-Null
    $transcribing = $true
    $engines = @(Get-Process -Name 'NoDPI' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
    $label = 'engine-stopped'
    if ($engines.Count -gt 0) { $label = 'engine-running' }
    Write-Host ('Read-only network check. NoDPI state: ' + $label)
    Write-Host 'No DNS, proxy, firewall, VPN, routes or adapter settings will be changed.'
    Write-Host 'A running VPN process does not prove that a tunnel or Kill Switch is active.'
    $data = [ordered]@{
        schema=3; time=(Get-Date -Format o); engine_pids_before=$engines; engine_pids_after=@(); state_changed_during_check=$false
        adapters=(Read-Section { Get-NetAdapter | Select-Object Name,InterfaceDescription,Status,LinkSpeed,InterfaceIndex })
        addresses=(Read-Section { Get-NetIPAddress | Select-Object InterfaceIndex,AddressFamily,IPAddress,PrefixLength })
        routes=(Read-Section { Get-NetRoute | Where-Object { $_.DestinationPrefix -in @('0.0.0.0/0','::/0') } | Select-Object InterfaceIndex,DestinationPrefix,NextHop,RouteMetric })
        dns_servers=(Read-Section { Get-DnsClientServerAddress | Select-Object InterfaceIndex,AddressFamily,ServerAddresses })
        winhttp_proxy=(Read-Section { & netsh.exe winhttp show proxy })
        user_proxy=(Read-Section {
            $entry = Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings'
            $enabled = $false; $pac = $false
            if ($entry.PSObject.Properties.Name -contains 'ProxyEnable') { $enabled = [bool]$entry.ProxyEnable }
            if ($entry.PSObject.Properties.Name -contains 'AutoConfigURL') { $pac = -not [string]::IsNullOrWhiteSpace([string]$entry.AutoConfigURL) }
            [pscustomobject]@{ proxy_enabled=$enabled; pac_configured=$pac }
        })
        related_processes=(Read-Section { Get-Process | Where-Object { $_.ProcessName -match '^(NoDPI|NoDPIGui|NoDPIProbe|AdGuardVpn|AmneziaVPN|AmneziaVPN-service|openvpn|wireguard|winws|winws2|goodbyedpi)$' } | Select-Object ProcessName,Id })
        kill_switch_state='not inspected; process presence is insufficient'
        browser_doh='not inspected; system DNS and WinHTTP are tested separately'
        system_name_tests=@(); direct_ip_tests=@()
    }
    $data.system_name_tests = @(foreach ($name in @('ya.ru','mail.ru','www.wikipedia.org','www.youtube.com','discord.com','chrome.cloudflare-dns.com')) {
        Write-Host ('System DNS and direct TCP: ' + $name)
        Test-SystemName $name
    })
    $data.direct_ip_tests = @(Test-DirectSocket '1.1.1.1'; Test-DirectSocket '8.8.8.8')
    $beforeReport = Join-Path $logs ('probe-baseline-' + $stamp + '.json')
    Invoke-Probe @('--baseline',('--report=' + $beforeReport),('--label=' + $label),'--timeout-ms=2500')
    Write-Host 'HTTPS service checks; a page is separate from video and voice.'
    $serviceReport = Join-Path $logs ('probe-network-' + $stamp + '.json')
    Invoke-Probe @(('--report=' + $serviceReport),('--label=' + $label),'--timeout-ms=3500')
    $after = @(Get-Process -Name 'NoDPI' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
    $data.engine_pids_after = $after
    $data.state_changed_during_check = ((@($engines | Sort-Object) -join ',') -ne (@($after | Sort-Object) -join ','))
    $data | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath (Join-Path $logs ('network-check-' + $stamp + '.json')) -Encoding UTF8
    Write-Host 'Reports saved. Use Collect-Diagnostics.cmd to create the ZIP.' -ForegroundColor Green
    Write-Host 'If stopped-engine checks fail, investigate VPN Kill Switch, DNS, proxy or other filters before testing DPI profiles.'
    $resultCode = 0
} catch { Write-Host ('NETWORK CHECK FAILED: ' + $_.Exception.Message) -ForegroundColor Red }
finally {
    if ($null -ne $data) {
        try {
            $after = @(Get-Process -Name 'NoDPI' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
            $data.engine_pids_after = $after
            $data.state_changed_during_check = ((@($engines | Sort-Object) -join ',') -ne (@($after | Sort-Object) -join ','))
            $data | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath (Join-Path $logs ('network-check-' + $stamp + '.json')) -Encoding UTF8
        } catch { Write-Host ('Could not save network snapshot: ' + $_.Exception.Message) -ForegroundColor Red }
    }
    if ($transcribing) { Stop-Transcript | Out-Null }
}
exit $resultCode
