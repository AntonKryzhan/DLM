param([string]$Python = 'python', [switch]$SkipFullRegression, [string]$OpenGlDll = '')
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$env:RUST_MIN_STACK = '33554432'
if ($OpenGlDll) { $env:DLM_TEST_OPENGL_DLL = (Resolve-Path -LiteralPath $OpenGlDll).Path }
function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command exited with $LASTEXITCODE" }
}
if (-not $SkipFullRegression) {
    Invoke-Checked cargo @('test','--workspace','--release','--target','x86_64-pc-windows-gnu')
} else {
    Invoke-Checked cargo @('test','-p','dlm_core','--release','--target','x86_64-pc-windows-gnu','--test','native_foundation','--test','native_platform')
}
Invoke-Checked cargo @('test','-p','dlm_core','--release','--target','x86_64-pc-windows-gnu','--test','native_platform','editor_embeds','--','--ignored','--nocapture')
Invoke-Checked cargo @('build','--workspace','--release','--target','x86_64-pc-windows-gnu')
$compiler = Join-Path $PWD 'target/x86_64-pc-windows-gnu/release/dlm.exe'
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('dlm-formats-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null
try {
    $xml = Join-Path $temporary 'xml.dll'
    $settings = Join-Path $temporary 'settings.dll'
    Invoke-Checked $compiler @('build','examples/native/xml.dlm','--lib','lib','--allow-unsafe','--target','sharedlib','-o',$xml)
    Invoke-Checked $compiler @('build','examples/native/versioned_state.dlm','--lib','lib','--allow-unsafe','--target','sharedlib','-o',$settings)
    Invoke-Checked $Python @('crates/dlm_core/tests/native_hosts/interop.py',$xml,$settings)
} finally { Remove-Item -Recurse -Force $temporary }
