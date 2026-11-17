# Windows 原生桌面交付 · 2026-11-02

## 交付入口

- EXE：`D:\CXworkspace\HandmadeBT\dist\HandmadeBT-0.9.0-desktop-win64\HandmadeBT.exe`
- 完整便携 ZIP：`D:\CXworkspace\HandmadeBT\dist\HandmadeBT-0.9.0-desktop-win64.zip`
- Windows 10/11 x64；完整解压到可写目录，保留 EXE 旁两个 DLL，双击即可。
- 原生 Win32 窗口；界面已编译进 EXE，真实 download_core / TaskService 直接链接到进程。无浏览器、WebView、开发服务器、Python/Node 运行依赖。两个非系统运行库已随包提供，无额外安装步骤。
- 默认数据位于 EXE 同目录 userdata；未完成任务使用绝对路径，暂不支持移动目录后自动修复路径。
- 普通界面没有模拟开关、测试下载按钮和 TaskService 开发标签。

## 实际包验收（与历史内核测试分开）

从发布 ZIP 全新解压到带中文、空格的独立目录。仅保留 System32 PATH，在无关工作目录、无参数启动；启动帮助进程立即退出，客户端独立运行。验收文件不加入发布 ZIP。

| 验收 | 结果 |
|---|---|
| 独立原生窗口、无开发终端依赖 | 通过，实际从解压后的 EXE 启动；PE 为 WINDOWS_GUI 子系统 |
| 实际界面添加 HTTP 下载 | 通过，点击添加后接收真实 HTTP Range 数据 |
| 文件正确性 | 通过，8,388,645 字节；独立 SHA256 e7a33b82017b7a58935b44644dfb6cf2be6e3bc9446881994d4d10c0099211c2 与源一致 |
| 暂停 | 通过，第二笔下载暂停在 1,048,576 / 8,388,645 字节；落盘记录匹配 |
| 强制退出后重开 | 任务与断点保留通过；新进程 PID 16136，界面恢复两条任务，第二笔已暂停 12.5% |
| 继续至完成 | 通过；解锁后在实际 EXE 点击恢复，完成 8,388,645 字节，独立 SHA256 与源一致 |
| 本包公网 HTTPS / BT / Magnet / DHT-only | 本轮未重新实测；历史内核证据不能替代本包验收 |

测试 HTTP 服务只是下载源，在独立测试目录运行，不属于产品依赖；产品无需启动该服务，直接添加用户自己的有效下载链接即可。首笔下载完成后才点击到暂停，未把那次无效点击计为暂停成功；第二笔限速下载才用于有效暂停验收。

证据：`acceptance.json`（发布文件完整哈希核验与输出 SHA256）、`launch-before-restart.json` / `launch-evidence.json`（无开发 PATH 启动）、`desktop-downloading.png` / `desktop-paused.png`（真实窗口）。重启后锁屏截图未作为产品截图使用。

## 统一内核架构

本轮新增 handmadebt_desktop 构建目标，复用现有原生 GUI 和同一 download_core，不另起一套下载实现。HTTP 经 WinHTTP/既有解析及公共 FileStore，BT 经 libtorrent 自定义磁盘接口进入同一 FileStore。保留 TaskService 的快照、事件与恢复提示；GUI 显示用户恢复提示，去除开发诊断内容。

统一内核目标仍未全部完成：原生 GUI 部分展示经 legacy_tasks 读取内部对象；HTTP/BT 的运行时、恢复格式和错误类型尚未全面统一；当前仍单活跃任务队列。不能把桌面打包成功等同于架构完成。此前 DHT-only 发现限时未通过的问题没有在本轮解决。

## 试用

1. 双击 EXE（或先完整解压 ZIP）。
2. 粘贴有效 HTTP/HTTPS/磁力链接，选择目录，点击添加下载。
3. 选中任务暂停；重开 EXE 后选中未完成任务，点击恢复。完成后点打开保存目录。

## 下一步

本包继续下载和最终哈希验收已完成；下一步完成公网 HTTPS 与真实 Magnet 文件选择/BT 下载回归。后续 UI 改进以这个独立桌面入口为交付，不再扩展网页预览。继续收拢 legacy 访问及定位 DHT-only。

解锁后补验：实际点击恢复选中，第二笔任务完成。独立读取 2-slow.bin 核验大小及 SHA256 均一致。完成截图见 desktop-resumed-complete.png；本次未改 EXE，原便携包仍为已验收版本。
