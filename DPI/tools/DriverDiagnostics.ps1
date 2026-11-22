Set-StrictMode -Version Latest
function Get-DPIDriverSystem([string]$Root) {
    $result=[ordered]@{ schema=1; created_utc=[DateTime]::UtcNow.ToString('o'); services=@(); kernel_drivers=@(); files=@(); events=@(); read_errors=@(); security_settings_changed=$false }
    try { $result.windows=Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' | Select-Object ProductName,DisplayVersion,CurrentBuild,UBR } catch { $result.read_errors += 'OS: ' + $_.Exception.Message }
    try { $result.services=@(Get-Service -Name BFE,mpssvc -ErrorAction Stop | Select-Object Name,Status,StartType) } catch { $result.read_errors += 'Services: ' + $_.Exception.Message }
    try { $result.kernel_drivers=@(Get-CimInstance Win32_SystemDriver -ErrorAction Stop | Where-Object { $_.Name -match 'WinDivert|NoDPI|zapret' -or $_.PathName -match 'WinDivert' } | Select-Object Name,State,Started,StartMode,PathName,ExitCode,ServiceSpecificExitCode) } catch { $result.read_errors += 'Kernel drivers: ' + $_.Exception.Message }
    try { $result.device_guard=Get-CimInstance -Namespace 'root\Microsoft\Windows\DeviceGuard' -ClassName Win32_DeviceGuard -ErrorAction Stop | Select-Object VirtualizationBasedSecurityStatus,SecurityServicesConfigured,SecurityServicesRunning,CodeIntegrityPolicyEnforcementStatus,UsermodeCodeIntegrityPolicyEnforcementStatus } catch { $result.read_errors += 'Device Guard: ' + $_.Exception.Message }
    foreach ($name in @('WinDivert.dll','WinDivert64.sys','NoDPI.exe')) {
        $path=Join-Path $Root $name
        if (-not [IO.File]::Exists($path)) { $result.files += [pscustomobject]@{ name=$name; missing=$true }; continue }
        try {
            $signature=Get-AuthenticodeSignature -LiteralPath $path
            $subject=''; if ($null -ne $signature.SignerCertificate) { $subject=$signature.SignerCertificate.Subject }
            $result.files += [pscustomobject]@{ name=$name; sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash; signature_status=[string]$signature.Status; signer=$subject; bytes=([IO.FileInfo]$path).Length }
        } catch { $result.read_errors += $name + ': ' + $_.Exception.Message }
    }
    foreach ($log in @('Microsoft-Windows-CodeIntegrity/Operational','System')) {
        try {
            $events=@(Get-WinEvent -FilterHashtable @{ LogName=$log; StartTime=(Get-Date).AddHours(-24) } -MaxEvents 1000 -ErrorAction Stop | Where-Object { $_.Message -match 'WinDivert|NoDPI' } | Select-Object -First 50 TimeCreated,Id,ProviderName,LevelDisplayName,Message)
            $result.events += $events
        } catch { $result.read_errors += $log + ': ' + $_.Exception.Message }
    }
    return [pscustomobject]$result
}
