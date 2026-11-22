Set-StrictMode -Version Latest
function Find-DPIBrowser {
    $paths = [Collections.Generic.List[string]]::new()
    foreach ($name in @('chrome.exe','msedge.exe')) {
        foreach ($base in @('HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\','HKLM:\Software\Microsoft\Windows\CurrentVersion\App Paths\')) {
            try { $key=Get-Item -LiteralPath ($base+$name); $value=[string]$key.GetValue(''); if ($value) { $paths.Add($value.Trim('"')) } } catch { }
        }
    }
    foreach ($folder in @($env:ProgramFiles,[Environment]::GetEnvironmentVariable('ProgramFiles(x86)'),$env:LOCALAPPDATA)) {
        if (-not $folder) { continue }
        $paths.Add((Join-Path $folder 'Google\Chrome\Application\chrome.exe'))
        $paths.Add((Join-Path $folder 'Microsoft\Edge\Application\msedge.exe'))
    }
    foreach ($path in $paths) { if (Test-Path -LiteralPath $path -PathType Leaf) { return $path } }
    return ''
}
function Test-YouTubeDOM([string]$HTML) {
    return ($HTML -match '(?is)<title[^>]*>[^<]*YouTube[^<]*</title>' -and $HTML -match 'ytInitialData' -and $HTML -match '<ytd-app' -and $HTML -notmatch 'chrome-error://|ERR_CONNECTION_RESET|ERR_CONNECTION_TIMED_OUT|ERR_NAME_NOT_RESOLVED')
}
function Measure-DPIBrowser([string]$Executable, [string]$Report) {
    $temporary=Join-Path ([IO.Path]::GetTempPath()) ('NoDPI-browser-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $temporary -Force | Out-Null
    $process=$null; $finished=$false; $watch=[Diagnostics.Stopwatch]::StartNew()
    $passed=$false; $bytes=0L; $reason='not_started'
    try {
        $arguments=@('--headless','--dump-dom','--timeout=10000','--virtual-time-budget=3000','--no-first-run','--no-default-browser-check','--no-proxy-server',('--user-data-dir='+(Join-Path $temporary 'profile')),'https://www.youtube.com/')
        $command=($arguments | ForEach-Object { Quote-DPIArgument $_ }) -join ' '
        $output=Join-Path $temporary 'dom.txt'; $errors=Join-Path $temporary 'stderr.txt'
        $process=Start-Process -FilePath $Executable -ArgumentList $command -PassThru -WindowStyle Hidden -RedirectStandardOutput $output -RedirectStandardError $errors
        while (-not $process.WaitForExit(250)) { Check-Stop; if ($watch.ElapsedMilliseconds -gt 15000) { break } }
        if ($process.HasExited) {
            $finished=$true; $reason='browser_or_dom_failure'
            if ($process.ExitCode -eq 0 -and (Test-Path -LiteralPath $output)) {
                $info=Get-Item -LiteralPath $output; $bytes=$info.Length
                if ($bytes -le 16MB) { $passed=Test-YouTubeDOM ([IO.File]::ReadAllText($output)); if ($passed) { $reason='page_dom_markers_only_not_video' } }
            }
        } else { $reason='browser_timeout' }
    } finally {
        if ($null -ne $process) {
            if (-not $finished -and -not $process.HasExited) { & taskkill.exe /PID $process.Id /T /F 2>&1 | Out-Null; [void]$process.WaitForExit(2000) }
            $process.Dispose()
        }
        Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue
    }
    $value=[pscustomobject]@{ schema=1; target='chromium_youtube'; passed=$passed; critical=$true; elapsed_ms=$watch.ElapsedMilliseconds; dom_bytes=$bytes; reason=$reason; browser=[IO.Path]::GetFileName($Executable); isolated_profile=$true; proxy='direct'; media_tested=$false }
    Write-AtomicText $Report ($value | ConvertTo-Json -Depth 4)
    return $value
}
