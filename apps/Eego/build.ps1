param(
  [string]$QtKit = 'D:/HandmadeBT-Qt/sdk/6.8.3/mingw_64',
  [string]$MingwBin = 'D:/HandmadeBT-Qt/sdk/Tools/mingw1310_64/bin',
  [string]$Output
)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (!$Output) { $Output = Join-Path $root 'dist/Eego.zip' }
$Output = [IO.Path]::GetFullPath($Output)
[void][IO.Directory]::CreateDirectory((Split-Path $Output -Parent))
$source = $root
$cmake = Join-Path $root 'tools/python-tools/cmake/data/bin/cmake.exe'
$ninja = Join-Path $root 'tools/python-tools/ninja-1.11.1.3.data/scripts'
$originalPath = $env:Path
$build = Join-Path $root ('.eego-build-' + [guid]::NewGuid())
[void][IO.Directory]::CreateDirectory($build)
function Invoke-Checked([string]$program, [string[]]$arguments) {
  & $program @arguments
  if ($LASTEXITCODE) { throw "$program returned $LASTEXITCODE" }
}
try {
  $env:Path = "$root/tools/llvm-mingw-20261020-ucrt-x86_64/bin;$ninja;$originalPath"
  Invoke-Checked $cmake @('-S',$source,'-B',"$build/core",'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DHBT_EEGO_CORE=ON','-DCMAKE_C_COMPILER=clang','-DCMAKE_CXX_COMPILER=clang++')
  Invoke-Checked $cmake @('--build',"$build/core",'--target','task_service_host','core_cli','eego_browser_host','-j','8')
  $env:Path = "$QtKit/bin;$MingwBin;$ninja;$originalPath"
  Invoke-Checked $cmake @('-S',$PSScriptRoot,'-B',"$build/ui",'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_PREFIX_PATH=$QtKit","-DCMAKE_CXX_COMPILER=$MingwBin/g++.exe")
  Invoke-Checked $cmake @('--build',"$build/ui",'--target','HandmadeBTQuick','-j','4')
  Invoke-Checked 'python' @("$PSScriptRoot/package.py",'--build',$build,'--qt',$QtKit,'--mingw',$MingwBin,'--output',$Output)
} finally {
  $env:Path = $originalPath
  $resolved = (Resolve-Path -LiteralPath $build).Path
  if ((Split-Path $resolved -Parent) -eq $root -and (Split-Path $resolved -Leaf) -match '^\.eego-build-[0-9a-f-]{36}$') {
    Remove-Item -LiteralPath $resolved -Recurse -Force
  }
}
