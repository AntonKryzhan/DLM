Set-StrictMode -Version Latest
function Convert-DPICloudPrefixes([string]$Text) {
    if ([Text.Encoding]::UTF8.GetByteCount($Text) -gt 1MB) { throw 'CDN range file exceeds 1 MiB' }
    $out=[Collections.Generic.HashSet[string]]::new()
    foreach ($raw in ($Text -split '\r?\n')) {
        $line=($raw -split '[#;]',2)[0].Trim().TrimStart([char]0xfeff)
        if (-not $line) { continue }
        if ($line -notmatch '^([0-9A-Fa-f:.]+)/([0-9]{1,3})$') { throw 'Invalid CDN prefix syntax' }
        $ip=$null; $bits=[int]$Matches[2]
        if (-not [Net.IPAddress]::TryParse($Matches[1],[ref]$ip)) { throw 'Invalid CDN address' }
        $bytes=$ip.GetAddressBytes(); $limit=$bytes.Length*8
        if ($bits -gt $limit -or ($limit -eq 32 -and $bits -lt 8) -or ($limit -eq 128 -and $bits -lt 16)) { throw 'CDN prefix is too broad' }
        if ($limit -eq 32) {
            if ($bytes[0] -eq 0 -or $bytes[0] -eq 10 -or $bytes[0] -eq 127 -or $bytes[0] -ge 224 -or ($bytes[0] -eq 100 -and $bytes[1] -ge 64 -and $bytes[1] -le 127) -or ($bytes[0] -eq 169 -and $bytes[1] -eq 254) -or ($bytes[0] -eq 172 -and $bytes[1] -ge 16 -and $bytes[1] -le 31) -or ($bytes[0] -eq 192 -and $bytes[1] -eq 168)) { throw 'Non-public CDN address' }
        } elseif (($bytes[0] -band 224) -ne 32) { throw 'Non-public IPv6 CDN address' }
        for ($i=$bits; $i -lt $limit; $i++) { if (($bytes[[int][Math]::Floor($i/8)] -band (1 -shl (7-($i%8)))) -ne 0) { throw 'CDN prefix has host bits set' } }
        [void]$out.Add(($ip.ToString().ToLowerInvariant() + '/' + $bits))
        if ($out.Count -gt 16384) { throw 'Too many CDN prefixes' }
    }
    if ($out.Count -eq 0) { throw 'Empty CDN prefix set' }
    return @($out | Sort-Object)
}
function Get-DPICloudSelection([string]$Root, [string]$Name) {
    foreach ($dir in @('cache','data')) {
        $path=Join-Path (Join-Path $Root $dir) $Name
        try {
            if (-not [IO.File]::Exists($path) -or ([IO.FileInfo]$path).Length -gt 1MB) { continue }
            $text=[IO.File]::ReadAllText($path)
            [void](Convert-DPICloudPrefixes $text)
            return $path
        } catch { }
    }
    throw ('No valid bundled or cached CDN data: ' + $Name)
}
function Get-DPICloudContext([string]$Root) {
    $rows=@()
    foreach ($name in @('cloudflare-cidr.txt','cloudfront-cidr.txt','azure-frontdoor-cidr.txt','azure-cloud-cidr.txt')) {
        $path=Get-DPICloudSelection $Root $name
        $rows += $name + ':' + (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    }
    return $rows -join '|'
}
function Get-DPICloudText([string]$Url) {
    $uri=[Uri]$Url
    if ($uri.Scheme -ne 'https' -or $uri.Host -notin @('www.cloudflare.com','ip-ranges.amazonaws.com','www.microsoft.com','download.microsoft.com')) { throw 'Unsupported CDN metadata source' }
    Add-Type -AssemblyName System.Net.Http
    $handler=[Net.Http.HttpClientHandler]::new(); $handler.UseProxy=$false; $handler.AllowAutoRedirect=$false
    $client=[Net.Http.HttpClient]::new($handler); $client.Timeout=[TimeSpan]::FromMilliseconds(2500); $client.MaxResponseContentBufferSize=8MB
    try {
        $task=$client.GetStringAsync($uri)
        if (-not $task.Wait(2800)) { throw 'CDN metadata download timed out' }
        return $task.GetAwaiter().GetResult()
    } finally { $client.Dispose() }
}
function Set-DPICloudPrefixes([string]$Root, [string]$Name, [string[]]$Prefixes, [string]$Source) {
    $clean=@(Convert-DPICloudPrefixes ($Prefixes -join "`n"))
    Write-AtomicText (Join-Path (Join-Path $Root 'cache') $Name) ("# Source: " + $Source + "`n# Retrieved UTC: " + [DateTime]::UtcNow.ToString('o') + "`n" + ($clean -join "`n") + "`n")
    return $clean.Count
}
function Update-DPICloudRanges([string]$Root, [switch]$Force) {
    $cache=Join-Path $Root 'cache'; New-Item -ItemType Directory -Path $cache -Force | Out-Null
    $statusFile=Join-Path $cache 'cdn-refresh.json'; $now=[DateTime]::UtcNow
    if (-not $Force) {
        try {
            $old=[IO.File]::ReadAllText($statusFile) | ConvertFrom-Json
            if ($now -lt [DateTime]::Parse([string]$old.next_attempt_utc).ToUniversalTime()) { return }
        } catch { }
        if (-not [IO.File]::Exists($statusFile)) {
            $bundled=Join-Path (Join-Path $Root 'data') 'cdn-sources.json'
            try { $snapshot=[IO.File]::ReadAllText($bundled) | ConvertFrom-Json; if (($now-[DateTime]::Parse([string]$snapshot.checked_utc).ToUniversalTime()).TotalDays -lt 7) { [void](Get-DPICloudContext $Root); return } } catch { }
        }
    }
    $rows=[Collections.Generic.List[object]]::new()
    foreach ($provider in @('cloudflare','cloudfront','azure')) {
        try {
            $count=0
            if ($provider -eq 'cloudflare') {
                $v4=Get-DPICloudText 'https://www.cloudflare.com/ips-v4'; $v6=Get-DPICloudText 'https://www.cloudflare.com/ips-v6'
                $count=Set-DPICloudPrefixes $Root 'cloudflare-cidr.txt' (($v4 + "`n" + $v6) -split '\r?\n' | Where-Object { $_.Trim() }) 'https://www.cloudflare.com/ips/'
            } elseif ($provider -eq 'cloudfront') {
                $value=(Get-DPICloudText 'https://ip-ranges.amazonaws.com/ip-ranges.json') | ConvertFrom-Json
                $prefixes=@($value.prefixes | Where-Object { $_.service -eq 'CLOUDFRONT' -and $_.region -eq 'GLOBAL' } | ForEach-Object { $_.ip_prefix }) + @($value.ipv6_prefixes | Where-Object { $_.service -eq 'CLOUDFRONT' -and $_.region -eq 'GLOBAL' } | ForEach-Object { $_.ipv6_prefix })
                $count=Set-DPICloudPrefixes $Root 'cloudfront-cidr.txt' $prefixes ('https://ip-ranges.amazonaws.com/ip-ranges.json; createDate=' + $value.createDate)
            } else {
                $page=Get-DPICloudText 'https://www.microsoft.com/en-us/download/details.aspx?id=56519'
                $match=[regex]::Match($page,'https://download\.microsoft\.com/download/[A-Za-z0-9/_.-]+/ServiceTags_Public_[0-9]{8}\.json')
                if (-not $match.Success) { throw 'Official Azure service-tag download URL not found' }
                $value=(Get-DPICloudText $match.Value) | ConvertFrom-Json
                $front=@($value.values | Where-Object { $_.name -ceq 'AzureFrontDoor.Frontend' })
                $cloud=@($value.values | Where-Object { $_.name -ceq 'AzureCloud' })
                if ($front.Count -ne 1 -or $cloud.Count -ne 1) { throw 'Required Azure tags not found' }
                # Validate both before replacing either file; AzureCloud is classification data only.
                [void](Convert-DPICloudPrefixes ($front[0].properties.addressPrefixes -join "`n")); [void](Convert-DPICloudPrefixes ($cloud[0].properties.addressPrefixes -join "`n"))
                $count=Set-DPICloudPrefixes $Root 'azure-frontdoor-cidr.txt' $front[0].properties.addressPrefixes $match.Value
                $count+=Set-DPICloudPrefixes $Root 'azure-cloud-cidr.txt' $cloud[0].properties.addressPrefixes $match.Value
            }
            $rows.Add([pscustomobject]@{ provider=$provider; updated=$true; prefixes=$count; error='' })
        } catch { $rows.Add([pscustomobject]@{ provider=$provider; updated=$false; prefixes=0; error=$_.Exception.Message }) }
    }
    $delay=7*24; if (@($rows | Where-Object { -not $_.updated }).Count -gt 0) { $delay=6 }
    Write-AtomicText $statusFile (ConvertTo-Json -InputObject ([ordered]@{ schema=1; attempted_utc=$now.ToString('o'); next_attempt_utc=$now.AddHours($delay).ToString('o'); providers=@($rows.ToArray()) }) -Depth 5)
    [void](Get-DPICloudContext $Root)
}
function Convert-DPIPopulation([string[]]$Population, [bool]$MwoOnly) {
    $seen=[Collections.Generic.HashSet[string]]::new()
    foreach ($gene in $Population) {
        $value=$gene
        if ($MwoOnly) {
            $fields=$gene.Split(','); if ($fields.Count -ne 8) { throw 'Invalid evolution gene' }
            $fields[4]='0'; $fields[5]='0'; $fields[6]='1048576'; $fields[7]='0'; $value=$fields -join ','
        }
        if ($seen.Add($value)) { $value }
    }
}
