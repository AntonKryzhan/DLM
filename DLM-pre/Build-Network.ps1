#requires -Version 5.1
[CmdletBinding()]
param([switch]$SkipCompilerBuild, [ValidateRange(0, 259200)][int]$SoakSeconds = 0)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'DLM-main'
if (-not (Test-Path -LiteralPath (Join-Path $source 'Cargo.toml') -PathType Leaf) -and (Test-Path -LiteralPath (Join-Path $root 'Cargo.toml') -PathType Leaf)) { $source = $root }
$sdk = Join-Path $root 'DLM-OUT'
if (-not (Test-Path -LiteralPath (Join-Path $source 'Cargo.toml') -PathType Leaf)) {
    throw 'Extract this patch over the directory containing DLM-main and DLM-pre.'
}
$examples = @('network_protocol', 'network_loopback', 'network_streaming', 'network_typed', 'network_reactor', 'network_scope', 'network_scope_control', 'network_crypto', 'network_crypto_limits', 'network_quic', 'network_transport_policy', 'network_transport', 'network_transport_quic', 'network_spec', 'network_spec_versions', 'network_spec_session', 'network_codec', 'network_codec_limits', 'network_codec_session')
$modules = @('native_net', 'native_protocol', 'net_bytes', 'net_codec', 'net_error', 'net_socket', 'net_reactor', 'net_async', 'net_crypto', 'net_quic', 'net_transport_policy', 'net_transport', 'net_spec', 'net_spec_wire', 'net_spec_compat', 'net_spec_laws', 'net_codec_runtime', 'net_session_machine', 'net_session', 'net_identity', 'net_content', 'net_discovery', 'net_optimizer', 'net_adaptive', 'net_tun', 'net_session_verified', 'dln_gateway', 'dln_continuity', 'dln_proxy')
$required = @($examples | ForEach-Object { Join-Path $source ('examples\native\' + $_ + '.dlm') })
$required += @($modules | ForEach-Object { Join-Path $source ('lib\std\' + $_ + '.dlm') })
$specProofs = @('repeated', 'zero', 'padding')
$required += @($specProofs | ForEach-Object { Join-Path $source ('examples\native\fixtures\network-spec-' + $_ + '.kproof') })
$hostSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_async_host.c'
$cryptoHostSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_crypto_host.c'
$quicHostSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_quic_host.c'
$transportHostSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_transport_host.c'
$specHostSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_spec_host.c'
$specFixtureSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_spec.dlm'
$codecHostSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_codegen_host.c'
$codecFixtureSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_codegen.dlm'
$codecGenerations = @(@('network_codec_schema', 'codec_echo', 'Echo', '1'), @('network_codec_pos_schema', 'codec_pos', 'Pos', '0'), @('network_codec_le_schema', 'codec_le', 'Little', '0'))
$required += @(Join-Path $source 'examples\native\codec_schema_common.dlm')
foreach ($generation in $codecGenerations) {
    $required += @(Join-Path $source ('examples\native\' + $generation[0] + '.dlm'))
    foreach ($suffix in @('.dlm', '.dlm.dns', '.dlm.codec')) {
        $required += @(Join-Path $source ('examples\native\generated\' + $generation[1] + $suffix))
    }
}
$releaseHostSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_release_host.c'
$required += @($releaseHostSource)
$gatewayHostSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_gateway_host.c'
$gatewayFixtureSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\gateway_continuity_boundary.dlm'
$proxyHostSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_proxy_host.c'
$proxyFixtureSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\gateway_proxy_boundary.dlm'
$required += @($proxyHostSource, $proxyFixtureSource, $gatewayHostSource, $gatewayFixtureSource, (Join-Path $source 'crates\dlm_gateway\include\dlm_gateway.h'))
$adaptiveHostSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_adaptive_host.c'
$tunHostSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_tun_host.c'
$adaptiveAbiHostSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_adaptive_abi_host.c'
$adaptiveFixtureSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_adaptive.dlm'
$required += @($adaptiveHostSource, $tunHostSource, $adaptiveAbiHostSource, $adaptiveFixtureSource, (Join-Path $source 'crates\dlm_core\src\native\adaptive.c'), (Join-Path $source 'crates\dlm_core\src\native\tun.c'), (Join-Path $source 'crates\dlm_core\src\native\edge_headers.h'))
$nodesHostSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_nodes_host.c'
$nodesAbiHostSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_nodes_abi_host.c'
$nodesFixtureSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_nodes.dlm'
$required += @($nodesHostSource, $nodesAbiHostSource, $nodesFixtureSource, (Join-Path $source 'crates\dlm_core\src\native\nodes.c'))
$stateHostSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_state_host.c'
$stateFixtureSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_state.dlm'
$stateSchema = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_state_schema.dlm'
$stateGenerated = Join-Path $source 'crates\dlm_core\tests\native_hosts\generated\state_session.dlm'
$required += @($stateHostSource, $stateFixtureSource, $stateSchema)
foreach ($suffix in @('', '.dns', '.state', '.nproof')) { $required += @($stateGenerated + $suffix) }
$required += @($codecHostSource, $codecFixtureSource)
$required += @($hostSource, $cryptoHostSource, $quicHostSource, $transportHostSource, $specHostSource, $specFixtureSource, (Join-Path $source 'native\quic\provider.c'), (Join-Path $source 'vendor\DLN_QUIC_SOURCES.json'))
if ($SkipCompilerBuild) {
    $required += @($modules | ForEach-Object { Join-Path $sdk ('lib\std\' + $_ + '.dlm') })
}
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
if ($missing.Count) {
    throw ("Incomplete networking files. Apply the complete 0.149 patch over the 0.148 project. Missing:`n" + ($missing -join "`n"))
}
if (-not $SkipCompilerBuild) {
    & (Join-Path $PSScriptRoot 'Build-DLM.ps1') -SourcePath $source -OutputPath $sdk
}
$compiler = Join-Path $sdk 'bin\dlm.exe'
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) { throw 'Build the compiler first.' }
$versionText = (& $compiler '--version' | Out-String).Trim()
$versionMatch = [regex]::Match($versionText, '^dlm ([0-9]+\.[0-9]+\.[0-9]+)$')
if ($LASTEXITCODE -ne 0 -or -not $versionMatch.Success) {
    throw 'Cannot identify the built DLM compiler.'
}
$builtVersion = [version]($versionMatch.Groups[1].Value)
if ($builtVersion -lt [version]'0.149.0') { throw 'Networking requires DLM 0.149.0 or later; rebuild the compiler.' }
$names = @('PATH', 'CC', 'AR', 'DLM_PATH', 'DLM_LIBCURL', 'DLM_LIBGNUTLS', 'DLM_LIBHOGWEED', 'DLM_LIBQUIC')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$transcriptStarted = $false
try {
    $env:CC = Join-Path $sdk 'toolchain\mingw64\bin\gcc.exe'
    if (-not (Test-Path -LiteralPath $env:CC -PathType Leaf)) { throw 'SDK C compiler is missing.' }
    $env:AR = Join-Path (Split-Path -Parent $env:CC) 'ar.exe'
    if (-not (Test-Path -LiteralPath $env:AR -PathType Leaf)) { throw 'SDK static-library archiver is missing; rebuild the complete SDK toolchain.' }
    $env:PATH = (Split-Path -Parent $env:CC) + ';' + $env:PATH
    $env:DLM_PATH = Join-Path $sdk 'lib'
    $curlDll = Join-Path $sdk 'bin\libcurl-4.dll'
    if (-not $env:DLM_LIBCURL -and (Test-Path -LiteralPath $curlDll -PathType Leaf)) { $env:DLM_LIBCURL = $curlDll }
    $validation = Join-Path $sdk 'validation\network'
    New-Item -ItemType Directory -Path $validation -Force | Out-Null
    $logRoot = Join-Path $env:LOCALAPPDATA 'DLM\BuildCache'
    New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
    $logPath = Join-Path $logRoot ('network-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8) + '.log')
    Start-Transcript -Path $logPath -Force | Out-Null
    $transcriptStarted = $true
    Write-Host ('Network validation log: ' + $logPath)
    foreach ($dependency in @(@('DLM_LIBGNUTLS', 'libgnutls-30.dll'), @('DLM_LIBHOGWEED', 'libhogweed-6.dll'))) {
        if (-not [Environment]::GetEnvironmentVariable($dependency[0], 'Process')) {
            $library = Join-Path $sdk ('bin\' + $dependency[1])
            if (-not (Test-Path -LiteralPath $library -PathType Leaf)) { throw ('SDK crypto dependency is missing: ' + $library) }
            [Environment]::SetEnvironmentVariable($dependency[0], $library, 'Process')
        }
    }
    if (-not $env:DLM_LIBQUIC) { $env:DLM_LIBQUIC = Join-Path $sdk 'bin\dlm-quic-1.dll' }
    if (-not (Test-Path -LiteralPath $env:DLM_LIBQUIC -PathType Leaf)) { throw ('SDK QUIC provider is missing: ' + $env:DLM_LIBQUIC + '. Rebuild without -SkipCompilerBuild.') }
    Write-Host ('QUIC provider: ' + $env:DLM_LIBQUIC)
    Write-Host ('Crypto GnuTLS: ' + $env:DLM_LIBGNUTLS)
    Write-Host ('Crypto Hogweed: ' + $env:DLM_LIBHOGWEED)
    $cryptoHostExecutable = Join-Path $validation 'network_crypto_host.exe'
    & $env:CC '-std=gnu11' '-O1' '-Wall' '-Wextra' '-Werror' '-Wno-misleading-indentation' $cryptoHostSource '-lws2_32' '-o' $cryptoHostExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Crypto known-answer host compilation failed.' }
    & $cryptoHostExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Crypto known-answer host checks failed.' }
    $nodesHostExecutable = Join-Path $validation 'network_nodes_host.exe'
    & $env:CC '-std=gnu11' '-O1' '-Wall' '-Wextra' '-Werror' '-Wno-misleading-indentation' '-UNDEBUG' $nodesHostSource '-lws2_32' '-o' $nodesHostExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Identity/discovery/content host compilation failed.' }
    & $nodesHostExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Identity/discovery/content signatures, replay, Merkle or three-peer checks failed.' }
    $nodesFixtureLibrary = Join-Path $validation 'dln_nodes.a'
    & $compiler build $nodesFixtureSource --lib $env:DLM_PATH --allow-unsafe --target staticlib -o $nodesFixtureLibrary
    if ($LASTEXITCODE -ne 0) { throw 'Identity/discovery/content DLM fixture compilation failed.' }
    $nodesAbiExecutable = Join-Path $validation 'network_nodes_abi_host.exe'
    & $env:CC '-std=gnu11' '-O1' '-Wall' '-Wextra' '-Werror' '-UNDEBUG' $nodesAbiHostSource ('-I' + $validation) $nodesFixtureLibrary '-lshell32' '-lws2_32' '-lgdi32' '-luser32' '-o' $nodesAbiExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Identity/discovery/content DLM ABI host compilation failed.' }
    & $nodesAbiExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Identity/discovery/content DLM ownership or ABI checks failed.' }
    foreach ($component in @(@($adaptiveHostSource, 'network_adaptive_host.exe'), @($tunHostSource, 'network_tun_host.exe'))) {
        $componentExecutable = Join-Path $validation $component[1]
        & $env:CC '-std=gnu11' '-O1' '-Wall' '-Wextra' '-Werror' '-Wno-misleading-indentation' '-UNDEBUG' $component[0] '-lws2_32' '-o' $componentExecutable
        if ($LASTEXITCODE -ne 0) { throw ('Adaptive/TUN host compilation failed: ' + $component[1]) }
        if ($component[1] -ceq 'network_adaptive_host.exe') { & $componentExecutable (Join-Path $source 'examples\native\fixtures') } else { & $componentExecutable }
        if ($LASTEXITCODE -ne 0) { throw ('Adaptive/TUN host checks failed: ' + $component[1]) }
    }
    $adaptiveFixtureLibrary = Join-Path $validation 'dln_adaptive.a'
    & $compiler build $adaptiveFixtureSource --lib $env:DLM_PATH --allow-unsafe --target staticlib -o $adaptiveFixtureLibrary
    if ($LASTEXITCODE -ne 0) { throw 'Adaptive/verified-session ABI fixture compilation failed.' }
    $adaptiveAbiExecutable = Join-Path $validation 'network_adaptive_abi_host.exe'
    & $env:CC '-std=gnu11' '-O1' '-Wall' '-Wextra' '-Werror' '-UNDEBUG' $adaptiveAbiHostSource ('-I' + $validation) $adaptiveFixtureLibrary '-lshell32' '-lws2_32' '-lgdi32' '-luser32' '-o' $adaptiveAbiExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Adaptive/verified-session ABI host compilation failed.' }
    $optimizerCertificate = Join-Path $validation 'optimizer.nopt'
    & $adaptiveAbiExecutable $optimizerCertificate
    if ($LASTEXITCODE -ne 0) { throw 'Adaptive/verified-session/optimizer/TUN authority checks failed.' }
    $transportChecker = Join-Path $sdk 'bin\dlm-transport-check.exe'
    if (-not (Test-Path -LiteralPath $transportChecker -PathType Leaf)) { throw 'Rebuild the SDK with dlm-transport-check.' }
    & $transportChecker $optimizerCertificate
    if ($LASTEXITCODE -ne 0) { throw 'Independent optimizer certificate checking failed.' }
    foreach ($delivery in @('1', '2')) {
        & $transportChecker '--model' '4' '4' $delivery
        if ($LASTEXITCODE -ne 0) { throw 'Independent finite migration model checking failed.' }
    }
    $wintun = Join-Path $sdk 'bin\wintun.dll'
    $wintunManifest = Get-Content -LiteralPath (Join-Path $source 'vendor\DLN_WINTUN_SOURCES.json') -Raw | ConvertFrom-Json
    if (-not (Test-Path -LiteralPath $wintun -PathType Leaf)) { throw 'Pinned Wintun runtime is missing; rebuild the SDK.' }
    if ((Get-FileHash -LiteralPath $wintun -Algorithm SHA256).Hash -ine $wintunManifest.dll_sha256) { throw 'Wintun runtime SHA256 mismatch.' }
    Write-Host 'TUN/TAP packet and ownership checks passed; privileged native driver acceptance remains separate.'
    foreach ($generation in $codecGenerations) {
        $schema = Join-Path $source ('examples\native\' + $generation[0] + '.dlm')
        $generated = Join-Path $source ('examples\native\generated\' + $generation[1] + '.dlm')
        & $compiler network-codegen $schema --lib $env:DLM_PATH --module ('generated.' + $generation[1]) --prefix $generation[2] --minor $generation[3] -o $generated --check
        if ($LASTEXITCODE -ne 0) { throw ('Generated codec/source/descriptor plan differs: ' + $generation[1]) }
    }
    foreach ($example in $examples) {
        $program = Join-Path $source ('examples\native\' + $example + '.dlm')
        $executable = Join-Path $validation ($example + '.exe')
        & $compiler build $program --lib $env:DLM_PATH --allow-unsafe -o $executable
        if ($LASTEXITCODE -ne 0) { throw ('Compilation failed: ' + $example) }
        Push-Location -LiteralPath $source
        try { & $executable } finally { Pop-Location }
        if ($LASTEXITCODE -ne 0) { throw ('Loopback check failed: ' + $example) }
    }
    $hostExecutable = Join-Path $validation 'network_scope_host.exe'
    & $env:CC '-std=gnu11' '-O1' '-Wall' '-Wextra' '-Werror' '-Wno-misleading-indentation' $hostSource '-lws2_32' '-o' $hostExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Scoped thread/lifecycle host compilation failed.' }
    & $hostExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Scoped thread/lifecycle host checks failed.' }
    $quicHostExecutable = Join-Path $validation 'network_quic_host.exe'
    & $env:CC '-std=gnu11' '-O1' '-Wall' '-Wextra' '-Werror' '-Wno-misleading-indentation' $quicHostSource '-lws2_32' '-o' $quicHostExecutable
    if ($LASTEXITCODE -ne 0) { throw 'QUIC lifecycle host compilation failed.' }
    $fixtures = Join-Path $source 'examples\native\fixtures'
    & $quicHostExecutable (Join-Path $fixtures 'quic-test-ca.pem') (Join-Path $fixtures 'quic-test-server.pem') (Join-Path $fixtures 'quic-test-key.pem')
    if ($LASTEXITCODE -ne 0) { throw 'QUIC lifecycle host checks failed.' }
    $transportHostExecutable = Join-Path $validation 'network_transport_host.exe'
    & $env:CC '-std=gnu11' '-O1' '-Wall' '-Wextra' '-Werror' '-Wno-misleading-indentation' $transportHostSource '-lws2_32' '-o' $transportHostExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Transport fault/lifecycle host compilation failed.' }
    & $transportHostExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Transport fault/lifecycle host checks failed.' }
    $specFixtureLibrary = Join-Path $validation 'dln_spec.a'
    & $compiler build $specFixtureSource --lib $env:DLM_PATH --allow-unsafe --target staticlib -o $specFixtureLibrary
    if ($LASTEXITCODE -ne 0) { throw 'NetworkSpec ABI fixture compilation failed.' }
    $specHostExecutable = Join-Path $validation 'network_spec_host.exe'
    & $env:CC '-std=gnu11' '-O1' '-Wall' '-Wextra' '-Werror' $specHostSource ('-I' + $validation) $specFixtureLibrary '-lshell32' '-lws2_32' '-lgdi32' '-luser32' '-o' $specHostExecutable
    if ($LASTEXITCODE -ne 0) { throw 'NetworkSpec ABI host compilation failed.' }
    & $specHostExecutable
    if ($LASTEXITCODE -ne 0) { throw 'NetworkSpec descriptor and proof-gate checks failed.' }
    $codecFixtureLibrary = Join-Path $validation 'dln_codegen.a'
    & $compiler build $codecFixtureSource --lib $env:DLM_PATH --lib (Join-Path $source 'examples\native') --allow-unsafe --target staticlib -o $codecFixtureLibrary
    if ($LASTEXITCODE -ne 0) { throw 'Generated codec ABI fixture compilation failed.' }
    $codecHostExecutable = Join-Path $validation 'network_codec_host.exe'
    & $env:CC '-std=gnu11' '-O1' '-Wall' '-Wextra' '-Werror' '-UNDEBUG' $codecHostSource ('-I' + $validation) $codecFixtureLibrary '-lshell32' '-lws2_32' '-lgdi32' '-luser32' '-o' $codecHostExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Generated codec ABI host compilation failed.' }
    & $codecHostExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Generated codec vectors/bounds/cleanup checks failed.' }
    & $compiler network-protocol $stateSchema --lib $env:DLM_PATH --module 'generated.state_session' --prefix 'State' -o $stateGenerated --check
    if ($LASTEXITCODE -ne 0) { throw 'Protocol machine/source/certificate bundle differs.' }
    $networkChecker = Join-Path $sdk 'bin\dlm-network-check.exe'
    if (-not (Test-Path -LiteralPath $networkChecker -PathType Leaf)) { throw 'Rebuild the SDK with the independent network proof checker.' }
    & $networkChecker ($stateGenerated + '.nproof') --lib $env:DLM_PATH
    if ($LASTEXITCODE -ne 0) { throw 'Independent protocol model certificate checking failed.' }
    $stateFixtureLibrary = Join-Path $validation 'dln_state.a'
    & $compiler build $stateFixtureSource --lib $env:DLM_PATH --allow-unsafe --target staticlib -o $stateFixtureLibrary
    if ($LASTEXITCODE -ne 0) { throw 'Protocol session ABI fixture compilation failed.' }
    $stateHostExecutable = Join-Path $validation 'network_state_host.exe'
    & $env:CC '-std=gnu11' '-O1' '-Wall' '-Wextra' '-Werror' '-UNDEBUG' $stateHostSource ('-I' + $validation) $stateFixtureLibrary '-lshell32' '-lws2_32' '-lgdi32' '-luser32' '-o' $stateHostExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Protocol session ABI host compilation failed.' }
    Push-Location -LiteralPath $source
    try { & $stateHostExecutable '--quic' } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw 'Protocol session state/limit/cleanup checks failed.' }
    $lawCheck = (& $compiler check (Join-Path $source 'lib\std\net_spec_laws.dlm') --lib $env:DLM_PATH --trusted-only | Out-String)
    if ($LASTEXITCODE -ne 0) { Write-Host $lawCheck; throw 'NetworkSpec arithmetic law checking failed.' }
    Write-Host 'NetworkSpec six arithmetic laws checked'
    foreach ($proof in $specProofs) {
        & $compiler proof-check (Join-Path $source ('examples\native\fixtures\network-spec-' + $proof + '.kproof'))
        if ($LASTEXITCODE -ne 0) { throw ('NetworkSpec independent kernel check failed: ' + $proof) }
    }
    $gatewayCli = Join-Path $sdk 'bin\dlm-net.exe'
    $gatewayLibraryDirectory = Join-Path $sdk 'bin'
    foreach ($name in @('dlm-net.exe', 'dlm_gateway.dll', 'libdlm_gateway.dll.a')) {
        if (-not (Test-Path -LiteralPath (Join-Path $gatewayLibraryDirectory $name) -PathType Leaf)) { throw ('SDK gateway output is missing: ' + $name + '. Rebuild without -SkipCompilerBuild.') }
    }
    $gatewayFixture = Join-Path $validation 'gateway_boundary.a'
    & $compiler build $gatewayFixtureSource --lib $env:DLM_PATH --allow-unsafe --target staticlib -o $gatewayFixture
    if ($LASTEXITCODE -ne 0) { throw 'Gateway DLM boundary compilation failed.' }
    $gatewayHost = Join-Path $validation 'network_gateway_host.exe'
    & $env:CC '-std=gnu11' '-O1' '-Wall' '-Wextra' '-Werror' '-UNDEBUG' '-DDLN_CONTINUITY_ACCEPTANCE' $gatewayHostSource $gatewayFixture ('-L' + $gatewayLibraryDirectory) '-ldlm_gateway' '-lshell32' '-lws2_32' '-lgdi32' '-luser32' '-o' $gatewayHost
    if ($LASTEXITCODE -ne 0) { throw 'Gateway DLM/C ABI host compilation failed.' }
    $gatewayKeys = Join-Path $validation ('gateway-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $gatewayKeys | Out-Null
    $serverIdentityPath = Join-Path $gatewayKeys 'server.identity'
    $clientIdentityPath = Join-Path $gatewayKeys 'client.identity'
    $serverPublic = (& $gatewayCli keygen --identity $serverIdentityPath | ConvertFrom-Json)
    if ($LASTEXITCODE -ne 0) { throw 'Gateway server key generation failed.' }
    $clientPublic = (& $gatewayCli keygen --identity $clientIdentityPath | ConvertFrom-Json)
    if ($LASTEXITCODE -ne 0) { throw 'Gateway client key generation failed.' }
    $gatewayServerConfig = Join-Path $gatewayKeys 'server.cfg'
    $gatewayClientConfig = Join-Path $gatewayKeys 'client.cfg'
    $gatewaySpoolKey = Join-Path $gatewayKeys 'spool.key'
    & $gatewayCli journal-keygen --output $gatewaySpoolKey
    if ($LASTEXITCODE -ne 0) { throw 'Gateway journal key generation failed.' }
    $gatewayResults = @()
    foreach ($carrier in @('tls', 'quic-stream', 'quic-datagram', 'websocket', 'https-connect')) {
        $serverText = @(
            'listen=127.0.0.1:0', ('transport=' + $carrier), ('identity=' + $serverIdentityPath),
            ('certificate=' + (Join-Path $source 'examples\native\fixtures\quic-test-server.pem')),
            ('key=' + (Join-Path $source 'examples\native\fixtures\quic-test-key.pem')),
            ('allow_client=' + $clientPublic.root), 'allow_target=tcp,127.0.0.0/8,__ECHO_PORT__', 'allow_private=true',
            ('spool=' + (Join-Path $gatewayKeys ('inbox-' + $carrier))), ('spool_key=' + $gatewaySpoolKey)
        ) -join "`n"
        $clientText = @(
            'gateway=__GATEWAY__', 'hostname=localhost', ('transport=' + $carrier), ('identity=' + $clientIdentityPath),
            ('ca=' + (Join-Path $source 'examples\native\fixtures\quic-test-ca.pem')), ('peer=' + $serverPublic.root)
        ) -join "`n"
        [IO.File]::WriteAllText($gatewayServerConfig, $serverText, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($gatewayClientConfig, $clientText, [Text.UTF8Encoding]::new($false))
        $gatewayOutput = @(& $gatewayHost $gatewayServerConfig $gatewayClientConfig)
        if ($LASTEXITCODE -ne 0 -or $gatewayOutput.Count -ne 1) { throw ('Gateway runtime acceptance failed: ' + $carrier) }
        $gatewayResult = $gatewayOutput[0] | ConvertFrom-Json
        if ($gatewayResult.status -cne 'PASS') { throw ('Gateway acceptance result failed: ' + $carrier) }
        $gatewayResults += [ordered]@{ carrier = $carrier; result = $gatewayResult }
        Write-Host ($carrier + ': ' + $gatewayOutput[0])
    }
    $proxyFixture = Join-Path $validation 'proxy_boundary.a'
    & $compiler build $proxyFixtureSource --lib $env:DLM_PATH --allow-unsafe --target staticlib -o $proxyFixture
    if ($LASTEXITCODE -ne 0) { throw 'Proxy DLM boundary compilation failed.' }
    $proxyHost = Join-Path $validation 'network_proxy_host.exe'
    & $env:CC '-std=gnu11' '-O1' '-Wall' '-Wextra' '-Werror' '-UNDEBUG' $proxyHostSource $proxyFixture ('-L' + $gatewayLibraryDirectory) '-ldlm_gateway' '-lshell32' '-lws2_32' '-lgdi32' '-luser32' '-o' $proxyHost
    if ($LASTEXITCODE -ne 0) { throw 'Proxy DLM/C ABI host compilation failed.' }
    $proxyToken = Join-Path $gatewayKeys 'proxy.key'
    & $gatewayCli proxy-keygen --output $proxyToken
    if ($LASTEXITCODE -ne 0) { throw 'Proxy credential generation failed.' }
    $proxyServerConfig = Join-Path $gatewayKeys 'proxy-server.cfg'
    $proxyClientConfig = Join-Path $gatewayKeys 'proxy-client.cfg'
    $proxyServerText = @(
        'listen=127.0.0.1:0', ('token=' + $proxyToken), 'allow_private=true', 'allow_target=udp,127.0.0.0/8,__ECHO_PORT__',
        ('certificate=' + (Join-Path $source 'examples\native\fixtures\quic-test-server.pem')),
        ('key=' + (Join-Path $source 'examples\native\fixtures\quic-test-key.pem'))
    ) -join "`n"
    $proxyClientText = @(
        'proxy_address=__PROXY__', 'proxy_hostname=localhost', ('proxy_token=' + $proxyToken),
        ('proxy_ca=' + (Join-Path $source 'examples\native\fixtures\quic-test-ca.pem'))
    ) -join "`n"
    [IO.File]::WriteAllText($proxyServerConfig, $proxyServerText, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($proxyClientConfig, $proxyClientText, [Text.UTF8Encoding]::new($false))
    $proxyOutput = @(& $proxyHost $proxyServerConfig $proxyClientConfig)
    if ($LASTEXITCODE -ne 0 -or $proxyOutput.Count -ne 1) { throw 'Proxy runtime acceptance failed.' }
    $proxyResult = $proxyOutput[0] | ConvertFrom-Json
    if ($proxyResult.status -cne 'PASS') { throw 'Proxy acceptance result failed.' }
    Write-Host ('HTTP/1.1 MASQUE: ' + $proxyOutput[0])
    $capabilityOutput = @(& $gatewayCli capabilities)
    if ($LASTEXITCODE -ne 0 -or $capabilityOutput.Count -ne 1) { throw 'Carrier capability query failed.' }
    $carrierCapabilities = $capabilityOutput[0] | ConvertFrom-Json
    $continuityModelPath = Join-Path $gatewayKeys 'continuity-model.json'
    & $gatewayCli continuity-model --output $continuityModelPath
    if ($LASTEXITCODE -ne 0) { throw 'Continuity ownership and durable ACK model validation failed.' }
    $continuityModel = [IO.File]::ReadAllText($continuityModelPath) | ConvertFrom-Json
    if ($continuityModel.status -cne 'PASS' -or $continuityModel.false_committed_acks -ne 0) { throw 'Continuity model reported a false committed acknowledgement.' }
    $releaseExecutable = Join-Path $validation 'network_release_host.exe'
    & $env:CC '-std=gnu11' '-O1' '-g' '-Wall' '-Wextra' '-Werror' '-Wno-misleading-indentation' '-UNDEBUG' $releaseHostSource '-lws2_32' '-o' $releaseExecutable
    if ($LASTEXITCODE -ne 0) { throw 'DLN release impairment host compilation failed.' }
    $releaseCases = @(
        @{ Name = 'delayed_probes'; Arguments = @('probes') },
        @{ Name = 'silent_failover'; Arguments = @('blackhole') },
        @{ Name = 'lost_authentication'; Arguments = @('auth-loss') },
        @{ Name = 'hostile_input'; Arguments = @('fuzz') }
    )
    foreach ($loss in @(0, 1, 5, 10)) {
        foreach ($delivery in @(1, 2)) {
            $releaseCases += @{ Name = ('transfer_' + $loss + '_' + $delivery); Arguments = @('transfer', [string](14600 + $loss * 10 + $delivery), [string]$loss, [string]$delivery, '320') }
        }
    }
    $releaseResults = @()
    foreach ($releaseCase in $releaseCases) {
        $caseArguments = $releaseCase.Arguments
        $caseOutput = @(& $releaseExecutable @caseArguments)
        if ($LASTEXITCODE -ne 0 -or $caseOutput.Count -eq 0) { throw ('DLN release case failed: ' + $releaseCase.Name) }
        foreach ($line in $caseOutput) {
            Write-Host $line
            $result = $line | ConvertFrom-Json
            if ($result.status -cne 'PASS') { throw ('DLN release result failed: ' + $releaseCase.Name) }
            $releaseResults += $result
        }
    }
    $soak = [ordered]@{ status = 'NOT_RUN'; requested_seconds = $SoakSeconds; cycles = 0; elapsed_seconds = 0 }
    if ($SoakSeconds -gt 0) {
        $soakOutput = @(& $releaseExecutable 'soak' ([string]$SoakSeconds))
        if ($LASTEXITCODE -ne 0 -or $soakOutput.Count -ne 1) { throw 'DLN reconnect/ownership soak failed.' }
        $soakResult = $soakOutput[0] | ConvertFrom-Json
        if ($soakResult.status -cne 'PASS' -or -not $soakResult.single_process) { throw 'DLN reconnect/ownership soak result failed.' }
        $soak.status = 'PASS'
        $soak.cycles = $soakResult.cycles
        $soak.elapsed_seconds = $soakResult.elapsed_ms / 1000.0
        Write-Host ('DLN reconnect/ownership soak passed: ' + $soak.cycles + ' cycles')
    }
    $sourceHashes = [ordered]@{}
    foreach ($directory in @('lib', 'crates', 'native')) {
        foreach ($file in @(Get-ChildItem -LiteralPath (Join-Path $source $directory) -File -Recurse | Where-Object { $_.Extension -in @('.rs', '.c', '.h', '.dlm', '.toml') } | Sort-Object FullName)) {
            $relative = $file.FullName.Substring($source.Length + 1).Replace('\', '/')
            $sourceHashes[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    foreach ($relative in @('Cargo.toml', 'Cargo.lock')) {
        $sourceHashes[$relative] = (Get-FileHash -LiteralPath (Join-Path $source $relative) -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $releaseReport = [ordered]@{
        format = 'DLM-DLN-ACCEPTANCE-v1'; scope = 'local native Windows engineering release gate'; status = 'PASS'
        version = $versionText; finished_utc = [DateTime]::UtcNow.ToString('o'); platform = [Environment]::OSVersion.VersionString
        compiler_sha256 = (Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash.ToLowerInvariant()
        gateway_sha256 = (Get-FileHash -LiteralPath $gatewayCli -Algorithm SHA256).Hash.ToLowerInvariant()
        gateway_library_sha256 = (Get-FileHash -LiteralPath (Join-Path $gatewayLibraryDirectory 'dlm_gateway.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
        quic_sha256 = (Get-FileHash -LiteralPath $env:DLM_LIBQUIC -Algorithm SHA256).Hash.ToLowerInvariant()
        host_sha256 = (Get-FileHash -LiteralPath $releaseExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
        source_files = $sourceHashes; results = $releaseResults; gateway_results = $gatewayResults; proxy_result = $proxyResult; carrier_capabilities = $carrierCapabilities; continuity_model = $continuityModel; soak = $soak; transcript = $logPath
        external_gates = [ordered]@{ privileged_tun_driver = 'NOT_RUN'; public_internet_two_hosts = 'NOT_RUN'; other_native_operating_systems = 'NOT_RUN'; independent_python_interoperability = 'NOT_RUN' }
        formal_scope = 'finite model certificates only; native and cryptographic proofs pending'
    }
    $reportPath = Join-Path $validation ('release-report-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8) + '.json')
    [IO.File]::WriteAllText($reportPath, ($releaseReport | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
    Write-Host ('DLN acceptance report: ' + $reportPath)
    Write-Host ('Networking build and loopback checks passed: ' + $versionText)
    Write-Host 'Protocol state model certificate checked; native machine, codec and cryptographic proof obligations remain pending.'
} finally {
    if ($transcriptStarted) { Stop-Transcript | Out-Null }
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
