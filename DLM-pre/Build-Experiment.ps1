#requires -Version 5.1
[CmdletBinding()]
param([switch]$SkipCompilerBuild, [switch]$RunLoopback)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'DLM-main'
if (-not (Test-Path -LiteralPath (Join-Path $source 'Cargo.toml') -PathType Leaf) -and (Test-Path -LiteralPath (Join-Path $root 'Cargo.toml') -PathType Leaf)) { $source = $root }
$sdk = Join-Path $root 'DLM-OUT'
if (-not $SkipCompilerBuild) { & (Join-Path $PSScriptRoot 'Build-DLM.ps1') -SourcePath $source -OutputPath $sdk }
$compiler = Join-Path $sdk 'bin\dlm.exe'
$cli = Join-Path $sdk 'bin\dlm-channel-lab.exe'
$cc = Join-Path $sdk 'toolchain\mingw64\bin\gcc.exe'
$fixture = Join-Path $source 'crates\dlm_core\tests\native_hosts\channel_lab_boundary.dlm'
$hostSource = Join-Path $source 'crates\dlm_core\tests\native_hosts\dln_experiment_host.c'
foreach ($path in @($compiler, $cli, $cc, $fixture, $hostSource, (Join-Path $sdk 'bin\dlm_channel_lab.dll'), (Join-Path $sdk 'lib\std\dln_experiment.dlm'))) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw ('Experiment component is missing: ' + $path) }
}
$versionText = (& $compiler '--version' | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $versionText -notmatch '^dlm ([0-9]+\.[0-9]+\.[0-9]+)$') { throw 'Cannot identify the DLM compiler.' }
if ([version]$Matches[1] -lt [version]'0.150.0') { throw 'The experiment module requires DLM 0.150.0 or later.' }
$names = @('PATH', 'CC', 'AR', 'DLM_PATH')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:CC = $cc
    $env:AR = Join-Path (Split-Path -Parent $cc) 'ar.exe'
    $env:DLM_PATH = Join-Path $sdk 'lib'
    $env:PATH = (Join-Path $sdk 'bin') + ';' + (Split-Path -Parent $cc) + ';' + $env:PATH
    $validation = Join-Path $sdk ('validation\experiment-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $validation -Force | Out-Null
    $library = Join-Path $validation 'experiment.a'
    & $compiler build $fixture --lib $env:DLM_PATH --allow-unsafe --target staticlib -o $library
    if ($LASTEXITCODE -ne 0) { throw 'Experiment DLM boundary compilation failed.' }
    $hostExecutable = Join-Path $validation 'experiment-host.exe'
    & $cc '-std=c11' '-O1' '-Wall' '-Wextra' '-Werror' '-UNDEBUG' $hostSource $library ('-L' + (Join-Path $sdk 'bin')) '-ldlm_channel_lab' '-lshell32' '-lws2_32' '-lgdi32' '-luser32' '-o' $hostExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Experiment C ABI host compilation failed.' }
    $hostOutput = @(& $hostExecutable)
    if ($LASTEXITCODE -ne 0 -or $hostOutput.Count -ne 1) { throw 'Experiment C/DLM runtime acceptance failed.' }
    $hostResult = $hostOutput[0] | ConvertFrom-Json
    if ($hostResult.status -cne 'PASS') { throw 'Experiment ABI host reported failure.' }
    $config = Join-Path $validation 'matrix.cfg'
    $inputFile = Join-Path $validation 'binary.input'
    $matrixPath = Join-Path $validation 'matrix.json'
    [IO.File]::WriteAllText($config, "seed=150`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllBytes($inputFile, [byte[]](0..255))
    & $cli matrix --config $config --input $inputFile --report $matrixPath | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'The experiment matrix failed.' }
    $matrix = [IO.File]::ReadAllText($matrixPath) | ConvertFrom-Json
    if ($matrix.status -cne 'MEASURED' -or @($matrix.cases).Count -ne 96) { throw 'The experiment matrix is incomplete.' }
    foreach ($case in $matrix.cases) {
        if ($case.case -ceq 'clean' -and -not $case.measurement.payload_exact_match) { throw 'A clean experimental channel failed.' }
        if ($case.case -ceq 'closed_channel' -and ($case.measurement.payload_exact_match -or $case.measurement.useful_bits_per_second -ne 0)) { throw 'A closed channel reported false delivery.' }
    }
    $loopback = [ordered]@{ status = 'NOT_RUN' }
    if ($RunLoopback) {
        $loopConfig = Join-Path $validation 'loopback.cfg'
        $loopInput = Join-Path $validation 'loopback.input'
        $loopReport = Join-Path $validation 'loopback.json'
        [IO.File]::WriteAllText($loopConfig, "mode=protocol`nslot_us=4000`n", [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllBytes($loopInput, [byte[]]@(65, 0, 255))
        & $cli loopback --config $loopConfig --input $loopInput --report $loopReport --capture (Join-Path $validation 'loopback.dlct') --output (Join-Path $validation 'loopback.output') | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Physical loopback measurement failed.' }
        $loopback = [IO.File]::ReadAllText($loopReport) | ConvertFrom-Json
        if (-not $loopback.payload_exact_match) { throw 'The physical loopback did not recover its payload.' }
    }
    $report = [ordered]@{
        format = 'DLM-EXPERIMENT-SDK-ACCEPTANCE-v1'; status = 'PASS'; scope = 'native local laboratory tests'; version = $versionText
        finished_utc = [DateTime]::UtcNow.ToString('o'); abi = $hostResult; matrix_cases = @($matrix.cases).Count; loopback = $loopback
        compiler_sha256 = (Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash.ToLowerInvariant()
        lab_cli_sha256 = (Get-FileHash -LiteralPath $cli -Algorithm SHA256).Hash.ToLowerInvariant()
        lab_library_sha256 = (Get-FileHash -LiteralPath (Join-Path $sdk 'bin\dlm_channel_lab.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
        production_carrier = $false; public_internet = 'NOT_RUN'; long_duration_soak = 'NOT_RUN'
    }
    $reportPath = Join-Path $validation 'report.json'
    [IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 12), [Text.UTF8Encoding]::new($false))
    Write-Host ('Experimental channel acceptance passed: ' + $reportPath)
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
