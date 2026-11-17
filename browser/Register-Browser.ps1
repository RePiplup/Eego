param([switch]$Unregister)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$state = Join-Path $PSScriptRoot 'registration-backup.json'
if ($Unregister) {
    if (!(Test-Path -LiteralPath $state)) { Write-Host 'No registration backup.'; exit 0 }
    $entries = @(Get-Content -LiteralPath $state -Raw | ConvertFrom-Json)
    foreach ($entry in $entries) {
        $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($entry.key, $true)
        if ($null -eq $key) { continue }
        try {
            if ($key.GetValue($entry.name, $null) -ne $entry.installed) { continue }
            if ($entry.existed) { $key.SetValue($entry.name, $entry.previous, [Enum]::Parse([Microsoft.Win32.RegistryValueKind],$entry.kind)) }
            else { $key.DeleteValue($entry.name, $false) }
        } finally { $key.Dispose() }
    }
    Remove-Item -LiteralPath $state
    Write-Host 'HandmadeBT registration removed; previous values restored.'
    exit 0
}
$exe = Join-Path $root 'bin\handmadebt.exe'
$hostExe = Join-Path $root 'bin\handmadebt_host.exe'
if (Test-Path -LiteralPath (Join-Path $root 'HandmadeBT.exe')) {
    $exe = Join-Path $root 'HandmadeBT.exe'
    $hostExe = Join-Path $root 'handmadebt_host.exe'
}
if (!(Test-Path -LiteralPath $exe) -or !(Test-Path -LiteralPath $hostExe)) { throw 'Run this script from the complete portable package.' }
$id = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'extension-id.txt') -Raw).Trim()
if ($id -notmatch '^[a-p]{32}$') { throw 'Invalid extension identity' }
$manifest = Join-Path $PSScriptRoot 'native-host.json'
$json = @{name='org.handmadebt.bridge';description='HandmadeBT task handoff';path=$hostExe;type='stdio';allowed_origins=@("chrome-extension://$id/")} | ConvertTo-Json
[IO.File]::WriteAllText($manifest, $json, (New-Object Text.UTF8Encoding($false)))
$entries = New-Object System.Collections.Generic.List[object]
if (Test-Path -LiteralPath $state) { foreach ($e in @(Get-Content -LiteralPath $state -Raw | ConvertFrom-Json)) { $entries.Add($e) } }
function Set-OwnedValue([string]$keyPath, [string]$name, [string]$value) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($keyPath)
    try {
        $existing = @($entries | Where-Object { $_.key -eq $keyPath -and $_.name -eq $name })
        if ($existing.Count -eq 0) {
            $existed = $key.GetValueNames() -contains $name
            $entries.Add([pscustomobject]@{key=$keyPath;name=$name;existed=$existed;previous=$key.GetValue($name,$null);kind=$(if($existed){$key.GetValueKind($name).ToString()}else{'String'});installed=$value})
        } else { $existing[0].installed = $value }
        # Save rollback information before modifying the registry value.
        ConvertTo-Json -InputObject @($entries.ToArray()) -Depth 5 | Set-Content -LiteralPath $state -Encoding UTF8
        $key.SetValue($name,$value,[Microsoft.Win32.RegistryValueKind]::String)
    } finally { $key.Dispose() }
}
Set-OwnedValue 'Software\Google\Chrome\NativeMessagingHosts\org.handmadebt.bridge' '' $manifest
Set-OwnedValue 'Software\Microsoft\Edge\NativeMessagingHosts\org.handmadebt.bridge' '' $manifest
$command = '"' + $exe + '" --protocol "%1"'
Set-OwnedValue 'Software\Classes\HandmadeBT.Magnet' '' 'HandmadeBT Magnet'
Set-OwnedValue 'Software\Classes\HandmadeBT.Magnet' 'URL Protocol' ''
Set-OwnedValue 'Software\Classes\HandmadeBT.Magnet\shell\open\command' '' $command
Set-OwnedValue 'Software\HandmadeBT\Capabilities' 'ApplicationName' 'HandmadeBT'
Set-OwnedValue 'Software\HandmadeBT\Capabilities' 'ApplicationDescription' 'HTTP HTTPS and Magnet downloads'
Set-OwnedValue 'Software\HandmadeBT\Capabilities\URLAssociations' 'magnet' 'HandmadeBT.Magnet'
Set-OwnedValue 'Software\RegisteredApplications' 'HandmadeBT' 'Software\HandmadeBT\Capabilities'
Set-OwnedValue 'Software\Classes\handmadebt' '' 'URL:HandmadeBT'
Set-OwnedValue 'Software\Classes\handmadebt' 'URL Protocol' ''
Set-OwnedValue 'Software\Classes\handmadebt\shell\open\command' '' $command
Write-Host 'Registered for current user; administrator rights are not needed.'
Write-Host 'Load browser\extension as an unpacked extension in Edge / Chrome.'
Write-Host 'Existing default Magnet application has NOT been changed.'
Write-Host 'For direct Magnet clicks, choose HandmadeBT for MAGNET in Windows Default apps.'
Write-Host "Extension folder: $(Join-Path $PSScriptRoot 'extension')"
