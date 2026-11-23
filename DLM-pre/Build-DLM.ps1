#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$SourcePath = 'M:\users\Ark\Desktop\DLM\DLM-main',
    [string]$OutputPath = 'M:\users\Ark\Desktop\DLM\DLM-OUT',
    [string]$CacheRoot = '',
    [string]$RustToolchain = 'stable-x86_64-pc-windows-gnu',
    [switch]$RunTests
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Set-StrictMode -Version 2.0

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or
    -not [Environment]::Is64BitOperatingSystem) {
    throw 'Run this script on 64-bit Windows.'
}

# A fixed, portable WinLibs distribution; its digest is published by the release API.
$gccBundleName = 'winlibs-x86_64-posix-seh-gcc-15.2.0-mingw-w64msvcrt-13.0.0-r2.zip'
$gccBundleUrl = 'https://github.com/brechtsanders/winlibs_mingw/releases/download/15.2.0posix-13.0.0-msvcrt-r2/' + $gccBundleName
$gccBundleHash = '2cdfb0a28c905f2913453dc0be62ff148bf828414f2468c68c62811319275633'
$targetTriple = 'x86_64-pc-windows-gnu'
$stagePath = $null
$transcriptStarted = $false
$savedSecurityProtocol = [Net.ServicePointManager]::SecurityProtocol
$savedEnvironment = @{}
$environmentNames = @(
    'PATH', 'CC', 'DLM_PATH', 'CARGO_HOME', 'RUSTUP_HOME', 'CARGO_TARGET_DIR',
    'CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER', 'RUSTFLAGS', 'CARGO_ENCODED_RUSTFLAGS',
    'CARGO_BUILD_JOBS', 'RUSTUP_INIT_SKIP_PATH_CHECK'
)
foreach ($name in $environmentNames) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

function Invoke-Native {
    param([string]$FilePath, [string[]]$Arguments)
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw ('Command failed (exit {0}): {1} {2}' -f $LASTEXITCODE, $FilePath, ($Arguments -join ' '))
    }
}

function Write-Utf8 {
    param([string]$Path, [string]$Text)
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Copy-Tree {
    param([string]$From, [string]$To)
    New-Item -ItemType Directory -Path $To -Force | Out-Null
    Get-ChildItem -LiteralPath $From -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $To -Recurse -Force
    }
}

function Test-Within {
    param([string]$Child, [string]$Parent)
    return $Child.Equals($Parent, [StringComparison]::OrdinalIgnoreCase) -or
        $Child.StartsWith($Parent.TrimEnd([char[]]'\/') + '\', [StringComparison]::OrdinalIgnoreCase)
}

function Get-Download {
    param([string]$Url, [string]$Path, [string]$Sha256)
    if ($Sha256 -notmatch '^[a-fA-F0-9]{64}$') { throw "Invalid expected SHA256: $Url" }
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ieq $Sha256) { return }
    }
    $partialPath = $Path + '.partial-' + [guid]::NewGuid().ToString('N')
    try {
        Write-Host ('Downloading: ' + [IO.Path]::GetFileName($Path))
        Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $partialPath
        if ((Get-FileHash -LiteralPath $partialPath -Algorithm SHA256).Hash -ine $Sha256) {
            throw ('Download SHA256 mismatch: ' + $Url)
        }
        Move-Item -LiteralPath $partialPath -Destination $Path -Force
    } finally {
        if (Test-Path -LiteralPath $partialPath) { Remove-Item -LiteralPath $partialPath -Force }
    }
}

