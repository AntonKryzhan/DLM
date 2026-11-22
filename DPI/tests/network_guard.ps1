Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'tools\NetworkGuard.ps1')
$adapters=@([pscustomobject]@{InterfaceIndex=34;Status='Up';MacAddress='00-11-22-33-44-55'},[pscustomobject]@{InterfaceIndex=7;Status='Disconnected';MacAddress='00-00-00-00-00-07'})
$addresses=@([pscustomobject]@{InterfaceIndex=34;IPAddress='10.10.10.117';PrefixLength=23;AddressState='Preferred';SkipAsSource=$false},[pscustomobject]@{InterfaceIndex=7;IPAddress='192.168.1.50';PrefixLength=24;AddressState='Deprecated';SkipAsSource=$false})
$routes=@([pscustomobject]@{InterfaceIndex=34;DestinationPrefix='0.0.0.0/0';NextHop='10.10.10.1';RouteMetric=0},[pscustomobject]@{InterfaceIndex=7;DestinationPrefix='0.0.0.0/0';NextHop='192.168.1.1';RouteMetric=0})
$interfaces=@([pscustomobject]@{InterfaceIndex=34;AddressFamily='IPv4';InterfaceMetric=25;ConnectionState='Connected'},[pscustomobject]@{InterfaceIndex=7;AddressFamily='IPv4';InterfaceMetric=35;ConnectionState='Disconnected'})
$dns=@([pscustomobject]@{InterfaceIndex=34;AddressFamily='IPv4';ServerAddresses=@('10.10.10.1','1.1.1.1')})
$before=Convert-DPINetworkSnapshot $adapters $addresses $routes $interfaces $dns
$hash=Get-DPINetworkHash $before
$reversed=Convert-DPINetworkSnapshot @($adapters[1],$adapters[0]) @($addresses[1],$addresses[0]) @($routes[1],$routes[0]) $interfaces $dns
if ((Get-DPINetworkHash $reversed) -ne $hash) { throw 'Unordered OS enumeration changed network identity' }
$addresses[1].AddressState='Tentative';$addresses[1].IPAddress='192.168.1.51';$routes[1].NextHop='192.168.1.254'
if ((Get-DPINetworkHash (Convert-DPINetworkSnapshot $adapters $addresses $routes $interfaces $dns)) -ne $hash) { throw 'Disconnected WiFi changed active network identity' }
$routes[0].NextHop='10.10.10.2'
if ((Get-DPINetworkHash (Convert-DPINetworkSnapshot $adapters $addresses $routes $interfaces $dns)) -eq $hash) { throw 'Active gateway change was missed' }
$routes[0].NextHop='10.10.10.1';$interfaces[0].InterfaceMetric=50
if ((Get-DPINetworkHash (Convert-DPINetworkSnapshot $adapters $addresses $routes $interfaces $dns)) -eq $hash) { throw 'Active interface metric change was missed' }
$interfaces[0].InterfaceMetric=25;$dns[0].ServerAddresses=@('1.1.1.1','10.10.10.1')
if ((Get-DPINetworkHash (Convert-DPINetworkSnapshot $adapters $addresses $routes $interfaces $dns)) -eq $hash) { throw 'DNS priority change was missed' }
$dns[0].ServerAddresses=@('10.10.10.1','1.1.1.1')
$specific=@($routes)+@([pscustomobject]@{InterfaceIndex=34;DestinationPrefix='104.16.1.2/32';NextHop='10.10.10.3';RouteMetric=1})
if ((Get-DPINetworkHash (Convert-DPINetworkSnapshot $adapters $addresses $specific $interfaces $dns)) -eq $hash) { throw 'Specific target route change was missed' }
$tunnel=@($interfaces)+@([pscustomobject]@{InterfaceIndex=50;AddressFamily='IPv4';InterfaceMetric=1;ConnectionState='Connected'})
$vpnRoutes=@($routes)+@([pscustomobject]@{InterfaceIndex=50;DestinationPrefix='0.0.0.0/1';NextHop='0.0.0.0';RouteMetric=1})
if ((Get-DPINetworkHash (Convert-DPINetworkSnapshot $adapters $addresses $vpnRoutes $tunnel $dns)) -eq $hash) { throw 'Connected VPN missing from adapter enumeration was ignored' }
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'tools\TelegramProbe.ps1')
$script:checkpointCount=0;$cancelled=$false
try { [void](Invoke-TelegramProbe -Root '.' -Dc 1 -Address '149.154.175.50' -Transport tcp-obfuscated -CheckStop { throw 'Controller network changed' }) } catch [OperationCanceledException] { $cancelled=$_.Exception.Message.Contains('Controller network changed') }
if (-not $cancelled) { throw 'Controller exception was swallowed as a Telegram connection failure' }
Write-Host 'Active network identity and Telegram controller cancellation tests passed.'
