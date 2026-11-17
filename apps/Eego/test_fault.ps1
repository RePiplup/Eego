$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$kit='D:/HandmadeBT-Qt/sdk/6.8.3/mingw_64'
$mingw='D:/HandmadeBT-Qt/sdk/Tools/mingw1310_64/bin'
$cmake=Join-Path $root 'tools/python-tools/cmake/data/bin/cmake.exe'
$env:Path="$kit/bin;$mingw;$root/tools/python-tools/ninja-1.11.1.3.data/scripts;$env:Path"
$build=Join-Path $root 'build/qt-tests'
& $cmake -S $PSScriptRoot -B $build -G Ninja "-DCMAKE_PREFIX_PATH=$kit" "-DCMAKE_CXX_COMPILER=$mingw/g++.exe" -DCMAKE_BUILD_TYPE=Release -DEEGO_BUILD_SERVICE_TEST=ON
if ($LASTEXITCODE) { throw 'Qt test configuration failed' }
& $cmake --build $build --target eego_service_test
if ($LASTEXITCODE) { throw 'Qt test build failed' }
$run=Join-Path $root ('artifacts/test-runs/eego-next/fault-'+[DateTimeOffset]::Now.ToUnixTimeMilliseconds())
New-Item -ItemType Directory -Path "$run/denied" -Force | Out-Null
$target=(Resolve-Path "$run/denied").Path
$acl=Get-Acl -LiteralPath $target
$saved=$acl.GetSecurityDescriptorSddlForm('All')
$sid=[System.Security.Principal.WindowsIdentity]::GetCurrent().User
$acl.AddAccessRule([System.Security.AccessControl.FileSystemAccessRule]::new($sid,'Write','Deny'))
Set-Acl -LiteralPath $target -AclObject $acl
try {
 $env:Path='D:\HandmadeBT-Qt\sdk\6.8.3\mingw_64\bin;D:\HandmadeBT-Qt\sdk\Tools\mingw1310_64\bin;'+$env:Path
 & "$build/eego_service_test.exe" "$run/library" 'C:\ProgramData\anaconda3\python.exe' "$root/apps/Eego/delay_proxy.py" "$root/dist/Eego/core/task_service_host_r3.exe" $target | Tee-Object "$run/result.txt"
 $testExit=$LASTEXITCODE
 @{exitCode=$testExit;run=$run;aclRestored=$false}|ConvertTo-Json|Set-Content "$run/result.json"
} finally {
 $restore=Get-Acl -LiteralPath $target;$restore.SetSecurityDescriptorSddlForm($saved);Set-Acl -LiteralPath $target -AclObject $restore
}
@{exitCode=$testExit;run=$run;aclRestored=$true}|ConvertTo-Json|Set-Content "$run/result.json"
Write-Output "$run : $testExit"
exit $testExit
