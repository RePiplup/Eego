# Eego · Windows 桌面下载客户端

从 [GitHub Releases](https://github.com/RePiplup/Eego/releases/latest) 下载文件名以 `windows-x64-setup.exe` 结尾的安装包。运行后从开始菜单打开 **Eego**。首次接管浏览器下载时，从开始菜单运行 **Browser Setup**，按照窗口提示加载扩展，再在扩展中启用接管。

**升级**　先退出 Eego，再运行新版安装包。安装程序沿用已有安装位置，任务记录、下载文件和已加载的扩展路径均可继续使用。从便携版转为安装版时，选择原来包含 `HandmadeBTQuick.exe` 的目录，以继续使用那里的任务库和浏览器设置。另选目录会创建独立安装。

**维护者发版**　在完成下文的依赖准备后运行 `./apps/Eego/build.ps1`，再运行 `./tools/build-installer.ps1 -Version 0.10.0`（后续替换为新版本号，需 Inno Setup 6）。测试首次安装和覆盖升级，提交源码并推送，为该提交创建并推送同名版本标签（例如 `v0.10.0`）。在 GitHub Release 附上 `dist` 下对应版本的安装程序、`.sha256` 和更新说明。

## 本地开发工作区

当前正式启动入口：双击根目录 Eego.exe（原生 Windows 无控制台启动器）。它启动现有统一版，固定活动库为 D:/CXworkspace/HandmadeBT-data/production。请保留现有应用目录和 Qt 依赖文件；这不是单文件应用。旧脚本仅保留兼容。

磁力任务取得清单后自动弹出文件选择，勾选并确认后开始下载。

**用 Edge 或 Chrome 接管下载。** 首次使用时运行 [Eego 浏览器设置](dist/Eego/Browser-Setup.cmd)，在浏览器扩展管理页开启开发者模式，选择“加载解压缩的扩展”，指向 `dist/Eego/browser/extension`。打开“Eego 下载助手”，勾选“尝试接管下载”，再到网页点击文件链接。交接成功后可在 Eego 任务窗口查看进度。网页生成的 `blob:` 文件由浏览器保存。

**升级。** 桌面端与扩展独立发版。已有用户从扩展 0.9.6 升到 0.9.7 时，请在扩展管理页重新加载一次。此后将新版覆盖到同一目录，助手会定期检查扩展文件是否变更，并在自动接管结束后重新加载。迁移安装目录时，重新运行新目录中的浏览器设置脚本，并从新位置加载扩展。

普通操作：粘贴链接 → 添加下载 → 选中任务暂停/恢复。重启后未完成任务保留为暂停。任务库位置可从“任务库”菜单查看；有未完成任务时不要移动目录。实测范围和已知限制见 [桌面交付验收](docs/acceptance/handmadebt-desktop.md)。

旧版桌面 UI 保留 [HandmadeBT 0.9.6 便携版](dist/HandmadeBT/HandmadeBT.exe)，使用该包自己的任务库。新版桌面 UI 使用根目录 [Eego.exe](Eego.exe)。根目录的 HandmadeBT.cmd 也启动同一份新版程序。

### 桌面版打包与启动检查

在项目根目录运行。默认输出分别为 `dist/HandmadeBT.zip` 和 `dist/Eego.zip`。可以分别用 `--output` 或 `-Output` 指定发布位置。两种打包流程各自在临时目录编译和组装，不读取已安装程序、旧发布包或任务库。

```powershell
python tools/package-desktop.py
./apps/Eego/build.ps1
python tests/desktop_startup.py
```

旧版 Win32 UI 的源码在 `apps/HandmadeBT`，程序在 `dist/HandmadeBT`。新版 Qt UI 的源码在 `apps/Eego`，程序在 `dist/Eego`。两个版本共用根目录的依赖和 `src`，新版专用的内核差异在 `apps/Eego/core`。旧预览留下的 `dist/*/userdata` 仍在原路径，用于保留任务、下载和恢复记录。

第二、三轮网页预览的开发和验收记录见 [第二轮交付](docs/acceptance/round-2.md) 和 [第三轮交付](docs/acceptance/round-3.md)。

### 回归测试和数据目录

`tests/eego` 保留并发调度、边界条件和批量移除回归。正式验收记录归在 `docs/acceptance`。新测试的本地输出默认写入 `artifacts/test-runs`。例如：

```powershell
python tests/eego/accept_edges.py
python tests/published_recovery_test.py --host dist/Eego/core/task_service_host_r3.exe
```

历史任务库、下载及停机迁移的完整回滚备份保留原路径，包括 `workstreams/library-audit-20261112/production-migration-0815f025`。该备份的 `backup-manifest.json` 记录了文件路径和 SHA-256。

## 内核架构与协议限制

历史版本的浏览器设置见 [MAGNET_BROWSER.md](docs/MAGNET_BROWSER.md)，HTTP 界面基础操作见 [GUI_QUICKSTART.md](docs/GUI_QUICKSTART.md)。

这是路线 B 的可运行迁移验证，**不是完整下载软件，也尚未完成整个 BT 运行时融合**。

已经接通：aria2 派生 HTTP 响应/Range 解析 → WinHTTP 传输 → 公共 FileStore；libtorrent 原有 BT peer/picker/hash 流程 → 自定义 disk_interface → 同一个 FileStore。`mixed` 命令在一个进程内并行跑两种下载，使用同一个 FileStore 实例。

没有启动 aria2c，没有 aria2 RPC，没有完整 aria2 DownloadEngine。BT 下载自定义后端不调用 libtorrent 默认磁盘实现；默认后端只用于明确标识的做种基线与 `bt-baseline-get` 测试。

## 构建和测试

在项目目录的 PowerShell 中运行。已有构建可以直接用 `build/core_cli.exe`。

```powershell
python tools/bootstrap.py
./tools/build.ps1
python tests/integration.py --online
python tests/crash_recovery.py
```

首次 bootstrap 需要网络、Git 和 Python 3.12+，只向本项目写入便携工具；本机验证使用 Python 3.12.7。测试除 Python 标准库外无额外 Python 依赖。`--online` 增加公开 HTTPS 文件的证书及内容校验；不加则只跑本机受控测试。受限沙箱需要允许子进程连接本机 HTTP/BT 测试节点；不是关闭系统防火墙。

输入版本及 SHA-256 在 `provenance/inputs.lock.json`。bootstrap 遇到已有源码版本不符或归档哈希不符会停止，不重置已有工作。`tools/import_http.py` 是重现迁移文件和测试适配的脚本，日常构建无需重复运行。

## CLI

```powershell
# 最后一个可选参数：本轮下载若干完整 256 KiB 块后暂停；退出码 3 表示已保存暂停状态。
./build/core_cli.exe http "https://example.org/file.bin" "output.bin" 4 3
# 同一 URL、输出路径再次执行即可恢复。成功退出码 0，错误为 1。
./build/core_cli.exe http "https://example.org/file.bin" "output.bin" 4

# 使用自己生成的文件进行 BT 验证。做种命令在独立测试节点运行。
./build/core_cli.exe make-torrent "seed/fixture.bin" "fixture.torrent"
./build/core_cli.exe bt-seed "fixture.torrent" "seed" 49001
./build/core_cli.exe bt-get "fixture.torrent" "download" 49001 524288 3
./build/core_cli.exe bt-get "fixture.torrent" "download" 49001

# 同一个进程、同一个 FileStore，HTTP 和 BT 同时下载各自文件。
./build/core_cli.exe mixed "http://127.0.0.1:8000/fixture.bin" "http.bin" "fixture.torrent" "bt-output" 49001
```

示例 URL 需要替换；自动化测试自行创建服务、生成数据和做种，不需要用户准备文件。`bt-get` 目前只连接指定的本机受控 peer，尚未提供面向公网的任务提交功能。

## 当前限制

- HTTP：已知正长度文件，以及最多32 MiB的 chunked 报表。分段/恢复要求强 ETag；不支持 Range 的服务器可单连接完整下载，请求分段时明确报错。支持最多10次安全跳转，拒绝循环、URL凭据及 HTTPS 降级；HTML页面不标为文件完成。暂不支持浏览器登录会话、压缩正文、非chunked未知长度和零字节文件。HTTPS 使用 Windows 默认可信根、主机名和证书校验，未设置任何忽略验证选项。
- HTTP 的分段调度和 checkpoint 是本项目的新实现；**尚未迁移 aria2 的 SegmentMan、镜像选择、完整请求/重试/认证体系**。
- BT：GUI/CLI 已支持 v1 Magnet、多文件和中文路径；保留上游 session 作为内部子系统。尚未开放 v2/hybrid、文件改名/移动/删除/优先级操作。自定义存储遇到这些专用操作返回不支持，不能视为完整 BT 存储实现。
- BT 恢复先重新校验文件，不是已优化的快速恢复。测试包含暂停后改坏一个字节再恢复，最终 SHA-256 必须正确。
- FileStore：共享单线程磁盘队列和 Win32 文件 I/O，真实异步完成回调；没有默认磁盘后端代理。尚无统一 TaskManager、全局预算、路径安全完整策略和文件缓存优化。
- `32 KiB` 是 BT 写入背压的软触发水位，不是进程内存硬上限；已在网络和协议队列中的数据仍可能继续到达。
- 0.2 已验证下载中强杀、重启重校验、残损/错配记录拒绝和同名无关文件保护；尚未验证突然断电、所有磁盘故障、长时间公网负载。不要用于唯一副本或重要目录。
- 0.2 使用带 SHA-256 校验的恢复记录及文件归属标记，拒绝自动接管 0.1 的旧记录和无归属临时文件，保留原数据。HTTP 每完成两块合并保存，先刷数据再原子更新记录；BT 重启依旧全量校验已保存数据。强杀时遗留的 `.tmp-*` 不作为有效恢复记录。

详细迁移清单：[docs/MIGRATION.md](docs/MIGRATION.md)。实际结果：[docs/VALIDATION.md](docs/VALIDATION.md)。

便携测试包及一键自测说明：[docs/PORTABLE.md](docs/PORTABLE.md)。便携包附带 Python 和 C++ 运行库，不需要编译或安装开发工具。
本轮强杀恢复修复及证据：[docs/CRASH_RECOVERY.md](docs/CRASH_RECOVERY.md)。旧版包保留作历史基线；0.3 性能验收见 [docs/CHECKPOINT_PERFORMANCE.md](docs/CHECKPOINT_PERFORMANCE.md)。

## 许可

新项目代码按 GPL-2.0-or-later 提供；融合产物必须满足适用 GPL 条件。aria2 文件原始 GPL/OpenSSL 例外声明完整保留，BSD 来源和版权保留在源码与 `LICENSES/`。GPL 正文为 `COPYING`。第三方原有许可证不被本项目声明替代。工具链只作构建输入；分发运行时 DLL 时应一并带上相应 LLVM/MinGW 许可。未宣称已有完整正式发行包的合规审计。

0.4 中文目录及文件名修复、验收范围见 [docs/UNICODE_PATHS.md](docs/UNICODE_PATHS.md)。
