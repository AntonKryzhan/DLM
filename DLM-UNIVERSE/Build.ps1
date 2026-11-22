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
    if ([version]$Matches[1] -lt [version]'0.131.0') { throw 'DLM 0.131.0 or later is required (GPU presentation).' }
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
        Write-Host 'All universe tests passed.'
    } else {
        # Emit C first so the final app uses the GUI subsystem and static libraries.
        $cBase = Join-Path $buildRoot 'Universe'
        Invoke-Checked $dlm @('build', '--trusted-only', '--lib', $lib, '--c-only', 'main.dlm', '-o', $cBase)
        # The DLM vector scene protocol is extended only for this application.
        # Inject a checked native GPU command handler into generated C;
        # the installed DLM compiler and standard library stay untouched.
        $generatedC = $cBase + '.c'
        $sourceC = [IO.File]::ReadAllText($generatedC)
        $walkAnchor = 'static int dvy_walk(const nat_t *scene, size_t n, const dvy_scale *s, int mode, dvy_ctx *cx) {'
        $walkStart = $sourceC.IndexOf($walkAnchor, [StringComparison]::Ordinal)
        if ($walkStart -lt 0) { throw 'GPU extension incompatible with this DLM C runtime (dvy_walk missing).' }
        $handler = '        if (op == 1) {'
        $handlerStart = $sourceC.IndexOf($handler, $walkStart + $walkAnchor.Length, [StringComparison]::Ordinal)
        if ($handlerStart -lt 0) { throw 'GPU extension incompatible with this DLM C runtime (vector opcode dispatch missing).' }
        $nativeInclude = "static void *dgl_proc(const char *name);`n#include `"../native/gpu_particles.inc`"`n"
        $sourceC = $sourceC.Insert($walkStart, $nativeInclude)
        $handlerStart += $nativeInclude.Length
        $gpuHandler = @'
        if (op == 10) {
            shape = 0;
            nat_t args[15];
            for (size_t k = 0; k < 15; k++) args[k] = dvy_next(&r);
            if (!r.bad && mode == 2 && !up_draw(args, s, clip, (int)cx->w, dgl_h)) r.bad = 1;
        } else if (op == 1) {
'@
        $sourceC = $sourceC.Remove($handlerStart, $handler.Length).Insert($handlerStart, $gpuHandler + "`n")
        $closeAnchor = "    dgl_current(x);`n    unsigned t[2] = { x->ramp, x->radial };"
        if (-not $sourceC.Contains($closeAnchor)) { throw 'GPU extension incompatible with this DLM C runtime (OpenGL teardown missing).' }
        $sourceC = $sourceC.Replace($closeAnchor, "    dgl_current(x);`n    up_destroy();`n    unsigned t[2] = { x->ramp, x->radial };")
        $drawError = 'if (!dgl_draw(x)) return dvo_errs("invalid scene");'
        if (-not $sourceC.Contains($drawError)) { throw 'GPU extension incompatible with this DLM C runtime (scene draw error missing).' }
        $sourceC = $sourceC.Replace($drawError, 'if (!dgl_draw(x)) return dvo_errs("GPU scene failed: physical compute models require OpenGL 4.3; reduce quality if memory is exhausted. See universe-gpu.log");')
        [IO.File]::WriteAllText($generatedC, $sourceC, [Text.UTF8Encoding]::new($false))
        Write-Host 'Native GPU particle scene handler installed in generated C.'
        $resource = Join-Path $buildRoot 'Universe.res.o'
        Push-Location -LiteralPath (Join-Path $projectRoot 'assets')
        try {
            Invoke-Checked $windres @('--input-format=rc', '--output-format=coff', '--codepage=65001', 'universe.rc', $resource)
        } finally { Pop-Location }
        $candidate = Join-Path $buildRoot 'Universe.new.exe'
        Invoke-Checked $gcc @('-O2', '-std=gnu11', '-w', '-ffp-contract=off', '-static', '-mwindows',
            ($cBase + '.c'), $resource, '-o', $candidate, '-lshell32', '-lws2_32', '-lgdi32', '-luser32', '-lm')
        $outputExe = Join-Path $projectRoot 'Universe.exe'
        Move-Item -LiteralPath $candidate -Destination $outputExe -Force
        Write-Host ''
        Write-Host ('READY: ' + $outputExe)
        Write-Host 'Start Run.cmd or Universe.exe.'
    }
    $result = 0
} catch {
    Write-Host ''
    Write-Host ('BUILD FAILED: ' + $_.Exception.Message) -ForegroundColor Red
    Write-Host 'Close a running Universe.exe before rebuilding.'
    if (Test-Path -LiteralPath $buildRoot) { Write-Host ('Log: ' + (Join-Path $buildRoot 'build.log')) }
} finally {
    if ($locationPushed) { Pop-Location }
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
    }
    if ($transcribing) { Stop-Transcript | Out-Null }
}
exit $result
