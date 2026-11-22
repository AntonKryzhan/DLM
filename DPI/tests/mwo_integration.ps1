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
$root=Join-Path ([IO.Path]::GetTempPath()) ('DPI-MWOTests-' + [Guid]::NewGuid().ToString('N'))
$logs=Join-Path $root 'logs'; New-Item -ItemType Directory -Path $logs -Force | Out-Null
try {
    $Target=5;$hasYoutube=$false;$hasDiscord=$false;$hasTelegram=$false;$hasMwo=$true
    $browser='';$run='fixture';$trialTag='mwo-trial';$context='network-fixture';$cloudDigest='cloud-fixture';$Seconds=600
    $status=@{media=0;browser=0;baseline_passed=0;baseline_total=8;elapsed=60;tested=2;generation=1}
    function Check-Stop { }
    function Start-Process {
        param($FilePath,$ArgumentList,$WorkingDirectory,$WindowStyle,[switch]$PassThru)
        if ($ArgumentList -notmatch '"--target=5"' -or $ArgumentList -notmatch '"--report=([^"]+)"') { throw 'MWO target/report argument lost' }
        $report=$Matches[1]
        if ($ArgumentList -match 'media-url') { throw 'YouTube media probe leaked into MWO target' }
        $targets=@();$port=45000
        foreach ($name in @('mwo_website','mwo_login','mwo_ac_frontend','mwo_public_css')) { $targets += @{target=$name;http_status=200;critical=$true;passed=$true;peer_ip='104.16.1.2';local_port=$port};$port++ }
        foreach ($name in @('control1','control2','control3')) { $targets += @{target=$name;critical=$false;passed=$true} }
        Write-AtomicText $report (ConvertTo-Json -InputObject @{ critical_passed=4;critical_total=4;successful_latency_ms=40;targets=$targets } -Depth 4)
        $process=[pscustomobject]@{ExitCode=0;HasExited=$true}
        $process | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value { param($Timeout) return $true }
        $process | Add-Member -MemberType ScriptMethod -Name Dispose -Value { }
        return $process
    }
    $measurement=Measure-Probes 'candidate'
    if ($measurement.mwo_passed -ne 8 -or $measurement.mwo_total -ne 8 -or $measurement.total -ne 8 -or $measurement.controls_total -ne 6 -or $measurement.controls_passed -ne 6 -or $measurement.telegram_web_total -ne 0) { throw 'MWO HTTPS / controls / legacy service totals mixed' }
    $events=@(@{event='session_start';trial_tag='other'},@{event='packet_transform';host='ac.mwomercs.com';peer_ip='104.16.1.2';local_port=45000},@{event='session_start';trial_tag=$trialTag},@{event='packet_transform';host='ac.mwomercs.com';peer_ip='104.16.1.2';local_port=45000},@{event='packet_transform';host='d123.cloudfront.net';peer_ip='104.16.1.2';local_port=45001},@{event='packet_transform';host='evilcloudfront.net';peer_ip='104.16.1.2';local_port=45002},@{event='packet_transform';host='ac.mwomercs.com.evil.net';peer_ip='104.16.1.2';local_port=45003},@{event='packet_transform';host='mwomercs.com';peer_ip='104.16.1.2';local_port=49999})
    Write-AtomicText (Join-Path $logs 'engine-fixture-123.jsonl') ((($events | ForEach-Object { ConvertTo-Json -InputObject $_ -Compress }) -join "`n") + "`n")
    if ((Count-ObservedTransforms 123 $measurement.connections) -ne 2) { throw 'MWO attribution accepted a spoofed domain, unrelated socket or stale session' }
    $item=[pscustomobject]@{gene='1,1,5,8,0,0,1048576,0';passed=8;total=8;controls_passed=6;controls_total=6;attributable=2;mwo_passed=8;mwo_total=8;telegram_web_passed=0;telegram_web_total=0;telegram_desktop_passed=0;telegram_desktop_total=0}
    Write-AtomicText (Join-Path $root 'active.profile') 'working-profile'
    Save-Profile $item $false
    if ([IO.File]::ReadAllText((Join-Path $root 'active.profile')) -ne 'working-profile') { throw 'Unvalidated MWO candidate replaced working profile' }
    Save-Profile $item $true
    $profile=[IO.File]::ReadAllText((Join-Path $root 'active.profile'))
    foreach ($line in @('Target=5','MwoHttpPassed=8','MwoHttpTotal=8','MwoAccountTested=0','MwoGameplayTested=0','CloudDataContext=cloud-fixture','+MWO-Public-HTTPS')) { if (-not $profile.Contains($line)) { throw ('MWO profile lost scope metadata: ' + $line) } }
    $collector=[Management.Automation.Language.Parser]::ParseFile((Join-Path $sourceRoot 'Collect-Diagnostics.ps1'),[ref]$null,[ref]$null)
    $pattern=$collector.FindAll({param($n) $n -is [Management.Automation.Language.StringConstantExpressionAst] -and $n.Value.StartsWith('^(engine-')},$true)[0].Value
    foreach ($name in @('driver-preflight-fixture.json','driver-check-fixture.json','startup-123.status','startup-123.json','network-context-fixture-changed.json','mwo-check-fixture.json','probe-mwo-fixture-h1.json')) { if ($name -notmatch $pattern) { throw ('Diagnostics missed ' + $name) } }
    Write-Host 'MWO training totals, attribution, profile preservation and diagnostic report selection passed.'
} finally { [IO.Directory]::Delete($root,$true) }
