#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$SourcePath,
    [Parameter(Mandatory = $true)][string]$SdkPath,
    [string]$CCompiler = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$sourceRoot = (Resolve-Path -LiteralPath $SourcePath).ProviderPath
$sdkRoot = (Resolve-Path -LiteralPath $SdkPath).ProviderPath
$gcc = if ($CCompiler) { (Get-Command $CCompiler -ErrorAction Stop).Source } else { Join-Path $sdkRoot 'toolchain\mingw64\bin\gcc.exe' }
$gnutls = Join-Path $sdkRoot 'bin\libgnutls-30.dll'
foreach ($path in @($gcc, $gnutls)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw ('QUIC build dependency is missing: ' + $path) }
}
$manifest = Get-Content -LiteralPath (Join-Path $sourceRoot 'vendor\DLN_QUIC_SOURCES.json') -Raw | ConvertFrom-Json
foreach ($group in @(@('ngtcp2', 'ngtcp2'), @('gnutls_headers', 'gnutls-headers'))) {
    $entry = $manifest.($group[0])
    foreach ($file in $entry.files.PSObject.Properties) {
        $path = Join-Path (Join-Path $sourceRoot ('vendor\' + $group[1])) $file.Name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.Value) {
            throw ('Modified or missing pinned QUIC source: ' + $path)
        }
    }
}
$vendor = Join-Path $sourceRoot 'vendor\ngtcp2'
$arguments = @('-std=gnu11', '-O2', '-fvisibility=hidden', '-DNGTCP2_STATICLIB')
foreach ($include in @('lib\includes', 'lib', 'crypto\includes', 'crypto')) { $arguments += @('-I', (Join-Path $vendor $include)) }
$arguments += @('-I', (Join-Path $sourceRoot 'vendor\gnutls-headers'))
$arguments += @(Get-ChildItem -LiteralPath (Join-Path $vendor 'lib') -Filter '*.c' -File | Sort-Object Name | ForEach-Object { $_.FullName })
$arguments += @((Join-Path $vendor 'crypto\shared.c'), (Join-Path $vendor 'crypto\gnutls\gnutls.c'), (Join-Path $sourceRoot 'native\quic\provider.c'))
$temporary = Join-Path $sdkRoot ('bin\dlm-quic-build-' + [guid]::NewGuid().ToString('N') + '.dll')
$arguments += @('-shared', $gnutls, '-lws2_32', '-static-libgcc', '-o', $temporary)
try {
    Write-Host 'Building the pinned ngtcp2 1.26.0 general QUIC provider...'
    & $gcc @arguments
    if ($LASTEXITCODE -ne 0) { throw 'General QUIC provider compilation failed.' }
    Move-Item -LiteralPath $temporary -Destination (Join-Path $sdkRoot 'bin\dlm-quic-1.dll') -Force
    $licenses = Join-Path $sdkRoot 'licenses\quic'
    New-Item -ItemType Directory -Path $licenses -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $vendor 'COPYING') -Destination (Join-Path $licenses 'ngtcp2-MIT.txt') -Force
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'vendor\gnutls-headers\COPYING.LESSER') -Destination (Join-Path $licenses 'gnutls-LGPL-2.1.txt') -Force
    Write-Host ('QUIC provider: ' + (Join-Path $sdkRoot 'bin\dlm-quic-1.dll'))
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
}
