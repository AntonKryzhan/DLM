[CmdletBinding()]
param(
    [string]$SdkPath = '',
    [switch]$SkipTests,
    [switch]$TestsOnly
)

# Windows PowerShell 5.1 and PowerShell 7. No Rust or downloads are needed.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$buildRoot = Join-Path $projectRoot 'build'
$savedEnvironment = @{}
foreach ($name in @('PATH', 'CC', 'DLM_PATH')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$transcribing = $false
$locationPushed = $false
$result = 1

function Invoke-Checked {
    param([string]$Executable, [string[]]$Arguments)
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw ('Command failed with exit code {0}: {1}' -f $LASTEXITCODE, $Executable)
    }
}

try {
    if ($env:OS -ne 'Windows_NT') { throw 'Build.ps1 requires Windows and the DLM Windows SDK.' }
    if ($TestsOnly -and $SkipTests) { throw 'TestsOnly and SkipTests cannot be used together.' }
    if ([string]::IsNullOrWhiteSpace($SdkPath)) {
        if (-not [string]::IsNullOrWhiteSpace($env:DLM_SDK)) { $SdkPath = $env:DLM_SDK }
        else { $SdkPath = Join-Path (Split-Path -Parent $projectRoot) 'DLM-OUT' }
    }
    $SdkPath = [IO.Path]::GetFullPath($SdkPath)
    $dlm = Join-Path $SdkPath 'bin\dlm.exe'
    $lib = Join-Path $SdkPath 'lib'
    $gcc = Join-Path $SdkPath 'toolchain\mingw64\bin\gcc.exe'
    $windres = Join-Path $SdkPath 'toolchain\mingw64\bin\windres.exe'
    foreach ($file in @($dlm, $gcc)) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw ('Missing SDK file: ' + $file) }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $lib 'std\vector.dlm') -PathType Leaf)) {
        throw ('DLM standard vector library is missing: ' + $lib)
    }
    if (-not $TestsOnly -and -not (Test-Path -LiteralPath $windres -PathType Leaf)) {
        throw ('Missing resource compiler: ' + $windres)
    }
    New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
    Start-Transcript -LiteralPath (Join-Path $buildRoot 'build.log') -Force | Out-Null
    $transcribing = $true
    Push-Location -LiteralPath $projectRoot
    $locationPushed = $true
    $env:CC = $gcc
    $env:DLM_PATH = $lib
    $env:PATH = (Split-Path -Parent $gcc) + ';' + $savedEnvironment['PATH']
    $versionText = (& $dlm '--version' | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $versionText -notmatch '^dlm\s+(\d+\.\d+\.\d+)') {
        throw 'The SDK compiler could not report its version.'
    }
    if ([version]$Matches[1] -lt [version]'0.130.0') { throw 'DLM 0.130.0 or later is required.' }
    Write-Host ('Compiler: ' + $versionText)
    Write-Host ('SDK:      ' + $SdkPath)
    Write-Host ('Project:  ' + $projectRoot)

    if (-not $SkipTests) {
        foreach ($source in @('tests.dlm', 'ui-tests.dlm')) {
            $testExe = Join-Path $buildRoot (([IO.Path]::GetFileNameWithoutExtension($source)) + '.exe')
            Invoke-Checked $dlm @('build', '--trusted-only', '--lib', $lib, $source, '-o', $testExe)
            Invoke-Checked $testExe @()
        }
    }
    if ($TestsOnly) {
        Write-Host 'All calculator tests passed.'
    } else {
        # Emit C first so the final app uses the GUI subsystem and static libraries.
        $cBase = Join-Path $buildRoot 'Calculator'
        Invoke-Checked $dlm @('build', '--trusted-only', '--lib', $lib, '--c-only', 'main.dlm', '-o', $cBase)
        $resource = Join-Path $buildRoot 'Calculator.res.o'
        Push-Location -LiteralPath (Join-Path $projectRoot 'assets')
        try {
            Invoke-Checked $windres @('--input-format=rc', '--output-format=coff', '--codepage=65001', 'calculator.rc', $resource)
        } finally { Pop-Location }
        $candidate = Join-Path $buildRoot 'Calculator.new.exe'
        Invoke-Checked $gcc @('-O2', '-std=gnu11', '-w', '-ffp-contract=off', '-static', '-mwindows',
            ($cBase + '.c'), $resource, '-o', $candidate, '-lshell32', '-lws2_32', '-lgdi32', '-luser32')
        $outputExe = Join-Path $projectRoot 'Calculator.exe'
        Move-Item -LiteralPath $candidate -Destination $outputExe -Force
        Write-Host ''
        Write-Host ('READY: ' + $outputExe)
        Write-Host 'Start Run.cmd or Calculator.exe.'
    }
    $result = 0
} catch {
    Write-Host ''
    Write-Host ('BUILD FAILED: ' + $_.Exception.Message) -ForegroundColor Red
    Write-Host 'Close a running Calculator.exe before rebuilding.'
    if (Test-Path -LiteralPath $buildRoot) { Write-Host ('Log: ' + (Join-Path $buildRoot 'build.log')) }
} finally {
    if ($locationPushed) { Pop-Location }
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
    }
    if ($transcribing) { Stop-Transcript | Out-Null }
}
exit $result
