param(
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [string]$Package,
    [string]$Compiler
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$Package) { $Package = Join-Path $root 'dist/Eego.zip' }
if (!$Compiler) {
    $Compiler = @(
        (Join-Path $env:LOCALAPPDATA 'Programs/Inno Setup 6/ISCC.exe')
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe')
    ) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (!$Compiler) { throw 'Install Inno Setup 6 or pass -Compiler.' }
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist ('installer-stage-' + [guid]::NewGuid())
[void][IO.Directory]::CreateDirectory($stage)
try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory((Resolve-Path -LiteralPath $Package).Path, $stage)
    $payload = Join-Path $stage 'Eego'
    $manifest = Get-Content -Raw -LiteralPath "$payload/manifest.json" | ConvertFrom-Json
    $entries = $manifest.files.PSObject.Properties
    foreach ($file in $entries) {
        if ((Get-FileHash -LiteralPath (Join-Path $payload $file.Name)).Hash -ne $file.Value) {
            throw "Payload checksum mismatch for $($file.Name)"
        }
    }
    foreach ($name in 'userdata','Downloads','rollback','library-path.txt','library-continuity.json') {
        if (Test-Path -LiteralPath (Join-Path $payload $name)) { throw "User data in package: $name" }
    }
    if (@(Get-ChildItem -LiteralPath $payload -File -Recurse -Force).Count -ne (@($entries).Count + 1)) {
        throw 'Unexpected files outside the package manifest.'
    }
    & $Compiler "/DPayload=$payload" "/DReleaseVersion=$Version" "/O$dist" "$root/packaging/Eego.iss"
    if ($LASTEXITCODE) { throw "ISCC returned $LASTEXITCODE" }
    $name = "Eego-$Version-windows-x64-setup.exe"
    $hash = (Get-FileHash -LiteralPath "$dist/$name").Hash.ToLowerInvariant()
    [IO.File]::WriteAllText("$dist/$name.sha256", "$hash  $name`n")
} finally {
    $resolved = (Resolve-Path -LiteralPath $stage).Path
    if ((Split-Path $resolved -Parent) -eq $dist -and (Split-Path $resolved -Leaf) -match '^installer-stage-[0-9a-f-]{36}$') {
        Remove-Item -LiteralPath $resolved -Force -Recurse
    }
}
