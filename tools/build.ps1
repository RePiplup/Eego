$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$root = (Get-Location).Path
$env:PATH = "$root/tools/llvm-mingw-20261020-ucrt-x86_64/bin;$root/tools/python-tools/ninja-1.11.1.3.data/scripts;$env:PATH"
$cmake = "$root/tools/python-tools/cmake/data/bin/cmake.exe"
& $cmake -S . -B build -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_C_COMPILER=clang' '-DCMAKE_CXX_COMPILER=clang++'
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
& $cmake --build build -j 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
Copy-Item "$root/tools/llvm-mingw-20261020-ucrt-x86_64/bin/libc++.dll","$root/tools/llvm-mingw-20261020-ucrt-x86_64/bin/libunwind.dll" -Destination build
& "$root/tools/python-tools/cmake/data/bin/ctest.exe" --test-dir build --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
