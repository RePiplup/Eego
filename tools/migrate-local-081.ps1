$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$old = Join-Path $root 'dist/HandmadeBT-0.7-task-management-win64/userdata'
$previous = Join-Path $root 'dist/HandmadeBT-0.8-download-manager-win64/userdata'
$release = Join-Path $root 'dist/HandmadeBT-0.8.1-safe-redirect-win64'
$destination = Join-Path $release 'userdata'
if (!(Test-Path -LiteralPath (Join-Path $release 'manifest.json'))) { throw 'Release is not yet packaged' }
if (Test-Path -LiteralPath (Join-Path $destination 'tasks.record')) { throw 'Destination already contains tasks; refusing overwrite' }
$held = @()
try {
    foreach ($dir in @($old,$previous)) {
        $held += [IO.File]::Open((Join-Path $dir 'ui.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    }
    $other = Get-Content -LiteralPath (Join-Path $previous 'tasks.record')
    if ($other[1] -notmatch '^GUI2 \d+ 0$') { throw '0.8 has tasks: merge required; no data was overwritten' }
    $backup = Join-Path $root ('backups/upgrade-081-' + (Get-Date -Format yyyyMMdd-HHmmss))
    New-Item -ItemType Directory -Path $backup | Out-Null
    Copy-Item -LiteralPath (Join-Path $old 'tasks.record') -Destination (Join-Path $backup 'tasks-07.record')
    Copy-Item -LiteralPath (Join-Path $previous 'tasks.record') -Destination (Join-Path $backup 'tasks-08.record')
    $before = @{}
    foreach ($file in Get-ChildItem -LiteralPath $old -File -Recurse) { $before[$file.FullName] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256 -ErrorAction SilentlyContinue).Hash }
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    $held += [IO.File]::Open((Join-Path $destination 'ui.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    Copy-Item -LiteralPath (Join-Path $backup 'tasks-07.record') -Destination (Join-Path $destination 'tasks.record')
    Copy-Item -LiteralPath (Join-Path $backup 'tasks-07.record') -Destination (Join-Path $destination 'tasks.record.before-081')
    foreach ($file in Get-ChildItem -LiteralPath $old -File -Filter 'ownership-*.record') { Copy-Item -LiteralPath $file.FullName -Destination $destination }
    $hash = (Get-FileHash -LiteralPath (Join-Path $destination 'tasks.record') -Algorithm SHA256).Hash
    if ($hash -ne (Get-FileHash -LiteralPath (Join-Path $backup 'tasks-07.record') -Algorithm SHA256).Hash) { throw 'Task copy checksum mismatch' }
    foreach ($file in Get-ChildItem -LiteralPath $old -File -Recurse) {
        if ($file.Name -eq 'ui.lock') { continue }
        if ((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne $before[$file.FullName]) { throw 'Source changed during migration' }
    }
    @{backup=$backup;source=$old;destination=$destination;task_record_sha256=$hash;old_files=$before;old_files_unchanged=$true} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $root 'docs/results/migration-081.json') -Encoding UTF8
} finally { foreach ($handle in $held) { $handle.Dispose() } }
& (Join-Path $release 'browser/Register-Browser.ps1')
if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw 'Browser registration failed' }
foreach ($key in @('Software\Google\Chrome\NativeMessagingHosts\org.handmadebt.bridge','Software\Microsoft\Edge\NativeMessagingHosts\org.handmadebt.bridge')) {
    $handle = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($key)
    try { if ($handle.GetValue('') -ne (Join-Path $release 'browser/native-host.json')) { throw 'Native registration mismatch' } } finally { $handle.Dispose() }
}
Write-Output 'Migrated current tasks without moving files; Chrome/Edge native host now uses 0.8.1.'
