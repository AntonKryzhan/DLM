[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$stage = Join-Path ([IO.Path]::GetTempPath()) ('DPI-diagnostics-' + [Guid]::NewGuid().ToString('N'))
$output = Join-Path $root ('DPI-diagnostics-' + $stamp + '.zip')
$result = 1
function Read-SharedText([string]$Path, [int]$Maximum) {
    $stream = [IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    try {
        $count = [Math]::Min([long]$Maximum,$stream.Length)
        [void]$stream.Seek(-$count,[IO.SeekOrigin]::End)
        $buffer = New-Object byte[] ([int]$count)
        $offset = 0
        while ($offset -lt $buffer.Length) { $read = $stream.Read($buffer,$offset,$buffer.Length-$offset); if ($read -eq 0) { break }; $offset += $read }
        return [Text.Encoding]::UTF8.GetString($buffer,0,$offset)
    } finally { $stream.Dispose() }
}
function Save-Section([string]$Name, [scriptblock]$Command) {
    try { (& $Command | Out-String -Width 240) | Set-Content -LiteralPath (Join-Path $stage $Name) -Encoding UTF8 }
    catch { ('Read failed: ' + $_.Exception.Message) | Set-Content -LiteralPath (Join-Path $stage $Name) -Encoding UTF8 }
}
try {
    if ($env:OS -ne 'Windows_NT') { throw 'Windows is required.' }
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $stage 'logs') -Force | Out-Null
    $logs = Join-Path $root 'logs'
    if (Test-Path -LiteralPath $logs -PathType Container) {
        $files = @(Get-ChildItem -LiteralPath $logs -File | Where-Object { $_.Name -match '^(engine-.*\.jsonl|probe-.*\.(json|txt)|tune-results-.*\.json|tune-.*\.txt|network-check-.*\.(json|txt)|training\.status|training-results-.*\.json|training-population-.*\.txt|training-best\.profile|startup-.*\.(json|status)|driver-(preflight|check|system)-.*\.json|network-context-.*\.json|mwo-check-.*\.json|browser-training-.*\.json|telegram-(training|check)-.*\.json)$' } | Sort-Object LastWriteTime -Descending | Select-Object -First 64)
        foreach ($file in $files) {
            if ($file.Length -le 9MB) { Read-SharedText $file.FullName 9437184 | Set-Content -LiteralPath (Join-Path (Join-Path $stage 'logs') $file.Name) -Encoding UTF8 }
        }
    }
    $mainLog = Join-Path $root 'NoDPI.log'
    if (Test-Path -LiteralPath $mainLog -PathType Leaf) { Read-SharedText $mainLog 4194304 | Set-Content -LiteralPath (Join-Path $stage 'NoDPI.log') -Encoding UTF8 }
    . (Join-Path $root 'tools\DriverDiagnostics.ps1')
    Save-Section 'driver-system.json' { Get-DPIDriverSystem $root | ConvertTo-Json -Depth 8 }
    $safeConfig = [Collections.Generic.List[string]]::new()
    $config = Join-Path $root 'config.ini'
    if (Test-Path -LiteralPath $config -PathType Leaf) {
        foreach ($line in [IO.File]::ReadAllLines($config,[Text.Encoding]::UTF8)) {
            if ($line -match '^\s*(Hostlist|HostlistExclude|IPSet|IPSetExclude|AwsJson)\s*=') {
                $key = $Matches[1]
                $equals = $line.IndexOf('=')
                $value = $line.Substring($equals+1).Trim().Trim('"')
                $configured = -not [string]::IsNullOrWhiteSpace($value)
                $safeConfig.Add($key + '=configured:' + $configured)
            } elseif ($line -match '^\s*(CloudCdnEnabled|TelegramEnabled|SafeAutomatic|Mode|AggressiveFrag|FragBySNI|ApplyECH|AdaptiveRetry|LearnStrategy|FakePackets|HttpManip|ReverseFrag|QuicMode|SplitMode|SplitStrategy|FakeMode|FirstPackets|HandshakeJitterMs|QuicProbeMs|AutoTTL|MinTTL|AwsWhitelistOnly|Debug|DebugVerbose|AdaptiveStrategies|ServerWindowCandidates|UdpObfuscate|DiagnosticLog|StrategyProfile|HandshakeTimeoutMs)\s*=') { $safeConfig.Add($line) }
        }
    }
    $safeConfig | Set-Content -LiteralPath (Join-Path $stage 'config-redacted.txt') -Encoding UTF8
    Save-Section 'windows.txt' { Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' | Select-Object ProductName,DisplayVersion,CurrentBuild,UBR; Get-Date -Format o }
    Save-Section 'adapters.txt' { Get-NetAdapter | Select-Object Name,InterfaceDescription,Status,LinkSpeed,InterfaceIndex }
    Save-Section 'addresses.txt' { Get-NetIPAddress | Select-Object InterfaceIndex,AddressFamily,IPAddress,PrefixLength,AddressState }
    Save-Section 'dns.txt' { Get-DnsClientServerAddress | Select-Object InterfaceIndex,AddressFamily,ServerAddresses; if (Get-Command Get-DnsClientDohServerAddress -ErrorAction SilentlyContinue) { Get-DnsClientDohServerAddress | Select-Object ServerAddress,AllowFallbackToUdp,AutoUpgrade } }
    Save-Section 'default-routes.txt' { Get-NetRoute | Where-Object { $_.DestinationPrefix -in @('0.0.0.0/0','::/0') } | Select-Object InterfaceIndex,DestinationPrefix,NextHop,RouteMetric,State }
    Save-Section 'related-processes.txt' { Get-Process | Where-Object { $_.ProcessName -match '^(NoDPI|NoDPIGui|NoDPIProbe|AmneziaVPN|AmneziaVPN-service|openvpn|wireguard|winws|winws2|goodbyedpi|AdGuardVPN)$' } | Select-Object ProcessName,Id }
    Save-Section 'related-services.txt' { Get-Service | Where-Object { $_.Name -match 'WinDivert|Amnezia|NoDPI|zapret' } | Select-Object Name,Status,DisplayName }
    foreach ($name in @('data\telegram-cidr.txt','active.profile','active.profile.previous','cache\auto-targets.txt','build-info.json','build-local.json','build\build.log')) { $file=Join-Path $root $name; if (Test-Path -LiteralPath $file -PathType Leaf) { Read-SharedText $file 1048576 | Set-Content -LiteralPath (Join-Path $stage ([IO.Path]::GetFileName($name))) -Encoding UTF8 } }
    foreach ($dir in @('data','cache')) { foreach ($name in @('cloudflare-cidr.txt','cloudfront-cidr.txt','azure-frontdoor-cidr.txt','azure-cloud-cidr.txt','cdn-sources.json','cdn-refresh.json')) { $path=Join-Path (Join-Path $root $dir) $name; if (Test-Path -LiteralPath $path -PathType Leaf) { Read-SharedText $path 1048576 | Set-Content -LiteralPath (Join-Path $stage ($dir + '-' + $name)) -Encoding UTF8 } } }
    Save-Section 'binary-hashes.txt' { foreach ($name in @('NoDPI.exe','NoDPIGui.exe','NoDPIProbe.exe','DPITrainer.exe','WinDivert.dll','WinDivert64.sys')) { $file = Join-Path $root $name; if (Test-Path -LiteralPath $file -PathType Leaf) { Get-FileHash -LiteralPath $file -Algorithm SHA256 | Select-Object Algorithm,Hash,@{Name='File';Expression={$name}} } } }
    $manual = Join-Path $root 'MANUAL_RESULTS.txt'
    if (-not (Test-Path -LiteralPath $manual -PathType Leaf)) {
        @('Time of test:', 'Provider / region (optional):', 'Selected profile:', 'Other VPN / DPI tools running:', 'YouTube page opens:', 'YouTube video plays for 2 minutes / quality:', 'Discord opens / messages:', 'Discord voice call works for 2 minutes:', 'Telegram Web A / K opens and messages work:', 'Telegram Desktop messages / media:', 'Telegram calls:', 'MWO website / launcher opens:', 'MWO account login / exact error:', 'MWO match / game connection:', 'Other affected sites:', 'Notes:') | Set-Content -LiteralPath $manual -Encoding UTF8
    }
    Copy-Item -LiteralPath $manual -Destination (Join-Path $stage 'MANUAL_RESULTS.txt')
    'Contains domains/peer IPs from engine logs and network configuration. It does not collect packet bodies, cookies, passwords, VPN keys, browser history, media URL files, or full INI paths. Review before sharing.' | Set-Content -LiteralPath (Join-Path $stage 'CONTENTS.txt') -Encoding UTF8
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $output -CompressionLevel Optimal
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($output)
    try { if ($archive.Entries.Count -lt 3) { throw 'Diagnostic ZIP is unexpectedly empty.' } } finally { $archive.Dispose() }
    Write-Host ('READY: ' + $output) -ForegroundColor Green
    Write-Host 'You can fill MANUAL_RESULTS.txt and collect again. Send the resulting ZIP for analysis.'
    $result = 0
} catch { Write-Host ('COLLECTION FAILED: ' + $_.Exception.Message) -ForegroundColor Red }
finally { if (Test-Path -LiteralPath $stage -PathType Container) { Remove-Item -LiteralPath $stage -Recurse -Force } }
exit $result
