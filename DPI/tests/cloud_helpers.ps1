Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path -Parent $PSScriptRoot
. (Join-Path $sourceRoot 'tools\TrainingIO.ps1')
. (Join-Path $sourceRoot 'tools\CloudIO.ps1')
foreach ($text in @('0.0.0.0/0','10.0.0.0/8','104.16.0.1/13','::/0','100::/64','bad','104.16.0.1','192.168.0.0/16')) { $rejected=$false; try { [void](Convert-DPICloudPrefixes $text) } catch { $rejected=$true }; if (-not $rejected) { throw ('Invalid provider prefix accepted: ' + $text) } }
$clean=@(Convert-DPICloudPrefixes "# comment`n104.16.0.0/13`n2606:4700::/32`n104.16.0.0/13`n")
if ($clean.Count -ne 2) { throw 'Provider prefix deduplication failed' }
$genes=@(Convert-DPIPopulation @('1,1,5,8,1,2,65536,0','1,1,5,8,2,1,131072,2') $true)
if ($genes.Count -ne 1 -or $genes[0] -ne '1,1,5,8,0,0,1048576,0') { throw 'MWO candidate normalization retained unused fake-packet dimensions' }
if (@(Convert-DPIPopulation @('1,1,5,8,1,2,65536,0','1,1,5,8,2,1,131072,2') $false).Count -ne 2) { throw 'Legacy candidate dimensions were changed' }
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('DPI-CloudTests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $fixture 'data') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $fixture 'cache') -Force | Out-Null
try {
    foreach ($name in @('cloudflare-cidr.txt','cloudfront-cidr.txt','azure-frontdoor-cidr.txt','azure-cloud-cidr.txt')) { Copy-Item -LiteralPath (Join-Path $sourceRoot ('data\' + $name)) -Destination (Join-Path $fixture ('data\' + $name)) }
    $before=Get-DPICloudContext $fixture
    Write-AtomicText (Join-Path $fixture 'cache\cloudflare-cidr.txt') '0.0.0.0/0'
    if ((Get-DPICloudContext $fixture) -ne $before) { throw 'Malformed cached data bypassed valid bundle fallback' }
    function Get-DPICloudText([string]$Url) { throw 'Offline fixture' }
    Update-DPICloudRanges $fixture -Force
    if ((Get-DPICloudContext $fixture) -ne $before) { throw 'Offline refresh damaged bundled data' }
    $refresh=[IO.File]::ReadAllText((Join-Path $fixture 'cache\cdn-refresh.json')) | ConvertFrom-Json
    if (@($refresh.providers | Where-Object { $_.updated }).Count -ne 0) { throw 'Failed downloads were reported as successful updates' }
    [void](Set-DPICloudPrefixes $fixture 'cloudflare-cidr.txt' @('104.16.0.0/13') 'fixture')
    if ((Get-DPICloudContext $fixture) -eq $before) { throw 'Training data context missed a valid provider-list change' }
    function Get-DPICloudText([string]$Url) {
        if ($Url -like '*ips-v4') { return '104.16.0.0/13' }
        if ($Url -like '*ips-v6') { return '2606:4700::/32' }
        if ($Url -like '*ip-ranges.json') { return '{"createDate":"fixture","prefixes":[{"service":"CLOUDFRONT","region":"GLOBAL","ip_prefix":"13.32.0.0/15"},{"service":"CLOUDFRONT_ORIGIN_FACING","region":"GLOBAL","ip_prefix":"54.182.0.0/16"},{"service":"AMAZON","region":"GLOBAL","ip_prefix":"1.0.0.0/8"}],"ipv6_prefixes":[{"service":"CLOUDFRONT","region":"GLOBAL","ipv6_prefix":"2600:9000::/28"}]}' }
        if ($Url -like '*details.aspx*') { return '<a href="https://download.microsoft.com/download/a/b/ServiceTags_Public_20261116.json">Download</a>' }
        if ($Url -like '*ServiceTags_Public_*.json') { return '{"values":[{"name":"AzureFrontDoor.Frontend","properties":{"addressPrefixes":["13.107.208.0/21"]}},{"name":"AzureFrontDoor.Backend","properties":{"addressPrefixes":["13.0.0.0/8"]}},{"name":"AzureCloud","properties":{"addressPrefixes":["20.0.0.0/8"]}}]}' }
        throw 'Unexpected metadata source'
    }
    Update-DPICloudRanges $fixture -Force
    $refresh=[IO.File]::ReadAllText((Join-Path $fixture 'cache\cdn-refresh.json')) | ConvertFrom-Json
    if (@($refresh.providers | Where-Object { $_.updated }).Count -ne 3) { throw 'Valid official-format provider metadata was not accepted' }
    $front=[IO.File]::ReadAllText((Join-Path $fixture 'cache\cloudfront-cidr.txt'))
    if (-not $front.Contains('13.32.0.0/15') -or -not $front.Contains('2600:9000::/28') -or $front.Contains('54.182.') -or $front.Contains('1.0.0.0/8')) { throw 'CloudFront metadata mixed service/region scopes or lost IPv6' }
    $azure=[IO.File]::ReadAllText((Join-Path $fixture 'cache\azure-frontdoor-cidr.txt'))
    if ($azure.Contains('13.0.0.0/8') -or -not $azure.Contains('13.107.208.0/21')) { throw 'Azure Frontend and Backend tags were mixed' }
    Write-Host 'Provider metadata validation, official-format selection, offline fallback and candidate normalization passed.' 
} finally { [IO.Directory]::Delete($fixture,$true) }
