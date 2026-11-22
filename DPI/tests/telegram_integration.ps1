Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path -Parent $PSScriptRoot
. (Join-Path $sourceRoot 'tools\TrainingIO.ps1')
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $sourceRoot 'Train.ps1'),[ref]$null,[ref]$null)
foreach ($name in @('Measure-Probes','Count-ObservedTransforms','Save-Profile')) {
    $node=@($ast.FindAll({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name },$true))
    if ($node.Count -ne 1) { throw ('Training function missing: ' + $name) }
    Invoke-Expression $node[0].Extent.Text
}
$root=Join-Path ([IO.Path]::GetTempPath()) ('DPI-TelegramTests-' + [Guid]::NewGuid().ToString('N'))
$logs=Join-Path $root 'logs'
New-Item -ItemType Directory -Path $logs -Force | Out-Null
try {
    $Target=3; $hasYoutube=$false; $hasDiscord=$false; $hasTelegram=$true; $hasMwo=$false; $cloudDigest=''
    $browser=''; $run='fixture'; $trialTag='current-trial'; $context='fixture'; $Seconds=600
    $status=@{ media=0; browser=0; baseline_passed=0; baseline_total=8; elapsed=60; tested=2; generation=1 }
    function Check-Stop { }
    function Start-Process {
        param($FilePath,$ArgumentList,$WorkingDirectory,$WindowStyle,[switch]$PassThru)
        if ($ArgumentList -notmatch '"--report=([^"]+)"' -or $ArgumentList -notmatch '"--target=3"') { throw 'Telegram HTTPS target or report argument lost' }
        [void]($ArgumentList -match '"--report=([^"]+)"'); $report=$Matches[1]
        if ($ArgumentList -match 'media-url') { throw 'YouTube media leaked into Telegram measurement' }
        $value=@{ critical_passed=2; critical_total=2; successful_latency_ms=40; targets=@(
            @{ target='telegram_web_a'; http_status=200; critical=$true; passed=$true; peer_ip='149.154.167.99'; local_port=45000 },
            @{ target='telegram_web_k'; http_status=200; critical=$true; passed=$true; peer_ip='149.154.167.99'; local_port=45001 },
            @{ target='control'; critical=$false; passed=$true }) }
        Write-AtomicText $report (ConvertTo-Json -InputObject $value -Depth 4)
        $process=[pscustomobject]@{ ExitCode=0; HasExited=$true }
        $process | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value { param($Timeout) return $true }
        $process | Add-Member -MemberType ScriptMethod -Name Dispose -Value { }
        return $process
    }
    function Measure-Telegram {
        param($Root,$CheckStop)
        foreach ($dc in @(1,2)) {
            [pscustomobject]@{ transport='tcp-obfuscated'; passed=$true; peer_ip='149.154.175.50'; local_port=(46000+$dc); elapsed_ms=20 }
            [pscustomobject]@{ transport='wss-obfuscated'; passed=($dc -eq 1); peer_ip=''; local_port=0; elapsed_ms=20 }
        }
    }
    $measurement=Measure-Probes 'candidate'
    if ($measurement.total -ne 8 -or $measurement.passed -ne 7 -or $measurement.controls_total -ne 2 -or $measurement.controls_passed -ne 2) { throw 'HTTP/WSS/TCP measurements were aggregated incorrectly' }
    if ($measurement.telegram_web_passed -ne 5 -or $measurement.telegram_web_total -ne 6 -or $measurement.telegram_desktop_passed -ne 2 -or $measurement.telegram_desktop_total -ne 2) { throw 'Separate Web/Desktop counts are incorrect' }
    $file=Join-Path $logs 'engine-fixture-123.jsonl'
    $events=@(
        @{ event='session_start'; trial_tag='unrelated' },
        @{ event='telegram_tcp_transform'; peer_ip='149.154.175.50'; local_port=46001; host='' },
        @{ event='session_start'; trial_tag=$trialTag },
        @{ event='telegram_tcp_transform'; peer_ip='149.154.175.50'; local_port=46001; host='' },
        @{ event='telegram_tcp_transform'; peer_ip='149.154.175.50'; local_port=49000; host='' },
        @{ event='packet_transform'; peer_ip='149.154.167.99'; local_port=45000; host='web.telegram.org' },
        @{ event='packet_transform'; peer_ip='149.154.167.99'; local_port=45001; host='telegram.org.attacker.example' },
        @{ event='packet_transform'; peer_ip='149.154.167.99'; local_port=45001; host='www.youtube.com' })
    Write-AtomicText $file ((($events | ForEach-Object { ConvertTo-Json -InputObject $_ -Compress }) -join "`n") + "`n")
    if ((Count-ObservedTransforms 123 $measurement.connections) -ne 2) { throw 'Telegram attribution accepted the wrong session/socket/domain/target' }
    if ((Count-ObservedTransforms 999 $measurement.connections) -ne 0) { throw 'Telegram attribution accepted a different process' }
    $item=[pscustomobject]@{ mwo_passed=0; mwo_total=0; gene='1,1,64,8,0,0,1048576,0'; passed=7; total=8; controls_passed=2; controls_total=2; attributable=2; telegram_web_passed=5; telegram_web_total=6; telegram_desktop_passed=2; telegram_desktop_total=2 }
    Write-AtomicText (Join-Path $root 'active.profile') 'previous-working-profile'
    Save-Profile $item $false
    if ([IO.File]::ReadAllText((Join-Path $root 'active.profile')) -ne 'previous-working-profile') { throw 'Unvalidated candidate replaced the active profile' }
    Save-Profile $item $true
    $profile=[IO.File]::ReadAllText((Join-Path $root 'active.profile'))
    foreach ($field in @('Target=3','TelegramWebPassed=5','TelegramDesktopPassed=2','+MTProto-TCP-WSS','Validated=1')) { if (-not $profile.Contains($field)) { throw ('Profile metadata missing: ' + $field) } }
    if ([IO.File]::ReadAllText((Join-Path $root 'active.profile.previous')) -ne 'previous-working-profile') { throw 'Previous profile backup failed' }
    $collector=[Management.Automation.Language.Parser]::ParseFile((Join-Path $sourceRoot 'Collect-Diagnostics.ps1'),[ref]$null,[ref]$null)
    $pattern=$collector.FindAll({ param($n) $n -is [Management.Automation.Language.StringConstantExpressionAst] -and $n.Value.StartsWith('^(engine-') },$true)[0].Value
    foreach ($name in @('telegram-check-fixture.json','telegram-training-fixture.json','probe-telegram-fixture.json')) { if ($name -notmatch $pattern) { throw ('Collector missed ' + $name) } }
    if ('tdata' -match $pattern -or 'media-url.txt' -match $pattern) { throw 'Collector selected private data' }
    Write-Host 'Telegram training integration passed: protocol totals, attribution, profile preservation and report selection.'
} finally { [IO.Directory]::Delete($root,$true) }