function Resolve-SourceRoot {
    param([string]$InputPath, [string]$BuildCache)
    if (-not (Test-Path -LiteralPath $InputPath)) {
        if (Test-Path -LiteralPath ($InputPath + '.zip') -PathType Leaf) { $InputPath += '.zip' }
        else { throw ('Source folder or ZIP not found: ' + $InputPath) }
    }
    $sourceItem = Get-Item -LiteralPath $InputPath
    if (-not $sourceItem.PSIsContainer) {
        if ($sourceItem.Extension -ine '.zip') { throw 'The source archive must be a ZIP file.' }
        $digest = (Get-FileHash -LiteralPath $sourceItem.FullName -Algorithm SHA256).Hash
        $unpacked = Join-Path $BuildCache ('sources-' + $digest.Substring(0, 16))
        $complete = Join-Path $unpacked '.extracted'
        if (-not (Test-Path -LiteralPath $complete -PathType Leaf)) {
            New-Item -ItemType Directory -Path $unpacked -Force | Out-Null
            Expand-Archive -LiteralPath $sourceItem.FullName -DestinationPath $unpacked -Force
            Write-Utf8 $complete $digest
        }
        $InputPath = $unpacked
    } else { $InputPath = $sourceItem.FullName }
    $candidates = @($InputPath) + @(Get-ChildItem -LiteralPath $InputPath -Directory | ForEach-Object { $_.FullName })
    $roots = @($candidates | Where-Object {
        (Test-Path -LiteralPath (Join-Path $_ 'Cargo.toml') -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $_ 'crates\dlm_cli\Cargo.toml') -PathType Leaf)
    })
    if ($roots.Count -ne 1) { throw ('Expected exactly one DLM workspace in: ' + $InputPath) }
    foreach ($relative in @('Cargo.lock', 'lib\std\collections.dlm')) {
        if (-not (Test-Path -LiteralPath (Join-Path $roots[0] $relative) -PathType Leaf)) {
            throw ('Incomplete source tree: missing ' + $relative)
        }
    }
    return [IO.Path]::GetFullPath($roots[0]).TrimEnd([char[]]'\/')
}

function Test-HelloExecutable {
    param([string]$ExePath, [string]$WorkingDirectory)
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $ExePath
    $start.WorkingDirectory = $WorkingDirectory
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($start)
    try {
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(30000)) {
            $process.Kill()
            $process.WaitForExit()
            throw 'The compiled Hello application timed out.'
        }
        $stdout = $stdoutTask.GetAwaiter().GetResult().Replace("`r`n", "`n")
        $stderr = $stderrTask.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0 -or $stdout -cne "DLM standalone SDK is ready`n3`n") {
            throw ('Hello application failed: exit={0}; stdout={1}; stderr={2}' -f $process.ExitCode, $stdout, $stderr)
        }
    } finally { $process.Dispose() }
}

