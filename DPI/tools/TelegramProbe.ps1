Set-StrictMode -Version Latest

function Invoke-DPIProbeCheckpoint([scriptblock]$Callback) {
    try { & $Callback } catch { throw [OperationCanceledException]::new(('Probe cancelled by training controller: ' + $_.Exception.Message),$_.Exception) }
}
function New-TelegramRandom([int]$Count) {
    $bytes=New-Object byte[] $Count
    $rng=[Security.Cryptography.RandomNumberGenerator]::Create()
    try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
    return ,$bytes
}
function New-TelegramCTR([byte[]]$Key, [byte[]]$IV) {
    if ($Key.Length -ne 32 -or $IV.Length -ne 16) { throw 'Invalid AES-CTR key or counter length' }
    $aes=[Security.Cryptography.Aes]::Create()
    $aes.Mode=[Security.Cryptography.CipherMode]::ECB
    $aes.Padding=[Security.Cryptography.PaddingMode]::None
    $aes.Key=$Key
    return @{ aes=$aes; encryptor=$aes.CreateEncryptor(); counter=[byte[]]$IV.Clone(); block=(New-Object byte[] 16); position=16 }
}
function Convert-TelegramCTR($State, [byte[]]$Bytes) {
    $output=New-Object byte[] $Bytes.Length
    for ($i=0; $i -lt $Bytes.Length; $i++) {
        if ($State.position -eq 16) {
            [void]$State.encryptor.TransformBlock($State.counter,0,16,$State.block,0)
            for ($j=15; $j -ge 0; $j--) {
                if ($State.counter[$j] -ne 255) { $State.counter[$j]++; break }
                $State.counter[$j]=0
            }
            $State.position=0
        }
        $output[$i]=$Bytes[$i] -bxor $State.block[$State.position]
        $State.position++
    }
    return ,$output
}
function Close-TelegramCTR($State) {
    if ($null -ne $State) { $State.encryptor.Dispose(); $State.aes.Dispose() }
}
function New-TelegramObfuscation {
    do {
        $header=New-TelegramRandom 64
        $word=[BitConverter]::ToUInt32($header,0)
    } while ($header[0] -eq 0xef -or $word -in @(0x44414548,0x54534f50,0x20544547,0x4954504f,0x02010316,[uint32]4008636142,[uint32]3722304989) -or [BitConverter]::ToUInt32($header,4) -eq 0)
    # Derive the reverse direction before substituting the encrypted tail.
    $reversed=[byte[]]$header.Clone(); [Array]::Reverse($reversed)
    $tx=New-TelegramCTR ([byte[]]$header[8..39]) ([byte[]]$header[40..55])
    $rx=New-TelegramCTR ([byte[]]$reversed[8..39]) ([byte[]]$reversed[40..55])
    try {
        for ($i=56; $i -lt 60; $i++) { $header[$i]=0xef }
        $encrypted=Convert-TelegramCTR $tx $header
        [Array]::Copy($encrypted,56,$header,56,8)
        return @{ header=$header; tx=$tx; rx=$rx }
    } catch { Close-TelegramCTR $tx; Close-TelegramCTR $rx; throw }
}
function Wait-TelegramTask($Task, $Watch, [int]$TimeoutMs, [scriptblock]$CheckStop) {
    while (-not $Task.IsCompleted) {
        Invoke-DPIProbeCheckpoint $CheckStop
        if ($Watch.ElapsedMilliseconds -ge $TimeoutMs) { throw [TimeoutException]::new('Telegram probe deadline exceeded') }
        [void]$Task.Wait(80)
    }
    Invoke-DPIProbeCheckpoint $CheckStop
    return $Task.GetAwaiter().GetResult()
}
function Receive-TelegramExact($Connection, [int]$Count) {
    if ($Count -lt 1 -or $Count -gt 1048576) { throw 'Invalid Telegram frame size' }
    $output=New-Object byte[] $Count
    $offset=0
    while ($offset -lt $Count) {
        Invoke-DPIProbeCheckpoint $Connection.check
        if ($Connection.watch.ElapsedMilliseconds -ge $Connection.timeout) { throw [TimeoutException]::new('Telegram receive deadline exceeded') }
        if ($Connection.pending.Length -eq $Connection.offset) {
            $buffer=New-Object byte[] 16384
            if ($Connection.ws) {
                $result=Wait-TelegramTask ($Connection.ws.ReceiveAsync([ArraySegment[byte]]::new($buffer),$Connection.cancel.Token)) $Connection.watch $Connection.timeout $Connection.check
                if ($result.MessageType -eq [Net.WebSockets.WebSocketMessageType]::Close) {
                    if ($result.CloseStatusDescription -match '^\s*-?429\s*$') { throw 'TELEGRAM_RATE_LIMIT_429' }
                    throw 'Telegram WebSocket closed before resPQ'
                }
                if ($result.MessageType -ne [Net.WebSockets.WebSocketMessageType]::Binary) { throw 'Unexpected Telegram WebSocket text frame' }
                $received=$result.Count
            } else {
                $received=Wait-TelegramTask ($Connection.stream.ReadAsync($buffer,0,$buffer.Length)) $Connection.watch $Connection.timeout $Connection.check
                if ($received -eq 0) { throw 'Telegram TCP closed before resPQ' }
            }
            if ($received -eq 0) { continue }
            $chunk=New-Object byte[] $received; [Array]::Copy($buffer,0,$chunk,0,$received)
            if ($Connection.rx) { $chunk=Convert-TelegramCTR $Connection.rx $chunk }
            $Connection.pending=$chunk; $Connection.offset=0
        }
        $take=[Math]::Min($Count-$offset,$Connection.pending.Length-$Connection.offset)
        [Array]::Copy($Connection.pending,$Connection.offset,$output,$offset,$take)
        $Connection.offset += $take; $offset += $take
    }
    return ,$output
}
function Receive-TelegramFrame($Connection) {
    $first=(Receive-TelegramExact $Connection 1)[0]
    if ($first -gt 127 -or $first -eq 0) { throw 'Invalid abridged response header' }
    $words=[int]$first
    if ($first -eq 127) {
        $tail=Receive-TelegramExact $Connection 3
        $words=[int]$tail[0] + 256*[int]$tail[1] + 65536*[int]$tail[2]
        if ($words -lt 127) { throw 'Noncanonical abridged response length' }
    }
    if ($words -lt 1 -or $words -gt 262144) { throw 'Telegram response exceeds probe limit' }
    return ,(Receive-TelegramExact $Connection ($words*4))
}
function Invoke-TelegramProbe([string]$Root, [int]$Dc, [string]$Address, [ValidateSet('tcp-abridged','tcp-obfuscated','wss-obfuscated')][string]$Transport, [int]$Port=443, [int]$TimeoutMs=5500, [scriptblock]$CheckStop={}) {
    $watch=[Diagnostics.Stopwatch]::StartNew()
    $row=[ordered]@{ target=('telegram_dc' + $Dc + '_' + $Transport); dc=$Dc; transport=$Transport; critical=$true; passed=$false; stage='context'; elapsed_ms=0; peer_ip=''; peer_port=$Port; local_port=0; res_pq_valid=$false; validation_code=0; transport_error=0; account_tested=$false; error='' }
    $temporary=Join-Path ([IO.Path]::GetTempPath()) ('DPI-Telegram-' + [Guid]::NewGuid().ToString('N'))
    $obfuscation=$null; $tcp=$null; $ws=$null; $cancel=[Threading.CancellationTokenSource]::new(); $stream=$null; $securityProtocol=$null
    try {
        Invoke-DPIProbeCheckpoint $CheckStop
        $worker=Join-Path $Root 'DPITrainer.exe'
        if (-not [IO.File]::Exists($worker)) { throw 'DPITrainer.exe missing; rebuild version 2.3 first' }
        New-Item -ItemType Directory -Path $temporary | Out-Null
        $context=New-TelegramRandom 24
        $unixTicks=[DateTime]::UtcNow.Ticks - [DateTime]::new(1970,1,1,0,0,0,[DateTimeKind]::Utc).Ticks
        $unix=[long][Math]::Floor($unixTicks/10000000.0)
        $fraction=[uint64][Math]::Floor(($unixTicks % 10000000)*4294967296.0/10000000.0)
        $messageId=[uint64]$unix * [uint64]4294967296 + ($fraction -band [uint64]4294967292)
        [Array]::Copy([BitConverter]::GetBytes($messageId),0,$context,16,8)
        $contextFile=Join-Path $temporary 'context.bin'; $requestFile=Join-Path $temporary 'request.bin'
        [IO.File]::WriteAllBytes($contextFile,$context)
        & $worker '--mtp-build' $contextFile $requestFile
        if ($LASTEXITCODE -ne 0) { throw 'DLM MTProto request encoder failed' }
        $request=[IO.File]::ReadAllBytes($requestFile)
        if ($request.Length -ne 41) { throw 'DLM MTProto request length mismatch' }
        $row.stage='connect'
        if ($Transport -eq 'wss-obfuscated') {
            $names=@('','pluto','venus','aurora','vesta','flora')
            if ($Dc -lt 1 -or $Dc -gt 5) { throw 'Invalid Telegram DC' }
            $securityProtocol=[Net.ServicePointManager]::SecurityProtocol
            [Net.ServicePointManager]::SecurityProtocol=$securityProtocol -bor [Net.SecurityProtocolType]::Tls12
            $ws=[Net.WebSockets.ClientWebSocket]::new(); $ws.Options.Proxy=$null
            $ws.Options.AddSubProtocol('binary'); $ws.Options.SetRequestHeader('Origin','https://web.telegram.org')
            $uri=[Uri]('wss://' + $names[$Dc] + '.web.telegram.org/apiws')
            $row['host']=$uri.DnsSafeHost
            [void](Wait-TelegramTask ($ws.ConnectAsync($uri,$cancel.Token)) $watch $TimeoutMs $CheckStop)
        } else {
            $ip=[Net.IPAddress]::Parse($Address); $tcp=[Net.Sockets.TcpClient]::new($ip.AddressFamily); $tcp.NoDelay=$true
            [void](Wait-TelegramTask ($tcp.ConnectAsync($ip,$Port)) $watch $TimeoutMs $CheckStop)
            $stream=$tcp.GetStream(); $row.peer_ip=$tcp.Client.RemoteEndPoint.Address.ToString(); $row.local_port=$tcp.Client.LocalEndPoint.Port
        }
        $row.stage='send-req-pq'
        if ($Transport -ne 'tcp-abridged') {
            $obfuscation=New-TelegramObfuscation
            $encrypted=Convert-TelegramCTR $obfuscation.tx $request
            $out=New-Object byte[] (64+$encrypted.Length); [Array]::Copy($obfuscation.header,0,$out,0,64); [Array]::Copy($encrypted,0,$out,64,$encrypted.Length)
        } else { $out=New-Object byte[] (1+$request.Length); $out[0]=0xef; [Array]::Copy($request,0,$out,1,$request.Length) }
        if ($ws) { [void](Wait-TelegramTask ($ws.SendAsync([ArraySegment[byte]]::new($out),[Net.WebSockets.WebSocketMessageType]::Binary,$true,$cancel.Token)) $watch $TimeoutMs $CheckStop) }
        else { [void](Wait-TelegramTask ($stream.WriteAsync($out,0,$out.Length)) $watch $TimeoutMs $CheckStop) }
        $row.stage='await-res-pq'
        $rx=$null; if ($obfuscation) { $rx=$obfuscation.rx }
        $connection=@{ stream=$stream; ws=$ws; cancel=$cancel; rx=$rx; watch=$watch; timeout=$TimeoutMs; check=$CheckStop; pending=[byte[]]@(); offset=0 }
        $body=Receive-TelegramFrame $connection
        $nonceFile=Join-Path $temporary 'nonce.bin'; $bodyFile=Join-Path $temporary 'response.bin'; $resultFile=Join-Path $temporary 'result.json'
        [IO.File]::WriteAllBytes($nonceFile,[byte[]]$context[0..15]); [IO.File]::WriteAllBytes($bodyFile,$body)
        & $worker '--mtp-verify' $nonceFile $bodyFile $resultFile
        if ($LASTEXITCODE -ne 0) { throw 'DLM MTProto response verifier failed' }
        $result=[IO.File]::ReadAllText($resultFile) | ConvertFrom-Json
        $row.res_pq_valid=[bool]$result.res_pq_valid; $row.validation_code=[int]$result.validation_code; $row.transport_error=[int]$result.transport_error
        if ($row.transport_error -eq 429) { throw 'TELEGRAM_RATE_LIMIT_429' }
        $row.passed=$row.res_pq_valid; $row.stage='res-pq-verified'
        if (-not $row.passed) { $row.stage='invalid-res-pq' }
    } catch [OperationCanceledException] { throw }
    catch {
        if ($_.Exception.Message -eq 'TELEGRAM_RATE_LIMIT_429' -or $_.Exception.ToString() -match '(?i)(status|HTTP|response|code)[^\r\n]{0,100}\b429\b') { throw 'TELEGRAM_RATE_LIMIT_429' }
        $row.error=$_.Exception.GetType().Name + ': ' + $_.Exception.Message
    } finally {
        if ($null -ne $securityProtocol) { [Net.ServicePointManager]::SecurityProtocol=$securityProtocol }
        $cancel.Cancel()
        if ($ws) { $ws.Abort(); $ws.Dispose() }; if ($stream) { $stream.Dispose() }; if ($tcp) { $tcp.Dispose() }; $cancel.Dispose()
        if ($obfuscation) { Close-TelegramCTR $obfuscation.tx; Close-TelegramCTR $obfuscation.rx }
        if ([IO.Directory]::Exists($temporary)) { [IO.Directory]::Delete($temporary,$true) }
        $row.elapsed_ms=$watch.ElapsedMilliseconds
    }
    return [pscustomobject]$row
}
function Measure-Telegram([string]$Root, [scriptblock]$CheckStop={}, [switch]$Extended) {
    # Built-in production endpoints from telegramdesktop/tdesktop, dc_options.cpp.
    $servers=@(@{ dc=1; ip='149.154.175.50' },@{ dc=2; ip='149.154.167.51' })
    if ($Extended) { $servers += @(@{ dc=3; ip='149.154.175.100' },@{ dc=4; ip='149.154.167.91' },@{ dc=5; ip='149.154.171.5' }) }
    $rows=[Collections.Generic.List[object]]::new()
    foreach ($server in $servers) {
        foreach ($transport in @('tcp-obfuscated','wss-obfuscated')) {
            Invoke-DPIProbeCheckpoint $CheckStop
            $rows.Add((Invoke-TelegramProbe -Root $Root -Dc $server.dc -Address $server.ip -Transport $transport -CheckStop $CheckStop))
        }
        if ($Extended) {
            $rows.Add((Invoke-TelegramProbe -Root $Root -Dc $server.dc -Address $server.ip -Transport 'tcp-abridged' -Port 5222 -CheckStop $CheckStop))
        }
    }
    return $rows.ToArray()
}
