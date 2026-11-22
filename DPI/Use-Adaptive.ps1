[CmdletBinding()]
param([switch]$NoLaunch)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
. (Join-Path $root 'tools\Settings.ps1')
try {
    foreach ($name in @('NoDPI','NoDPIGui')) {
        if ($null -ne (Get-Process -Name $name -ErrorAction SilentlyContinue)) { throw ('Close ' + $name + '.exe before switching profiles.') }
    }
    $config = Join-Path $root 'config.ini'
    if (-not (Test-Path -LiteralPath $config -PathType Leaf)) { [IO.File]::WriteAllText($config,"[NoDPI]`r`n",[Text.UTF8Encoding]::new($false)) }
    $backup = $config + '.before-adaptive-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.bak'
    Copy-Item -LiteralPath $config -Destination $backup
    foreach ($item in @(
        @('SafeAutomatic','true'), @('AdaptiveStrategies','true'), @('StrategyProfile','auto'), @('ServerWindowCandidates','false'),
        @('DiagnosticLog','true'), @('UdpObfuscate','false'), @('QuicMode','pass'),
        @('AggressiveFrag','true'), @('AdaptiveRetry','true'), @('LearnStrategy','true'),
        @('ApplyECH','true'), @('Mode','Default')
    )) { Set-NoDPIValue $config $item[0] $item[1] }
    Write-Host 'Automatic profile enabled. Lists and their paths were preserved; configuration backup saved.' -ForegroundColor Green
    if (-not $NoLaunch) { Start-Process -FilePath (Join-Path $root 'NoDPIGui.exe') -WorkingDirectory $root }
    exit 0
} catch { Write-Host ('PROFILE FAILED: ' + $_.Exception.Message) -ForegroundColor Red; exit 1 }
