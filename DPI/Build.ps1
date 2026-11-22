[CmdletBinding()]
param(
    [string]$SdkPath = '',
    [string]$CCompiler = '',
    [ValidateSet('auto','scalar','sse2','avx2','avx512')][string]$Cpu = 'auto',
    [switch]$SkipTests,
    [switch]$TestsOnly,
    [switch]$Run
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$build = Join-Path $root 'build'
$saved = @{}
foreach ($name in @('PATH','CC','DLM_PATH')) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$pushed = $false
$transcribing = $false
$result = 1
function Invoke-Checked {
    param([string]$Executable, [string[]]$Arguments)
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw ('Command failed ({0}): {1}' -f $LASTEXITCODE, $Executable) }
}
try {
    if ($env:OS -ne 'Windows_NT') { throw 'This build script requires Windows x64.' }
    if ($SkipTests -and $TestsOnly) { throw 'SkipTests and TestsOnly cannot be combined.' }
    if ([string]::IsNullOrWhiteSpace($SdkPath)) {
        if (-not [string]::IsNullOrWhiteSpace($env:DLM_SDK)) { $SdkPath = $env:DLM_SDK }
        else { $SdkPath = Join-Path (Split-Path -Parent $root) 'DLM-OUT' }
    }
    $SdkPath = [IO.Path]::GetFullPath($SdkPath)
    $dlm = Join-Path $SdkPath 'bin\dlm.exe'
    if (-not (Test-Path -LiteralPath $dlm -PathType Leaf)) { throw ('Missing DLM compiler: ' + $dlm) }
    if ([string]::IsNullOrWhiteSpace($CCompiler)) {
        $CCompiler = Join-Path $SdkPath 'toolchain\mingw64\bin\gcc.exe'
        if (-not (Test-Path -LiteralPath $CCompiler -PathType Leaf)) {
            if (-not [string]::IsNullOrWhiteSpace($env:CC) -and (Test-Path -LiteralPath $env:CC -PathType Leaf)) { $CCompiler = $env:CC }
            else {
                $candidate = Get-Command 'gcc.exe' -ErrorAction SilentlyContinue
                if ($null -ne $candidate) { $CCompiler = $candidate.Source }
            }
        }
    }
    if (-not (Test-Path -LiteralPath $CCompiler -PathType Leaf)) {
        throw 'GCC is missing. Run ..\DLM-pre\Build-DLM.cmd to complete DLM-OUT, or pass -CCompiler with a MinGW x64 gcc.exe path. The supplied DLM ZIP omits the toolchain.'
    }
    $CCompiler = [IO.Path]::GetFullPath($CCompiler)
    $env:CC = $CCompiler
    $env:PATH = (Split-Path -Parent $CCompiler) + ';' + (Join-Path $SdkPath 'bin') + ';' + $saved['PATH']
    $env:DLM_PATH = $root
    $version = (& $dlm '--version' | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $version -notmatch '^dlm\s+(\d+\.\d+\.\d+)') { throw 'DLM compiler could not report its version.' }
    if ([version]$Matches[1] -lt [version]'0.133.0') { throw 'DLM 0.133.0 or later is required.' }
    $target = (& $CCompiler '-dumpmachine' | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $target -notmatch 'x86_64.*(mingw|windows)') { throw ('An x64 MinGW-compatible C compiler is required. Target: ' + $target) }
    foreach ($processName in @('NoDPI','NoDPIGui','NoDPIProbe','DPITrainer')) {
        $running = Get-Process -Name $processName -ErrorAction SilentlyContinue
        if ($null -ne $running) { throw ('Close ' + $processName + '.exe before rebuilding.') }
    }
    New-Item -ItemType Directory -Path $build -Force | Out-Null
    Start-Transcript -LiteralPath (Join-Path $build 'build.log') -Force | Out-Null
    $transcribing = $true
    Push-Location -LiteralPath $root
    $pushed = $true
    Write-Host ('Compiler: ' + $version)
    Write-Host ('C compiler: ' + $CCompiler)
    Write-Host ('CPU dispatch: ' + $Cpu)
    $common = @('--lib', $root, '--cpu', $Cpu, '--cflag', '-static')
    if (-not $SkipTests) {
        foreach ($testName in @('core','adaptive','automatic_checks','evolution_checks','capture_checks','telegram_checks','cloud_checks','startup_checks')) {
            $test = Join-Path $build ($testName + '-tests.exe')
            Invoke-Checked $dlm (@('build',('tests\' + $testName + '.dlm'),'-o',$test) + $common)
            Invoke-Checked $test @()
        }
    }
    if (-not $SkipTests) { foreach ($script in @('telegram_helpers','telegram_integration','network_guard','cloud_helpers','mwo_integration','driver_preflight')) { & (Join-Path $root ('tests\' + $script + '.ps1')); if (-not $?) { throw ('Telegram script tests failed: ' + $script) } } }
    if (-not $TestsOnly) {
        $importLibrary = Join-Path $root 'vendor\WinDivert\WinDivert.lib'
        foreach ($path in @($importLibrary, (Join-Path $root 'WinDivert.dll'), (Join-Path $root 'WinDivert64.sys'))) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw ('Missing WinDivert distribution file: ' + $path) }
        }
        $engine = Join-Path $build 'NoDPI.new.exe'
        $gui = Join-Path $build 'NoDPIGui.new.exe'
        $probe = Join-Path $build 'NoDPIProbe.new.exe'
        Invoke-Checked $dlm (@('build','engine.dlm','--allow-unsafe','--link-object',$importLibrary,'--library','winhttp','--library','iphlpapi','-o',$engine) + $common)
        Invoke-Checked $dlm (@('build','main.dlm','--allow-unsafe','--library','dwmapi','--library','comdlg32','--cflag','-Wl,--subsystem,windows','-o',$gui) + $common)
        Invoke-Checked $dlm (@('build','probe-main.dlm','--allow-unsafe','--library','winhttp','-o',$probe) + $common)
        $trainer = Join-Path $build 'DPITrainer.new.exe'
        Invoke-Checked $dlm (@('build','trainer-main.dlm','--allow-unsafe','-o',$trainer) + $common)
        Move-Item -LiteralPath $engine -Destination (Join-Path $root 'NoDPI.exe') -Force
        Move-Item -LiteralPath $gui -Destination (Join-Path $root 'NoDPIGui.exe') -Force
        Move-Item -LiteralPath $probe -Destination (Join-Path $root 'NoDPIProbe.exe') -Force
        Move-Item -LiteralPath $trainer -Destination (Join-Path $root 'DPITrainer.exe') -Force
        $localBuild=[ordered]@{ application_version='2.4'; dlm=$version; c_target=$target; cpu=$Cpu; built_at=(Get-Date -Format o); binaries=@() }
        foreach ($name in @('NoDPI.exe','NoDPIGui.exe','NoDPIProbe.exe','DPITrainer.exe')) { $hash=Get-FileHash -LiteralPath (Join-Path $root $name) -Algorithm SHA256; $localBuild.binaries += [pscustomobject]@{ file=$name; sha256=$hash.Hash } }
        $localBuild | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'build-local.json') -Encoding UTF8
        # Static runtime linking keeps the applications independent of the SDK DLLs.
        if ($Run) { Start-Process -FilePath (Join-Path $root 'NoDPIGui.exe') -WorkingDirectory $root }
        Write-Host 'READY: NoDPIGui.exe / NoDPI.exe / NoDPIProbe.exe / DPITrainer.exe' -ForegroundColor Green
    } else { Write-Host 'All DLM core tests passed.' -ForegroundColor Green }
    $result = 0
} catch {
    Write-Host ('BUILD FAILED: ' + $_.Exception.Message) -ForegroundColor Red
} finally {
    if ($pushed) { Pop-Location }
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
    if ($transcribing) { Stop-Transcript | Out-Null }
}
exit $result
