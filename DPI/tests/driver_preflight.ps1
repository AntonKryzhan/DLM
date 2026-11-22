Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path -Parent $PSScriptRoot
. (Join-Path $sourceRoot 'tools\TrainingIO.ps1')
. (Join-Path $sourceRoot 'tools\DriverIO.ps1')
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('DPI-DriverTests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $fixture 'logs') -Force | Out-Null
try {
    $script:mockDriverError=58; $script:mockReady=$false; $script:mockExit=2; $script:mockReport=$true
    function Invoke-DPIDriverCheck([string]$Root, [string]$Report) {
        if ([IO.File]::Exists($Report)) { throw 'Stale preflight report was reused' }
        if ($script:mockReport) { Write-AtomicText $Report (ConvertTo-Json -InputObject @{ schema=1; pid=123; component='WinDivertFalseProbe'; win32_error=$script:mockDriverError; ready=$script:mockReady }) }
        return $script:mockExit
    }
    $report=Join-Path $fixture 'logs\driver-fixture.json'; $started=0; $failure=''
    try { Test-DPIDriver $fixture $report; $started++ } catch { $failure=$_.Exception.Message }
    if ($started -ne 0 -or -not $failure.Contains('Win32=58') -or -not $failure.Contains('WinDivertFalseProbe')) { throw 'Driver failure did not stop measurement with the exact error' }
    $script:mockDriverError=0;$script:mockReady=$true;$script:mockExit=0
    Test-DPIDriver $fixture $report
    $script:mockReport=$false;$missing=$false
    try { Test-DPIDriver $fixture $report } catch { $missing=$_.Exception.Message.Contains('without a report') }
    if (-not $missing) { throw 'Missing startup report was treated as driver success' }
    Write-AtomicText (Join-Path $fixture 'logs\startup-123.json') (ConvertTo-Json -InputObject @{ pid=123; component='WinDivertOpen'; win32_error=58; ready=$false })
    $errorText=Get-DPIStartupFailure $fixture 123
    if (-not $errorText.Contains('Win32=58') -or -not $errorText.Contains('WinDivertOpen')) { throw 'Candidate startup failure lost the native diagnostic code' }
    $train=[IO.File]::ReadAllText((Join-Path $sourceRoot 'Train.ps1'))
    if ($train.IndexOf('Test-DPIDriver $root') -gt $train.IndexOf("`$baseline = Measure-Probes")) { throw 'Driver preflight runs after lengthy baseline probes' }
    Write-Host 'Driver preflight failure, stale-report rejection and native error propagation passed.'
} finally { [IO.Directory]::Delete($fixture,$true) }
