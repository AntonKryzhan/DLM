Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'tools\TelegramProbe.ps1')
function Convert-TestHex([string]$Text) {
    $bytes=New-Object byte[] ($Text.Length/2)
    for ($i=0; $i -lt $bytes.Length; $i++) { $bytes[$i]=[Convert]::ToByte($Text.Substring($i*2,2),16) }
    return ,$bytes
}
function Assert-TestBytes([byte[]]$A, [byte[]]$B, [string]$Name) {
    if ([Convert]::ToBase64String($A) -ne [Convert]::ToBase64String($B)) { throw ('Byte mismatch: ' + $Name) }
}
# NIST SP 800-38A F.5.5, AES-256 CTR.
$key=Convert-TestHex '603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4'
$iv=Convert-TestHex 'f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff'
$plain=Convert-TestHex '6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710'
$cipher=Convert-TestHex '601ec313775789a5b7a7f504bbf3d228f443e3ca4d62b59aca84e990cacaf5c52b0930daa23de94ce87017ba2d84988ddfc9c58db67aada613c2dd08457941a6'
$state=New-TelegramCTR $key $iv
try { Assert-TestBytes (Convert-TelegramCTR $state $plain) $cipher 'NIST AES-256-CTR' } finally { Close-TelegramCTR $state }
$state=New-TelegramCTR $key $iv
try {
    $chunks=[Collections.Generic.List[byte]]::new()
    foreach ($range in @(@(0,0),@(1,3),@(4,18),@(19,63))) { $chunks.AddRange((Convert-TelegramCTR $state ([byte[]]$plain[$range[0]..$range[1]]))) }
    Assert-TestBytes $chunks.ToArray() $cipher 'CTR counter and partial-block continuity'
} finally { Close-TelegramCTR $state }
for ($iteration=0; $iteration -lt 8; $iteration++) {
    $obf=New-TelegramObfuscation
    $decoder=$null; $serverTx=$null
    try {
        $decoder=New-TelegramCTR ([byte[]]$obf.header[8..39]) ([byte[]]$obf.header[40..55])
        $decoded=Convert-TelegramCTR $decoder $obf.header
        Assert-TestBytes ([byte[]]$decoded[56..59]) ([byte[]]@(239,239,239,239)) 'Obfuscated transport marker'
        Assert-TestBytes (Convert-TelegramCTR $decoder (Convert-TelegramCTR $obf.tx $plain)) $plain 'Outgoing offset after 64-byte init'
        $reverse=[byte[]]$obf.header.Clone(); [Array]::Reverse($reverse)
        $serverTx=New-TelegramCTR ([byte[]]$reverse[8..39]) ([byte[]]$reverse[40..55])
        Assert-TestBytes (Convert-TelegramCTR $obf.rx (Convert-TelegramCTR $serverTx $plain)) $plain 'Independent incoming counter starts at zero'
    } finally { Close-TelegramCTR $decoder; Close-TelegramCTR $serverTx; Close-TelegramCTR $obf.tx; Close-TelegramCTR $obf.rx }
}
foreach ($size in @(4,84,512,65536)) {
    $words=[int]($size/4)
    $header=[byte[]]@($words -band 255)
    if ($words -ge 127) { $header=[byte[]]@(127,($words -band 255),(($words -shr 8) -band 255),(($words -shr 16) -band 255)) }
    $payload=New-Object byte[] $size
    for ($i=0; $i -lt $size; $i++) { $payload[$i]=$i % 251 }
    $stream=[IO.MemoryStream]::new([byte[]]($header+$payload))
    try {
        $connection=@{ stream=$stream; ws=$null; rx=$null; watch=[Diagnostics.Stopwatch]::StartNew(); timeout=10000; check={}; pending=[byte[]]@(); offset=0 }
        Assert-TestBytes (Receive-TelegramFrame $connection) $payload 'Abridged stream framing across reads'
    } finally { $stream.Dispose() }
}
foreach ($bytes in @([byte[]]@(0),[byte[]]@(128),[byte[]]@(127,1,0,0),[byte[]]@(127,1,0,4),[byte[]]@(21,0))) {
    $stream=[IO.MemoryStream]::new($bytes); $rejected=$false
    try {
        $connection=@{ stream=$stream; ws=$null; rx=$null; watch=[Diagnostics.Stopwatch]::StartNew(); timeout=1000; check={}; pending=[byte[]]@(); offset=0 }
        try { [void](Receive-TelegramFrame $connection) } catch { $rejected=$true }
        if (-not $rejected) { throw 'Malformed or truncated abridged frame was accepted' }
    } finally { $stream.Dispose() }
}
$cancelled=$false
try { [void](Wait-TelegramTask ([Threading.Tasks.Task]::Delay(1000)) ([Diagnostics.Stopwatch]::StartNew()) 2000 { throw [OperationCanceledException]::new('fixture') }) } catch [OperationCanceledException] { $cancelled=$true }
if (-not $cancelled) { throw 'Probe cancellation was ignored' }
Write-Host 'Telegram helper tests passed: NIST CTR, chunk continuity, obfuscation directions, framing limits and cancellation.'
