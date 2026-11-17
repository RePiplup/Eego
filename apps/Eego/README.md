# Eego Qt 桌面版

界面使用 Qt Quick、Qt Quick Controls 与动态 Qt DLL。C++ 启动同包的 core/task_service_host_r3.exe，通过标准输入输出交换一行一个 UTF-8 JSON 对象。下载、暂停、重试、校验和重启加载均由原 TaskService 提供。运行时无需浏览器或 Python。

用 `--data-dir` 指向隔离任务库进行测试。

## 构建

本次使用 aqtinstall 3.3.0（PyPI，独立虚拟环境）、Qt 6.8.3 win64_mingw 与 MinGW-w64 GCC 13.1.0。编译下载服务仍使用原项目的 llvm-mingw、libc++、CMake 和 Ninja。Qt UI 和核心各自动态链接其运行库。

在完整项目根目录运行以下 PowerShell 命令。工具目录可以替换。构建脚本只调整当前进程 PATH。分发包的 sources/HandmadeBT-source.zip 提供应用和核心源码；项目的工具目录需按已有工程流程准备。

```powershell
.\apps/Eego\build.ps1 -QtKit 'D:\HandmadeBT-Qt\sdk\6.8.3\mingw_64' -MingwBin 'D:\HandmadeBT-Qt\sdk\Tools\mingw1310_64\bin'
```

脚本使用 apps/Eego、共享内核源码和 Qt SDK 生成 dist/Eego.zip。使用 -Output 指定其他 ZIP 路径。构建与部署使用临时目录，每次只更新指定归档。已安装版本、任务库和任务库选择文件独立于发布包。

根目录 Eego.exe 是定位 dist/Eego 的 Win32 启动器。变更安装布局后，使用 Qt 配套的 MinGW 编译它；便携 ZIP 的入口为包内 HandmadeBTQuick.exe。

```powershell
& "D:/HandmadeBT-Qt/sdk/Tools/mingw1310_64/bin/g++.exe" apps/Eego/launcher.cpp -municode -mwindows -static -s -Os -o Eego.exe
```

项目内的 accept.py 使用独立 Windows 桌面、UI Automation 和本地 HTTP 源执行界面测试，参数是待测 EXE 的绝对或相对路径。测试脚本依赖 Python、pywin32、Pillow、psutil 和 PowerShell。它们仅在测试机使用。

[Qt 6.8.3 Windows 开源包](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_683/qt6_683/qt.qt6.683.win64_mingw/) · [配套 MinGW](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/tools_mingw1310/qt.tools.win64_mingw1310/) · [对应源码](https://download.qt.io/archive/qt/6.8/6.8.3/submodules/) · [aqtinstall](https://pypi.org/project/aqtinstall/3.3.0/)

## 许可证及可替换性

UI 源码采用 GPL-2.0-or-later。发布时保留原核心、aria2 派生代码及其他原组件的许可证和源码义务。Qt 的商业授权不会改变这些义务。采用 Qt 开源组件时应按实际选入的各模块及第三方依赖附带许可证和版权通知；对于 LGPLv3 库，应提供该构建对应的完整源码或符合条件的书面提供方式，并说明替换共享库和重新构建、链接、运行程序的步骤。保留 DLL 和模块目录结构，允许以兼容版本替换，不施加签名锁定。

参考 [Qt 官方开源安装入口](https://www.qt.io/development/download-qt-installer-oss) 和 [Qt 对 GPL/LGPL 义务的说明](https://www.qt.io/development/open-source-lgpl-obligations)。