try {
    if ([string]::IsNullOrWhiteSpace($CacheRoot)) { $CacheRoot = Join-Path $env:LOCALAPPDATA 'DLM\BuildCache' }
    $CacheRoot = [IO.Path]::GetFullPath($CacheRoot).TrimEnd([char[]]'\/')
    $OutputPath = [IO.Path]::GetFullPath($OutputPath).TrimEnd([char[]]'\/')
    if ($OutputPath -ieq [IO.Path]::GetPathRoot($OutputPath).TrimEnd([char[]]'\/')) {
        throw 'The output must be a folder, not a drive root.'
    }
    if ((Test-Within $CacheRoot $OutputPath) -or (Test-Within $OutputPath $CacheRoot)) {
        throw 'The build cache and output folder must not contain each other.'
    }
    New-Item -ItemType Directory -Path $CacheRoot -Force | Out-Null
    $logPath = Join-Path $CacheRoot ('build-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '.log')
    Start-Transcript -Path $logPath -Force | Out-Null
    $transcriptStarted = $true
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    if (-not $RustToolchain.EndsWith('-x86_64-pc-windows-gnu')) { throw 'RustToolchain must target x86_64-pc-windows-gnu.' }
    $sourceRoot = Resolve-SourceRoot $SourcePath $CacheRoot
    if (Test-Within $sourceRoot $OutputPath) { throw 'The output must not replace the source folder or one of its parents.' }
    if ((Test-Path -LiteralPath $OutputPath) -and -not (Test-Path -LiteralPath $OutputPath -PathType Container)) {
        throw ('The output path is already a file: ' + $OutputPath)
    }
    $outputParent = Split-Path -Parent $OutputPath
    New-Item -ItemType Directory -Path $outputParent -Force | Out-Null
    $stagePath = Join-Path $outputParent ((Split-Path -Leaf $OutputPath) + '.build-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stagePath | Out-Null
    Write-Host ('Source: ' + $sourceRoot)
    Write-Host ('Output: ' + $OutputPath)
    Write-Host ('Log:    ' + $logPath)

    $gccPackage = Join-Path $CacheRoot 'winlibs-15.2.0-msvcrt'
    $gcc = Join-Path $gccPackage 'mingw64\bin\gcc.exe'
    $gccComplete = Join-Path $gccPackage '.complete'
    if (-not ((Test-Path -LiteralPath $gcc -PathType Leaf) -and (Test-Path -LiteralPath $gccComplete -PathType Leaf))) {
        $gccZip = Join-Path $CacheRoot $gccBundleName
        Get-Download $gccBundleUrl $gccZip $gccBundleHash
        New-Item -ItemType Directory -Path $gccPackage -Force | Out-Null
        Write-Host 'Extracting the portable C compiler...'
        Expand-Archive -LiteralPath $gccZip -DestinationPath $gccPackage -Force
        if (-not (Test-Path -LiteralPath $gcc -PathType Leaf)) { throw 'The WinLibs archive does not contain mingw64\bin\gcc.exe.' }
        Write-Utf8 $gccComplete $gccBundleHash
    }
    $env:PATH = (Split-Path -Parent $gcc) + ';' + $savedEnvironment['PATH']
    $gccTarget = (& $gcc '-dumpmachine' | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $gccTarget -notmatch '^x86_64-.*(mingw|windows)') { throw ('Unexpected C compiler target: ' + $gccTarget) }
    $gccVersion = (& $gcc '-dumpfullversion' | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot query the C compiler version.' }
    $env:CC = $gcc
    $env:CARGO_HOME = Join-Path $CacheRoot 'cargo'
    $env:RUSTUP_HOME = Join-Path $CacheRoot 'rustup'
    $env:CARGO_TARGET_DIR = Join-Path $CacheRoot 'target'
    $env:CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER = $gcc
    $env:CARGO_BUILD_JOBS = '2'
    $env:RUSTFLAGS = '-C target-feature=+crt-static'
    Remove-Item Env:CARGO_ENCODED_RUSTFLAGS -ErrorAction SilentlyContinue
    $env:RUSTUP_INIT_SKIP_PATH_CHECK = 'yes'
    $rustup = Join-Path $env:CARGO_HOME 'bin\rustup.exe'
    if (-not (Test-Path -LiteralPath $rustup -PathType Leaf)) {
        $rustupUrl = 'https://static.rust-lang.org/rustup/dist/x86_64-pc-windows-msvc/rustup-init.exe'
        $rustupShaFile = Join-Path $CacheRoot 'rustup-init.exe.sha256'
        Invoke-WebRequest -UseBasicParsing -Uri ($rustupUrl + '.sha256') -OutFile $rustupShaFile
        $rustupSha = ([IO.File]::ReadAllText($rustupShaFile).Trim() -split '\s+')[0]
        $rustupInstaller = Join-Path $CacheRoot 'rustup-init.exe'
        Get-Download $rustupUrl $rustupInstaller $rustupSha
        Invoke-Native $rustupInstaller @('-y', '--profile', 'minimal', '--default-host', $targetTriple, '--default-toolchain', 'none', '--no-modify-path')
    }
    $env:PATH = (Join-Path $env:CARGO_HOME 'bin') + ';' + $env:PATH
    $cachedRustc = Join-Path $env:RUSTUP_HOME ('toolchains\' + $RustToolchain + '\bin\rustc.exe')
    if (-not (Test-Path -LiteralPath $cachedRustc -PathType Leaf)) {
        Invoke-Native $rustup @('toolchain', 'install', $RustToolchain, '--profile', 'minimal')
    }
    $rustVersion = (& $rustup 'run' $RustToolchain 'rustc' '--version' | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'The private Rust toolchain is unavailable.' }
    Write-Host $rustVersion
    Write-Host ('Building DLM for ' + $targetTriple + '...')
    $manifest = Join-Path $sourceRoot 'Cargo.toml'
    if ($RunTests) {
        Invoke-Native $rustup @('run', $RustToolchain, 'cargo', 'test', '--manifest-path', $manifest, '--workspace', '--all-targets', '--locked', '--offline', '--target', $targetTriple)
    }
    Invoke-Native $rustup @('run', $RustToolchain, 'cargo', 'build', '--manifest-path', $manifest, '--release', '--locked', '--offline', '--target', $targetTriple, '-p', 'dlm_cli', '-p', 'dlm_network_proof', '-p', 'dlm_transport_proof', '-p', 'dlm_gateway', '-p', 'dlm_channel_lab')
    $builtExe = Join-Path $env:CARGO_TARGET_DIR ($targetTriple + '\release\dlm.exe')
    if (-not (Test-Path -LiteralPath $builtExe -PathType Leaf)) { throw 'Cargo did not produce dlm.exe.' }

    $builtNetworkChecker = Join-Path $env:CARGO_TARGET_DIR ($targetTriple + '\release\dlm-network-check.exe')
    if (-not (Test-Path -LiteralPath $builtNetworkChecker -PathType Leaf)) { throw 'Cargo did not produce the independent network checker.' }

    $builtTransportChecker = Join-Path $env:CARGO_TARGET_DIR ($targetTriple + '\release\dlm-transport-check.exe')
    if (-not (Test-Path -LiteralPath $builtTransportChecker -PathType Leaf)) { throw 'Cargo did not produce the independent transport checker.' }

    Copy-Tree (Join-Path $PSScriptRoot 'sdk') $stagePath
    $sdkBin = Join-Path $stagePath 'bin'
    New-Item -ItemType Directory -Path $sdkBin | Out-Null
    Copy-Item -LiteralPath $builtExe -Destination (Join-Path $sdkBin 'dlm.exe')
    Copy-Item -LiteralPath $builtNetworkChecker -Destination (Join-Path $sdkBin 'dlm-network-check.exe')
    Copy-Item -LiteralPath $builtTransportChecker -Destination (Join-Path $sdkBin 'dlm-transport-check.exe')
    foreach ($gatewayFile in @('dlm-net.exe', 'dlm_gateway.dll', 'libdlm_gateway.dll.a')) {
        $builtGateway = Join-Path $env:CARGO_TARGET_DIR ($targetTriple + '\release\' + $gatewayFile)
        if (-not (Test-Path -LiteralPath $builtGateway -PathType Leaf)) { throw ('Cargo gateway output is missing: ' + $builtGateway) }
        Copy-Item -LiteralPath $builtGateway -Destination (Join-Path $sdkBin $gatewayFile)
    }
    foreach ($labFile in @('dlm-channel-lab.exe', 'dlm_channel_lab.dll', 'libdlm_channel_lab.dll.a')) {
        $builtLab = Join-Path $env:CARGO_TARGET_DIR ($targetTriple + '\release\' + $labFile)
        if (-not (Test-Path -LiteralPath $builtLab -PathType Leaf)) { throw ('Cargo experiment output is missing: ' + $builtLab) }
        Copy-Item -LiteralPath $builtLab -Destination (Join-Path $sdkBin $labFile)
    }
    $sdkInclude = Join-Path $stagePath 'include'
    New-Item -ItemType Directory -Path $sdkInclude -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'crates\dlm_gateway\include\dlm_gateway.h') -Destination $sdkInclude
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'crates\dlm_channel_lab\include\dlm_channel_lab.h') -Destination $sdkInclude
    Copy-Tree $gccPackage (Join-Path $stagePath 'toolchain')
    Get-ChildItem -LiteralPath (Split-Path -Parent $gcc) -Filter '*.dll' -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $sdkBin
    }
    Copy-Tree (Join-Path $sourceRoot 'lib') (Join-Path $stagePath 'lib')
    foreach ($directory in @('examples', 'docs')) {
        $from = Join-Path $sourceRoot $directory
        if (Test-Path -LiteralPath $from -PathType Container) { Copy-Tree $from (Join-Path $stagePath $directory) }
    }
    foreach ($filename in @('LICENSE', 'LICENSE-MIT', 'LICENSE-APACHE', 'COPYRIGHT')) {
        $from = Join-Path $sourceRoot $filename
        if (Test-Path -LiteralPath $from -PathType Leaf) { Copy-Item -LiteralPath $from -Destination $stagePath }
    }
    # User projects survive a rebuild; the complete previous output is also kept.
    if (Test-Path -LiteralPath (Join-Path $OutputPath 'projects') -PathType Container) {
        Copy-Tree (Join-Path $OutputPath 'projects') (Join-Path $stagePath 'projects')
    }
    & (Join-Path $PSScriptRoot 'Build-Quic.ps1') -SourcePath $sourceRoot -SdkPath $stagePath
    $wintunManifest = Get-Content -LiteralPath (Join-Path $sourceRoot 'vendor\DLN_WINTUN_SOURCES.json') -Raw | ConvertFrom-Json
    if ($wintunManifest.format -cne 'DLN-WINTUN-SOURCES-v1' -or $wintunManifest.architecture -cne 'amd64') { throw 'Unsupported Wintun dependency manifest.' }
    $wintunZip = Join-Path $CacheRoot ('wintun-' + $wintunManifest.version + '.zip')
    Get-Download $wintunManifest.url $wintunZip $wintunManifest.archive_sha256
    $wintunExtract = Join-Path $CacheRoot ('wintun-extract-' + [guid]::NewGuid().ToString('N'))
    try {
        Expand-Archive -LiteralPath $wintunZip -DestinationPath $wintunExtract -Force
        $wintunDll = Join-Path $wintunExtract $wintunManifest.dll_path
        if ((Get-FileHash -LiteralPath $wintunDll -Algorithm SHA256).Hash -ine $wintunManifest.dll_sha256) { throw 'Wintun DLL SHA256 mismatch.' }
        Copy-Item -LiteralPath $wintunDll -Destination (Join-Path $sdkBin 'wintun.dll')
        $licenses = Join-Path $stagePath 'licenses'
        New-Item -ItemType Directory -Path $licenses -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $wintunExtract $wintunManifest.license_path) -Destination (Join-Path $licenses 'Wintun-LICENSE.txt')
    } finally { if (Test-Path -LiteralPath $wintunExtract) { Remove-Item -LiteralPath $wintunExtract -Recurse -Force } }
    $sdkExe = Join-Path $sdkBin 'dlm.exe'
    $systemPath = (Join-Path $env:SystemRoot 'System32') + ';' + $env:SystemRoot
    $env:PATH = $systemPath
    $sdkVersion = (& $sdkExe '--version' | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $sdkVersion -notmatch '^dlm\s+') { throw 'The packaged compiler cannot run without the build environment.' }
    $env:DLM_PATH = Join-Path $stagePath 'lib'
    $env:CC = Join-Path $stagePath 'toolchain\mingw64\bin\gcc.exe'
    $env:PATH = (Split-Path -Parent $env:CC) + ';' + $systemPath
    $smokePath = Join-Path ([IO.Path]::GetTempPath()) ('DLM SDK smoke ' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $smokePath | Out-Null
    try {
        # Use the known test source, independent of existing user projects.
        $helloSource = Join-Path $PSScriptRoot 'sdk\projects\Hello\main.dlm'
        $smokeSource = Join-Path $smokePath 'main.dlm'
        Copy-Item -LiteralPath $helloSource -Destination $smokeSource
        Invoke-Native $sdkExe @('check', '--trusted-only', $smokeSource)
        Invoke-Native $sdkExe @('run', '--trusted-only', $smokeSource)
        $helloExe = Join-Path $smokePath 'Hello.exe'
        Invoke-Native $sdkExe @('build', '--trusted-only', $smokeSource, '-o', $helloExe)
        $env:PATH = $systemPath
        Test-HelloExecutable $helloExe $smokePath
        $helloProject = Join-Path $stagePath 'projects\Hello'
        # Do not attach the test binary to a modified user Hello project.
        if ((Get-FileHash -LiteralPath (Join-Path $helloProject 'main.dlm')).Hash -eq (Get-FileHash -LiteralPath $helloSource).Hash) {
            Copy-Item -LiteralPath $helloExe -Destination (Join-Path $helloProject 'Hello.exe') -Force
        }
    } finally { Remove-Item -LiteralPath $smokePath -Recurse -Force }

    $info = [ordered]@{
        version = $sdkVersion
        built_utc = [DateTime]::UtcNow.ToString('o')
        source = $sourceRoot
        target = $targetTriple
        rust = $rustVersion
        gcc = $gccVersion
        gcc_bundle_sha256 = $gccBundleHash
        compiler_sha256 = (Get-FileHash -LiteralPath $sdkExe -Algorithm SHA256).Hash.ToLowerInvariant()
        smoke = 'check, interpreter, native build, standalone native execution: passed'
        full_tests_requested = [bool]$RunTests
    }
    Write-Utf8 (Join-Path $stagePath 'build-info.json') (($info | ConvertTo-Json) + "`n")
    Write-Utf8 (Join-Path $stagePath 'VERSION.txt') ($sdkVersion + "`n")
    $backupPath = $null
    if (Test-Path -LiteralPath $OutputPath) {
        $backupPath = $OutputPath + '.previous-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
        [IO.Directory]::Move($OutputPath, $backupPath)
    }
    try { [IO.Directory]::Move($stagePath, $OutputPath) }
    catch {
        if ($backupPath -and -not (Test-Path -LiteralPath $OutputPath)) {
            [IO.Directory]::Move($backupPath, $OutputPath)
        }
        throw
    }
    $stagePath = $null
    Write-Host ''
    Write-Host ('READY: ' + $OutputPath) -ForegroundColor Green
    Write-Host ('Compiler: ' + $sdkVersion)
    Write-Host ('Open: ' + (Join-Path $OutputPath 'DLM-Shell.cmd'))
    Write-Host ('Try:  dlm build Hello\main.dlm -o Hello\Hello.exe')
    if ($backupPath) { Write-Host ('Previous output preserved: ' + $backupPath) }
} finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
    }
    [Net.ServicePointManager]::SecurityProtocol = $savedSecurityProtocol
    if ($transcriptStarted) { Stop-Transcript | Out-Null }
    if ($stagePath -and (Test-Path -LiteralPath $stagePath)) {
        Remove-Item -LiteralPath $stagePath -Recurse -Force
    }
}
