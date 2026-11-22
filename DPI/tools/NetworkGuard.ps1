Set-StrictMode -Version Latest
function Get-DPIFamily($Value) {
    if ([string]$Value -in @('IPv4','InterNetwork','2')) { return '4' }
    if ([string]$Value -in @('IPv6','InterNetworkV6','23')) { return '6' }
    return [string]$Value
}
function Convert-DPINetworkSnapshot([object[]]$Adapters, [object[]]$Addresses, [object[]]$Routes, [object[]]$Interfaces=@(), [object[]]$Dns=@()) {
    $active=[Collections.Generic.HashSet[int]]::new(); $lines=[Collections.Generic.List[string]]::new(); $metrics=@{}; $known=[Collections.Generic.HashSet[int]]::new()
    foreach ($a in $Adapters) {
        [void]$known.Add([int]$a.InterfaceIndex)
        if ([string]$a.Status -ne 'Up') { continue }
        $index=[int]$a.InterfaceIndex; [void]$active.Add($index)
        $mac=([string]$a.MacAddress).Replace('-','').Replace(':','').ToUpperInvariant()
        $lines.Add(('A|{0}|{1}' -f $index,$mac))
    }
    foreach ($i in $Interfaces) { if (-not $known.Contains([int]$i.InterfaceIndex) -and [int]$i.InterfaceIndex -ne 1 -and $i.PSObject.Properties.Name -contains 'ConnectionState' -and [string]$i.ConnectionState -eq 'Connected') { [void]$active.Add([int]$i.InterfaceIndex); $lines.Add(('A|{0}|ip-interface' -f [int]$i.InterfaceIndex)) } }
    foreach ($i in $Interfaces) { if ($active.Contains([int]$i.InterfaceIndex)) { $metrics[[string]$i.InterfaceIndex + '|' + (Get-DPIFamily $i.AddressFamily)]=[string]$i.InterfaceMetric } }
    foreach ($a in $Addresses) {
        if (-not $active.Contains([int]$a.InterfaceIndex) -or [string]$a.AddressState -ne 'Preferred') { continue }
        if ($a.PSObject.Properties.Name -contains 'SkipAsSource' -and $a.SkipAsSource) { continue }
        $ip=$null
        if (-not [Net.IPAddress]::TryParse([string]$a.IPAddress,[ref]$ip)) { continue }
        $bytes=$ip.GetAddressBytes()
        if ([Net.IPAddress]::IsLoopback($ip) -or $ip.IsIPv6LinkLocal -or ($bytes.Length -eq 4 -and $bytes[0] -eq 169 -and $bytes[1] -eq 254)) { continue }
        $lines.Add(('I|{0}|{1}|{2}' -f [int]$a.InterfaceIndex,$ip.ToString().ToLowerInvariant(),[int]$a.PrefixLength))
    }
    foreach ($r in $Routes) {
        if (-not $active.Contains([int]$r.InterfaceIndex)) { continue }
        $routeAddress=$null
        if (-not [Net.IPAddress]::TryParse(([string]$r.DestinationPrefix).Split('/')[0],[ref]$routeAddress)) { continue }
        $routeBytes=$routeAddress.GetAddressBytes()
        if ([Net.IPAddress]::IsLoopback($routeAddress) -or $routeAddress.IsIPv6LinkLocal -or ($routeBytes.Length -eq 16 -and $routeBytes[0] -eq 255) -or ($routeBytes.Length -eq 4 -and ($routeBytes[0] -ge 224 -or ($routeBytes[0] -eq 169 -and $routeBytes[1] -eq 254)))) { continue }
        $family='4'; if ([string]$r.DestinationPrefix -like '*:*') { $family='6' }
        $metric=''; $key=[string]$r.InterfaceIndex + '|' + $family; if ($metrics.ContainsKey($key)) { $metric=$metrics[$key] }
        $lines.Add(('R|{0}|{1}|{2}|{3}|{4}' -f [int]$r.InterfaceIndex,[string]$r.DestinationPrefix,([string]$r.NextHop).ToLowerInvariant(),[int]$r.RouteMetric,$metric))
    }
    foreach ($d in $Dns) { if ($active.Contains([int]$d.InterfaceIndex)) { $servers=@($d.ServerAddresses | ForEach-Object { ([string]$_).ToLowerInvariant() }) -join ','; $lines.Add(('D|{0}|{1}|{2}' -f [int]$d.InterfaceIndex,(Get-DPIFamily $d.AddressFamily),$servers)) } }
    return [pscustomobject]@{ schema=1; lines=@($lines | Sort-Object -Unique) }
}
function Get-DPINetworkSnapshot {
    $adapters=@(Get-NetAdapter -ErrorAction Stop)
    $addresses=@(Get-NetIPAddress -ErrorAction Stop)
    $routes=@(Get-NetRoute -PolicyStore ActiveStore -ErrorAction Stop)
    $interfaces=@(Get-NetIPInterface -ErrorAction Stop)
    $dns=@(Get-DnsClientServerAddress -ErrorAction Stop)
    return Convert-DPINetworkSnapshot $adapters $addresses $routes $interfaces $dns
}
function Get-DPINetworkHash($Snapshot) {
    $text=(@($Snapshot.lines | Sort-Object -Unique) -join "`n") + "`n"
    $hash=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($text)))).Replace('-','') } finally { $hash.Dispose() }
}
