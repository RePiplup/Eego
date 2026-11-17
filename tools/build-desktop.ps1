$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Set-Location $projectRoot
$env:PATH = "$projectRoot/tools/llvm-mingw-20261020-ucrt-x86_64/bin;$projectRoot/tools/python-tools/ninja-1.11.1.3.data/scripts;$env:PATH"
$desktopCmake = "$projectRoot/tools/python-tools/cmake/data/bin/cmake.exe"
& $desktopCmake -S . -B build/desktop -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_C_COMPILER=clang' '-DCMAKE_CXX_COMPILER=clang++'
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
& $desktopCmake --build build/desktop --target handmadebt_desktop desktop_bridge -j 4
if ($LASTEXITCODE -ne 0) { throw 'Desktop build failed' }
