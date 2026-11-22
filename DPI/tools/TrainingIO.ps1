Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'NetworkGuard.ps1')
function Write-AtomicText([string]$Path, [string]$Text) {
    $temporary = $Path + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
    [IO.File]::WriteAllText($temporary,$Text,[Text.UTF8Encoding]::new($false))
    $backup=$temporary+'.previous'
    try {
        if ([IO.File]::Exists($Path)) { [IO.File]::Replace($temporary,$Path,$backup) }
        else { [IO.File]::Move($temporary,$Path) }
    } finally { if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }; if ([IO.File]::Exists($backup)) { [IO.File]::Delete($backup) } }
}
function Write-AutoTargets([string]$Root) {
    $cache = Join-Path $Root 'cache'
    New-Item -ItemType Directory -Path $cache -Force | Out-Null
    $addresses = [Collections.Generic.HashSet[string]]::new()
    $names=@('www.youtube.com','youtube.com','i.ytimg.com','redirector.googlevideo.com','discord.com','gateway.discord.gg','cdn.discordapp.com','web.telegram.org','pluto.web.telegram.org','venus.web.telegram.org','aurora.web.telegram.org','vesta.web.telegram.org','flora.web.telegram.org','mwomercs.com','ac.mwomercs.com','static.mwomercs.com')
    $mediaFile=Join-Path $Root 'media-url.txt'
    if (Test-Path -LiteralPath $mediaFile -PathType Leaf) {
        $uri=$null
        if ([Uri]::TryCreate([IO.File]::ReadAllText($mediaFile).Trim(),[UriKind]::Absolute,[ref]$uri) -and $uri.Scheme -eq 'https' -and $uri.Host -match '(^|\.)googlevideo\.com$') { $names += $uri.DnsSafeHost }
    }
    $tasks=@{}
    foreach ($name in $names) { try { $tasks[$name]=[Net.Dns]::GetHostAddressesAsync($name) } catch { } }
    $deadline=[Diagnostics.Stopwatch]::StartNew()
    foreach ($name in $names) {
        try {
            if (-not $tasks.ContainsKey($name)) { continue }
            $task=$tasks[$name]
            if ($task.IsCompleted -or $task.Wait([Math]::Max(0,2500-[int]$deadline.ElapsedMilliseconds))) {
                foreach ($address in $task.GetAwaiter().GetResult()) {
                    $suffix = '/128'
                    if ($address.AddressFamily -eq [Net.Sockets.AddressFamily]::InterNetwork) { $suffix = '/32' }
                    [void]$addresses.Add($address.ToString() + $suffix)
                }
            }
        } catch { }
    }
    Write-AtomicText (Join-Path $cache 'auto-targets.txt') ("# DNS target addresses; generated " + [DateTime]::UtcNow.ToString('o') + "`n" + ((@($addresses) | Sort-Object) -join "`n") + "`n")
}
function Get-NetworkContext { return Get-DPINetworkHash (Get-DPINetworkSnapshot) }
function Quote-DPIArgument([string]$Value) {
    $quoted = '"'; $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') { $slashes++; continue }
        if ($character -eq '"') { $quoted += ('\' * ($slashes * 2 + 1)); $quoted += '"' }
        else { $quoted += ('\' * $slashes); $quoted += $character }
        $slashes = 0
    }
    return ($quoted + ('\' * ($slashes * 2)) + '"')
}
