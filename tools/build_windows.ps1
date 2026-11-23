param(
    [string]$CCompiler = "gcc",
    [switch]$SkipTests,
    [switch]$TestOnly,
    [string[]]$TestTargets = @()
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$previousCC = $env:CC
$previousTarget = $env:CARGO_BUILD_TARGET
$previousLinker = $env:CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER
Push-Location $repo
try {
    $env:CC = $CCompiler
    Get-Command cargo -ErrorAction Stop | Out-Null
    Get-Command $CCompiler -ErrorAction Stop | Out-Null
    $target = & $CCompiler -dumpmachine
    if ($LASTEXITCODE -ne 0 -or $target -notmatch '^x86_64-.*(mingw|windows-gnu)') {
        throw "Use 64-bit MinGW-w64 GCC/Clang (MSYS2 UCRT64); compiler target: $target"
    }
    & rustup target add x86_64-pc-windows-gnu
    if ($LASTEXITCODE -ne 0) { throw "Rust GNU target installation failed" }
    $env:CARGO_BUILD_TARGET = "x86_64-pc-windows-gnu"
    $env:CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER = $CCompiler
    if (-not $SkipTests) {
        if ($TestTargets.Count -gt 0) {
            $testArguments = @('test', '-p', 'dlm_core', '--locked')
            foreach ($testTarget in $TestTargets) { $testArguments += @('--test', $testTarget) }
            & cargo @testArguments
        } else {
            & cargo test --workspace --locked --no-fail-fast
        }
        if ($LASTEXITCODE -ne 0) { throw "DLM tests failed" }
    }
    if ($TestOnly) { return }
    & cargo build --release --locked --workspace
    if ($LASTEXITCODE -ne 0) { throw "DLM build failed" }
    $smoke = Join-Path ([System.IO.Path]::GetTempPath()) ("dlm windows " + [guid]::NewGuid())
    New-Item -ItemType Directory -Path $smoke | Out-Null
    try {
        $program = Join-Path $repo "examples/valid/word_count.dlm"
        $output = Join-Path $smoke "word count.exe"
        & ./target/x86_64-pc-windows-gnu/release/dlm.exe build $program -o $output
        if ($LASTEXITCODE -ne 0) { throw "Native program compilation failed" }
        $inputPath = Join-Path $smoke "input.txt"
        [System.IO.File]::WriteAllText($inputPath, "one two`nthree", [System.Text.UTF8Encoding]::new($false))
        $result = Join-Path $smoke "output.txt"
        $process = Start-Process -FilePath $output -ArgumentList "Anton" -RedirectStandardInput $inputPath -RedirectStandardOutput $result -NoNewWindow -Wait -PassThru
        if ($process.ExitCode -ne 0) { throw "Native program failed: $($process.ExitCode)" }
        $actual = [System.IO.File]::ReadAllText($result).Replace("`r`n", "`n")
        if ($actual -ne "hello, Anton`nlines: 2`nwords: 3`nchars: 13`n") {
            throw "Native program output differs: $actual"
        }
    } finally {
        Remove-Item -Recurse -Force $smoke
    }
    Write-Host "Ready: $repo/target/x86_64-pc-windows-gnu/release/dlm.exe"
} finally {
    $env:CC = $previousCC
    $env:CARGO_BUILD_TARGET = $previousTarget
    $env:CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER = $previousLinker
    Pop-Location
}
