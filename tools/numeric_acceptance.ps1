# Numeric section acceptance (N16) on Windows with MinGW-w64 gcc in PATH. Sanitizers are not required here.
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
$env:DLM_REQUIRE_SANITIZERS = "0"
$env:DLM_REQUIRE_WINDOWS = "0"
$tests = "integers","machine_arithmetic","exact_numbers","source_exact","rational","source_wide_exact","exact_ops",
         "source_exact_ops","exact_complex_q","complex_q","source_complex_q","interval_q","creal","ccomplex","elementary",
         "convert","numeric_contract","dense_profile","scalar_witness","ir_verify","example_corpus","numeric_acceptance"
$argsList = @("test","-p","dlm_core","--release","--locked","--offline","--lib")
foreach ($t in $tests) { $argsList += @("--test",$t) }
& cargo @argsList
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cargo test -p dlm_cli --release --locked --offline --test cli_creal_ownership
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Output "numeric acceptance: passed"
