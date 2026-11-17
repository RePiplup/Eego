param([string]$ExtensionDirectory)
$ErrorActionPreference = 'Stop'
$package = Split-Path $PSScriptRoot -Parent
$source = Join-Path $PSScriptRoot 'extension'
$desktop = Get-Item -LiteralPath (Join-Path $package 'HandmadeBT.exe')
$bridge = Get-Item -LiteralPath (Join-Path $package 'handmadebt_host.exe')
$manifest = Get-Content -LiteralPath (Join-Path $source 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$extensionId = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'extension-id.txt') -Raw).Trim()
if ($extensionId -notmatch '^[a-p]{32}$') { throw 'Invalid extension ID' }
$settings = 'HKCU:\Software\HandmadeBT\EdgeAssistant'
if (!$ExtensionDirectory) { $ExtensionDirectory = $source }
$dest = [IO.Path]::GetFullPath($ExtensionDirectory)
if (Test-Path -LiteralPath $dest) {
    $current = Join-Path $dest 'manifest.json'
    if (Test-Path -LiteralPath $current) {
        $identity = Get-Content -LiteralPath $current -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($identity.key -ne $manifest.key) { throw 'Destination belongs to a different extension.' }
    } elseif (@(Get-ChildItem -LiteralPath $dest -Force).Count) { throw 'Destination must be empty or contain this extension.' }
} else { [void][IO.Directory]::CreateDirectory($dest) }
# Publish the manifest last. Edge keeps the running extension until it is reloaded.
if ($dest -ne $source) {
    $files = @(Get-ChildItem -LiteralPath $source -File | Sort-Object { $_.Name -eq 'manifest.json' })
    foreach ($file in $files) { Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $dest $file.Name) -Force }
}
$nativeManifest = Join-Path $PSScriptRoot 'edge-native-host.json'
$native = @{name='org.handmadebt.bridge';description='HandmadeBT Edge bridge';type='stdio';path=$bridge.FullName;allowed_origins=@("chrome-extension://$extensionId/")}
[IO.File]::WriteAllText($nativeManifest,($native | ConvertTo-Json),(New-Object Text.UTF8Encoding($false)))
$key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Software\Microsoft\Edge\NativeMessagingHosts\org.handmadebt.bridge')
try { $key.SetValue('', $nativeManifest) } finally { $key.Dispose() }
New-Item -Path $settings -Force | Out-Null
Set-ItemProperty -LiteralPath $settings -Name ExtensionDirectory -Value $dest
Write-Host "HandmadeBT $($desktop.VersionInfo.FileVersion) | Edge extension $($manifest.version): $dest"
Write-Host "For first use, load the unpacked directory shown above at edge://extensions. Future updates reload automatically."
