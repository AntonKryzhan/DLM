[CmdletBinding()]
param([ValidateRange(60,3600)][int]$Seconds=600, [ValidateRange(0,6)][int]$Target=0, [ValidatePattern('^[A-Za-z0-9-]{1,64}$')][string]$Session=([Guid]::NewGuid().ToString('N')), [int]$OwnerPid=0)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$logs = Join-Path $root 'logs'
$engine = $null
$watch = [Diagnostics.Stopwatch]::StartNew()
$status = [ordered]@{ phase='starting'; session=$Session; elapsed=0; budget=$Seconds; generation=0; tested=0; passed=0; total=0; controls_passed=0; controls_total=0; attributable=0; validated=0; media=0; browser=0; target=$Target; mwo_passed=0; mwo_total=0; telegram_web_passed=0; telegram_web_total=0; telegram_desktop_passed=0; telegram_desktop_total=0; baseline_passed=0; baseline_total=0; gene=''; message='Preparing real network measurements' }
$run = Get-Date -Format 'yyyyMMdd-HHmmss'
$pool = [Collections.Generic.List[object]]::new()
$best = $null
$context = ''
$trialTag = ''
$exitCode = 1
$trainingMutex=$null
$ownsMutex=$false
$owner=$null
$contextWatch=[Diagnostics.Stopwatch]::StartNew()
$mediaAtStart=$false
$mediaDigest=''
$cloudDigest=''
. (Join-Path $root 'tools\TrainingIO.ps1')
. (Join-Path $root 'tools\BrowserProbe.ps1')
. (Join-Path $root 'tools\TelegramProbe.ps1')
. (Join-Path $root 'tools\DriverIO.ps1')
. (Join-Path $root 'tools\CloudIO.ps1')
$hasYoutube=$Target -in @(0,2,4,6)
$hasDiscord=$Target -in @(1,2,4,6)
$hasTelegram=$Target -in @(3,4,6)
$hasMwo=$Target -in @(5,6)
$browser=''
function Publish-Status {
    $status.elapsed = [int]$watch.Elapsed.TotalSeconds
    $text = ($status.GetEnumerator() | ForEach-Object { $_.Key + '=' + [string]$_.Value }) -join "`n"
    Write-AtomicText (Join-Path $logs 'training.status') ($text + "`n")
}
function Check-Stop {
    Publish-Status
    if ($null -ne $owner -and $owner.HasExited) { throw [OperationCanceledException]::new('GUI owner closed') }
    if (Test-Path -LiteralPath (Join-Path $logs ('training-' + $Session + '.stop'))) { throw [OperationCanceledException]::new('Training cancelled') }
    if ($watch.Elapsed.TotalSeconds -ge $Seconds) { throw [OperationCanceledException]::new('Training time budget finished') }
    if ((Test-Path -LiteralPath (Join-Path $root 'media-url.txt') -PathType Leaf) -ne $mediaAtStart) { throw 'Media probe file changed during training' }
    if ($mediaAtStart -and (Get-FileHash -LiteralPath (Join-Path $root 'media-url.txt') -Algorithm SHA256).Hash -ne $mediaDigest) { throw 'Media probe URL changed during training' }
    if ($contextWatch.ElapsedMilliseconds -ge 1000) {
        if ($hasMwo -and (Get-DPICloudContext $root) -ne $cloudDigest) { throw [OperationCanceledException]::new('CDN range data changed during training; comparison stopped') }
        $nowSnapshot=Get-DPINetworkSnapshot
        if ((Get-DPINetworkHash $nowSnapshot) -ne $context) {
            Write-AtomicText (Join-Path $logs ('network-context-' + $run + '-changed.json')) (ConvertTo-Json -InputObject $nowSnapshot -Depth 4)
            throw [OperationCanceledException]::new('Active network path, addresses, metrics or DNS changed; comparison stopped. See network-context reports.')
        }
        $contextWatch.Restart()
    }
}
function Stop-OwnedEngine {
    if ($null -eq $script:engine) { return }
    try {
        if (-not $script:engine.HasExited) {
            try { $event = [Threading.EventWaitHandle]::OpenExisting('Local\NoDPI.Stop.' + $script:engine.Id); try { [void]$event.Set() } finally { $event.Dispose() } } catch { }
            if (-not $script:engine.WaitForExit(4000)) { $script:engine.Kill(); [void]$script:engine.WaitForExit(1000) }
        }
    } finally { $script:engine.Dispose(); $script:engine = $null }
}
function Start-Candidate([string]$Gene) {
    $script:trialTag = [Guid]::NewGuid().ToString('N')
    $arguments = @(('--trial-tag=' + $script:trialTag),('--config=' + (Join-Path $root 'config.ini')),'--automatic',('--gene=' + $Gene),('--gene-target=' + $Target),('--parent-pid=' + $PID))
    $command = ($arguments | ForEach-Object { Quote-DPIArgument $_ }) -join ' '
    $script:engine = Start-Process -FilePath (Join-Path $root 'NoDPI.exe') -ArgumentList $command -WorkingDirectory $root -PassThru -WindowStyle Hidden
    $readyWatch = [Diagnostics.Stopwatch]::StartNew()
    while ($readyWatch.ElapsedMilliseconds -lt 25000) {
        Check-Stop
        if ($script:engine.HasExited) { throw ('Candidate engine exited: ' + $script:engine.ExitCode + '. ' + (Get-DPIStartupFailure $root $script:engine.Id)) }
        try {
            $event = [Threading.EventWaitHandle]::OpenExisting('Local\NoDPI.Ready.' + $script:engine.Id)
            try { if ($event.WaitOne(0)) { return $script:engine.Id } } finally { $event.Dispose() }
        } catch [Threading.WaitHandleCannotBeOpenedException] { }
        Start-Sleep -Milliseconds 100
    }
    throw 'Engine readiness timeout'
}
function Measure-Probes([string]$Label) {
    $passed=0; $total=0; $cp=0; $ct=0; $latency=0L; $mp=0; $mt=0; $twp=0; $twt=0; $tdp=0; $tdt=0; $connections=[Collections.Generic.List[object]]::new()
    foreach ($protocol in @('h2','h1')) {
        Check-Stop
        $report = Join-Path $logs ('probe-training-' + $run + '-' + $Label + '-' + $protocol + '.json')
        $arguments = @(('--report=' + $report),('--label=train-' + $Label),('--target=' + $Target),'--timeout-ms=1800')
        if ($protocol -eq 'h1') { $arguments += '--http1' }
        $mediaFile = Join-Path $root 'media-url.txt'
        if ($hasYoutube -and (Test-Path -LiteralPath $mediaFile -PathType Leaf)) { $arguments += '--media-url-file=' + $mediaFile; $status.media=1 }
        $command = ($arguments | ForEach-Object { Quote-DPIArgument $_ }) -join ' '
        $process = Start-Process -FilePath (Join-Path $root 'NoDPIProbe.exe') -ArgumentList $command -WorkingDirectory $root -WindowStyle Hidden -PassThru
        $finished=$false
        try {
            while (-not $process.WaitForExit(250)) { Check-Stop }
            $finished=$true
            if ($process.ExitCode -notin @(0,1) -or -not (Test-Path -LiteralPath $report)) { throw 'Probe crashed or did not produce a report' }
        } finally { if (-not $finished -and -not $process.HasExited) { $process.Kill(); [void]$process.WaitForExit(1000) }; $process.Dispose() }
        $value = [IO.File]::ReadAllText($report).TrimStart([char]0xfeff) | ConvertFrom-Json
        foreach ($test in $value.targets) { if ($test.target -like 'telegram_*' -and $test.http_status -eq 429) { throw 'TELEGRAM_RATE_LIMIT_429: training stopped to avoid repeated requests' } }
        foreach ($test in $value.targets) { if ($test.target -eq 'youtube_media_sample' -and $test.http_status -in @(400,401,403,410)) { throw 'The media URL was rejected by the server. Refresh media-url.txt before comparing strategies.' } }
        foreach ($test in $value.targets) { if ($test.target -like 'mwo_*') { $mt++; if ($test.passed) { $mp++ } } }
        foreach ($test in $value.targets) { if ($test.target -like 'telegram_web_*') { $twt++; if ($test.passed) { $twp++ } } }
        foreach ($test in $value.targets) { if ($test.critical -and $test.passed -and $test.PSObject.Properties.Name -contains 'peer_ip' -and $test.PSObject.Properties.Name -contains 'local_port') { $connections.Add([pscustomobject]@{ peer_ip=$test.peer_ip; local_port=$test.local_port }) } }
        $passed += [int]$value.critical_passed; $total += [int]$value.critical_total; $latency += [long]$value.successful_latency_ms
        foreach ($test in $value.targets) { if (-not $test.critical) { $ct++; if ($test.passed) { $cp++ } } }
    }
    if ($browser -and $hasYoutube) {
        Check-Stop
        $dom=Measure-DPIBrowser $browser (Join-Path $logs ('browser-training-' + $run + '-' + $Label + '.json'))
        $total++; if ($dom.passed) { $passed++; $latency += [long]$dom.elapsed_ms }
    }
    if ($hasTelegram) {
        Check-Stop
        $telegram=@(Measure-Telegram -Root $root -CheckStop { Check-Stop })
        Write-AtomicText (Join-Path $logs ('telegram-training-' + $run + '-' + $Label + '.json')) (ConvertTo-Json -InputObject $telegram -Depth 5)
        foreach ($test in $telegram) {
            $total++
            if ($test.transport -eq 'wss-obfuscated') { $twt++; if ($test.passed) { $twp++ } }else { $tdt++; if ($test.passed) { $tdp++ } }
            if ($test.passed) { $passed++; $latency += [long]$test.elapsed_ms; if ($test.peer_ip -and $test.local_port -gt 0) { $connections.Add([pscustomobject]@{ peer_ip=$test.peer_ip; local_port=$test.local_port }) } }
        }
    }
    return [pscustomobject]@{ mwo_passed=$mp; mwo_total=$mt; telegram_web_passed=$twp; telegram_web_total=$twt; telegram_desktop_passed=$tdp; telegram_desktop_total=$tdt; passed=$passed; total=$total; controls_passed=$cp; controls_total=$ct; latency=$latency; connections=@($connections.ToArray()) }
}
function Count-ObservedTransforms([int]$EngineId, [object[]]$Connections=@()) {
    $tuples=[Collections.Generic.HashSet[string]]::new()
    foreach ($connection in $Connections) { [void]$tuples.Add(([string]$connection.peer_ip + '|' + [string]$connection.local_port)) }
    $count = 0
    foreach ($file in Get-ChildItem -LiteralPath $logs -Filter ('engine-*-' + $EngineId + '.jsonl') -File) {
        $belongs=$false
        foreach ($line in [IO.File]::ReadAllLines($file.FullName)) {
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            $event = $line.TrimStart([char]0xfeff) | ConvertFrom-Json
            if ($event.event -eq 'session_start') { $belongs=($event.PSObject.Properties.Name -contains 'trial_tag' -and $event.trial_tag -eq $script:trialTag) }
            if ($belongs -and $event.event -in @('packet_transform','telegram_tcp_transform') -and $tuples.Contains(([string]$event.peer_ip + '|' + [string]$event.local_port))) {
                if (($hasMwo -and $event.host -match '(^|\.)(mwomercs\.com|cloudfront\.net|azurefd\.net|azureedge\.net|vo\.msecnd\.net|azurewebsites\.net|blob\.core\.windows\.net|web\.core\.windows\.net|cloudapp\.net|cloudapp\.azure\.com|trafficmanager\.net)$') -or ($hasTelegram -and $event.event -eq 'telegram_tcp_transform') -or ($hasTelegram -and $event.host -match '(^|\.)(telegram\.org|telegram-cdn\.org|t\.me|telegram\.me)$') -or ($hasYoutube -and $event.host -match '(^|\.)(youtube\.com|googlevideo\.com|ytimg\.com|youtu\.be)$') -or ($hasDiscord -and $event.host -match '(^|\.)(discord\.com|discordapp\.(com|net)|discord\.gg)$')) { $count++ }
            }
        }
    }
    return $count
}
function Save-Profile($Item, [bool]$Validated) {
    $text = @('[DPIProfile]','Schema=1',('Gene=' + $Item.gene),('Target=' + $Target),('MwoHttpPassed=' + $Item.mwo_passed),('MwoHttpTotal=' + $Item.mwo_total),'MwoAccountTested=0','MwoGameplayTested=0',('TelegramWebPassed=' + $Item.telegram_web_passed),('TelegramWebTotal=' + $Item.telegram_web_total),('TelegramDesktopPassed=' + $Item.telegram_desktop_passed),('TelegramDesktopTotal=' + $Item.telegram_desktop_total),('Validated=' + [int]$Validated),('Passed=' + $Item.passed),('Total=' + $Item.total),('ControlsPassed=' + $Item.controls_passed),('ControlsTotal=' + $Item.controls_total),('Attributable=' + $Item.attributable),('MediaTested=' + $status.media),('BrowserTested=' + $status.browser),('BaselinePassed=' + $status.baseline_passed),('BaselineTotal=' + $status.baseline_total),('CloudDataContext=' + $cloudDigest),('Context=' + $context),('Elapsed=' + $status.elapsed),('Budget=' + $Seconds),('Tested=' + $status.tested),('Generation=' + $status.generation),('CreatedUtc=' + [DateTime]::UtcNow.ToString('o')),('Stack=WinHTTP-H2-H1' + $(if ($hasMwo) { '+MWO-Public-HTTPS' } else { '' }) + $(if ($hasTelegram) { '+MTProto-TCP-WSS' } else { '' }) + $(if ($status.browser -eq 1) { '+Chromium' } else { '' })),'VoiceTested=0') -join "`n"
    Write-AtomicText (Join-Path $logs 'training-best.profile') ($text + "`n")
    if ($Validated) { $active=Join-Path $root 'active.profile'; if ([IO.File]::Exists($active)) { Write-AtomicText (Join-Path $root 'active.profile.previous') ([IO.File]::ReadAllText($active)) }; Write-AtomicText $active ($text + "`n") }
}
try {
    if ($env:OS -ne 'Windows_NT') { throw 'Windows is required' }
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Administrator rights are required for training engines' }
    if ($null -ne (Get-Process -Name 'NoDPI' -ErrorAction SilentlyContinue)) { throw 'Stop the existing NoDPI engine before training' }
    foreach ($name in @('DPITrainer.exe','NoDPI.exe','NoDPIProbe.exe','config.ini')) { if (-not (Test-Path -LiteralPath (Join-Path $root $name))) { throw ('Missing: ' + $name) } }
    New-Item -ItemType Directory -Path $logs -Force | Out-Null
    $trainingMutex=[Threading.Mutex]::new($false,'Local\NoDPI.Training.v1')
    $ownsMutex=$trainingMutex.WaitOne(0)
    if (-not $ownsMutex) { throw 'Another training session is running' }
    if ($OwnerPid -gt 0) { $owner=[Diagnostics.Process]::GetProcessById($OwnerPid); [void]$owner.Handle }
    $mediaAtStart=Test-Path -LiteralPath (Join-Path $root 'media-url.txt') -PathType Leaf
    if ($mediaAtStart) { $mediaDigest=(Get-FileHash -LiteralPath (Join-Path $root 'media-url.txt') -Algorithm SHA256).Hash }
    $status.phase='driver_check'; $status.message='Checking WinDivert before network probes'; Publish-Status
    Test-DPIDriver $root (Join-Path $logs ('driver-preflight-' + $run + '.json'))
    if ($hasMwo) { Update-DPICloudRanges $root; $cloudDigest=Get-DPICloudContext $root }
    $networkSnapshot=Get-DPINetworkSnapshot
    $context=Get-DPINetworkHash $networkSnapshot
    Write-AtomicText (Join-Path $logs ('network-context-' + $run + '-before.json')) (ConvertTo-Json -InputObject $networkSnapshot -Depth 4)
    Write-AutoTargets $root
    if ($hasYoutube) { $browser=Find-DPIBrowser; if ($browser) { $status.browser=1 } }
    $status.phase='baseline'; Publish-Status
    $baseline = Measure-Probes 'control-before'
    $status.baseline_passed=$baseline.passed; $status.baseline_total=$baseline.total
    if ($baseline.controls_passed -ne $baseline.controls_total) { throw 'Control sites failed before training; repair baseline connection first' }
    $baselineClear=($baseline.passed -eq $baseline.total -and $baseline.total -gt 0)
    if ($baselineClear) { foreach ($field in @('mwo_passed','mwo_total','telegram_web_passed','telegram_web_total','telegram_desktop_passed','telegram_desktop_total')) { $status[$field]=$baseline.$field }; $status.passed=$baseline.passed; $status.total=$baseline.total; $status.controls_passed=$baseline.controls_passed; $status.controls_total=$baseline.controls_total }
    $population = @('1,0,0,0,0,0,1048576,0','0,1,5,0,0,0,1048576,0','1,1,64,8,1,2,65536,0','1,32,512,16,2,1,1048576,1','0,2,128,0,1,3,4194304,2','1,5,768,24,1,1,16777216,1','0,64,256,16,2,2,262144,0','1,1,1200,32,1,2,1048576,2')
    $active=Join-Path $root 'active.profile'
    if (Test-Path -LiteralPath $active -PathType Leaf) {
        $saved=[IO.File]::ReadAllText($active)
        if ($saved -match ('(?m)^Target=' + $Target + '\r?$') -and $saved -match '(?m)^Gene=([^\r\n]+)') {
            $warmInput=Join-Path $logs 'training-warm.txt'; $warmOutput=Join-Path $logs 'training-warm-next.txt'
            Write-AtomicText $warmInput ($Matches[1] + "|0`n")
            & (Join-Path $root 'DPITrainer.exe') $warmInput $warmOutput
            if ($LASTEXITCODE -ne 0) { throw 'Saved strategy is invalid; the DLM worker rejected it' }
            $population=@([IO.File]::ReadAllLines($warmOutput) | Where-Object { $_ })
        }
    }
    for ($generation=1; $generation -le 60; $generation++) {
        if ($baselineClear) { break }
        $status.generation=$generation; $status.phase='searching'
        foreach ($gene in (Convert-DPIPopulation $population ($Target -eq 5))) {
            Check-Stop; $status.message='Measuring candidate with HTTPS and selected service protocol probes'; Publish-Status
            $id=0; $measurement=$null
            try { $id=Start-Candidate $gene; $measurement=Measure-Probes ('g' + $generation + '-n' + $status.tested) }
            finally { Stop-OwnedEngine }
            if ($measurement.total -ne $baseline.total) { throw 'Probe target count changed; comparisons were stopped' }
            $observed = Count-ObservedTransforms $id $measurement.connections
            $score=0
            if ($observed -gt 0 -and $measurement.controls_passed -eq $measurement.controls_total) {
                $score = [Math]::Max(0,[int](900000.0*$measurement.passed/[Math]::Max(1,$measurement.total) - [Math]::Min(20000,$measurement.latency)/20.0))
            }
            $item=[pscustomobject]@{ mwo_passed=$measurement.mwo_passed; mwo_total=$measurement.mwo_total; telegram_web_passed=$measurement.telegram_web_passed; telegram_web_total=$measurement.telegram_web_total; telegram_desktop_passed=$measurement.telegram_desktop_passed; telegram_desktop_total=$measurement.telegram_desktop_total; gene=$gene; score=$score; passed=$measurement.passed; total=$measurement.total; controls_passed=$measurement.controls_passed; controls_total=$measurement.controls_total; attributable=$observed; generation=$generation }
            $pool.Add($item); $status.tested++
            if ($null -eq $best -or $item.score -gt $best.score) { $best=$item; Save-Profile $best $false }
            foreach ($field in @('mwo_passed','mwo_total','telegram_web_passed','telegram_web_total','telegram_desktop_passed','telegram_desktop_total')) { $status[$field]=$best.$field }
            $status.gene=$best.gene; $status.passed=$best.passed; $status.total=$best.total; $status.controls_passed=$best.controls_passed; $status.controls_total=$best.controls_total; $status.attributable=$best.attributable
            if ($observed -eq 0) { $status.message='No attributable target transformation; success is not credited to this method' }
            Publish-Status
            if ($item.score -gt 0 -and $item.passed -eq $item.total -and $item.total -gt 0 -and $item.passed -gt $baseline.passed) {
                $status.phase='validating'; Publish-Status
                try { $verifyId=Start-Candidate $gene; $held=Measure-Probes ('holdout-' + $status.tested) } finally { Stop-OwnedEngine }
                $verified=Count-ObservedTransforms $verifyId $held.connections
                $after=Measure-Probes ('control-after-' + $status.tested)
                if ($held.total -eq $baseline.total -and $after.total -eq $baseline.total -and $held.passed -eq $held.total -and $held.controls_passed -eq $held.controls_total -and $verified -gt 0 -and $after.controls_passed -eq $after.controls_total -and $held.passed -gt $after.passed) {
                    foreach ($field in @('mwo_passed','mwo_total','telegram_web_passed','telegram_web_total','telegram_desktop_passed','telegram_desktop_total')) { $item.$field += $held.$field }
                    $item.passed += $held.passed; $item.total += $held.total; $item.controls_passed += $held.controls_passed; $item.controls_total += $held.controls_total; $item.attributable += $verified; $best=$item
                    Save-Profile $best $true; foreach ($field in @('mwo_passed','mwo_total','telegram_web_passed','telegram_web_total','telegram_desktop_passed','telegram_desktop_total')) { $status[$field]=$best.$field }; $status.validated=1; $status.passed=$best.passed; $status.total=$best.total; $status.phase='completed'; $status.message='Protocol probe improvement confirmed on held-out attempts; account, video and voice still need manual verification'; Publish-Status; $exitCode=0; break
                }
                $status.phase='searching'
            }
        }
        if ($status.validated -eq 1) { break }
        $history=Join-Path $logs ('training-population-' + $run + '.txt')
        $top=@($pool | Group-Object gene | ForEach-Object { $_.Group | Sort-Object score -Descending | Select-Object -First 1 } | Sort-Object score -Descending | Select-Object -First 128)
        Write-AtomicText $history ((($top | ForEach-Object { $_.gene + '|' + $_.score }) -join "`n") + "`n")
        $next=Join-Path $logs 'training-next.txt'
        & (Join-Path $root 'DPITrainer.exe') $history $next
        if ($LASTEXITCODE -ne 0) { throw 'DLM evolution worker failed' }
        $population=@([IO.File]::ReadAllLines($next) | Where-Object { $_ })
    }
    if ($status.validated -ne 1) { $status.phase='completed'; $status.message='No validated improvement; previous active profile preserved'; if ($baselineClear) { $status.message='All probes already pass without NoDPI. No evasion improvement can be demonstrated in this network state.' }; $exitCode=0 }
} catch [OperationCanceledException] { $status.phase='stopped'; $status.message=$_.Exception.Message; $exitCode=0 }
catch { $status.phase='error'; $status.message=$_.Exception.Message }
finally {
    Stop-OwnedEngine
    if ($null -ne $owner) { $owner.Dispose() }
    if ($null -ne $trainingMutex) { if ($ownsMutex) { $trainingMutex.ReleaseMutex() }; $trainingMutex.Dispose() }
    Remove-Item -LiteralPath (Join-Path $logs ('training-' + $Session + '.stop')) -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $logs) {
        Publish-Status
        Write-AtomicText (Join-Path $logs ('training-results-' + $run + '.json')) (ConvertTo-Json -InputObject @($pool.ToArray()) -Depth 6)
    }
}
exit $exitCode
