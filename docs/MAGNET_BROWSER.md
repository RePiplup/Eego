# HandmadeBT 0.6：Magnet 与浏览器交接

## 2026-10-27 公网界面收尾：本阶段验收完成

使用原发布版界面和隔离任务目录 `test-output/public-magnet-gui-06/userdata`，实际点击添加已验证 Sintel Magnet，下载中点击暂停，最终保存 46,399,488 / 129,302,391 字节（35.9%）。点击关闭后重开，同一任务保持暂停和相同进度，前后持久化记录逐字节一致。实际点击恢复后，全量分块重验确认 46,006,272 字节（35.6%，未完整分块重新下载），随后任务完成。

独立核验全部 11 文件的 SHA-256 与此前已验证公网样本完全一致，并再次逐块对照官方种子的全部 987 个哈希。任务完成状态、清单校验和、精确文件集合和总字节数均通过。原日常任务记录逐字节未变。文件提前预分配到完整长度，但暂停记录仍为部分完成；未使用 CLI 定时暂停，也未引入本机做种或 HTTP web seed。

证据：`docs/results/public-magnet-gui-06.json`，原始暂停、重开、完成记录在 `test-output/public-magnet-gui-06/`。最后窗口捕获工具两次报告 `failed to activate captured window`，按规则停止界面操作，未取得最终完成截图；完成结论使用软件自身保存的完成记录及独立文件核验，不以旧的界面文本代替。程序仍有响应。本轮未修改产品代码或重建包，61 项回归沿用上一轮刚通过的记录，不冒称本轮重跑。

独立重验命令（从项目根目录运行）：

```powershell
& C:/ProgramData/anaconda3/python.exe tests/verify_public_magnet_gui.py --run test-output/public-magnet-gui-06 --reference docs/results/public-magnet-06.json --torrent test-output/public-magnet-06/sintel-public.torrent --bundle dist/HandmadeBT-0.6-magnet-browser-win64
```

0.6 已约定的 HTTP/HTTPS、日常 Edge 交接、受控和公网 Magnet、按钮暂停/重开恢复与内容核验，本阶段验收至此结束。此结论不扩大为所有公网资源、所有浏览器、BT v2/hybrid 或视频流已支持；既有最终需求继续保留。

## 2026-10-27 公网小范围验收补充

发布版 CLI 使用与界面相同的内核，已完成不含本机节点、HTTP web seed 或 HTTP 元数据源的公网 Sintel Magnet 下载：11 文件、129,302,391 字节，独立读取全部内容核验 987 个官方种子分块哈希全部一致，并记录逐文件 SHA-256。程序正常结束；61 项原有测试本轮重新运行全部通过。产品代码和发布包未改动。详见 [公网验收记录](PUBLIC_MAGNET_06.md) 和 `docs/results/public-magnet-06.json`。未单独区分 DHT 与 Tracker 的贡献，也未重做公网资源的界面点击；下方日常浏览器记录的“公网未验收”是该次测试当时的范围说明。

## 2026-10-27 日常浏览器验收补充

HTTP 的真实网页点击、冷启动唤醒、62.5% 处按钮暂停、关闭重开、按钮恢复及独立 SHA-256 校验已通过，证据为 `docs/results/daily-browser-http-06.json`。

用户确认在日常 Edge 的真实右键菜单选择了“使用 HandmadeBT 下载链接”；持久化的第 2 个任务 URI 与受控测试资源相同。原做种进程有 30 分钟时限，跨轮后已停止。恢复原端口 55599 的做种节点并重试原任务后，元数据与载荷均成功接收。实际点击暂停时最终保存 524,288 / 4,194,335 字节（12.5%）；载荷文件已预分配 4,194,335 字节，但界面仍正确显示部分完成。再次关闭重开保留暂停状态，点击恢复后观测到进度增长至 2,670,623 字节，最终完成。

独立核验覆盖资源内全部两个文件（包括零字节文件），同时检查任务状态、任务/文件清单校验和及实际文件集合。证据为 `docs/results/daily-browser-magnet-06.json`，可重跑：

```powershell
python tests/verify_browser_magnet.py test-output/browser-magnet-click-3884dbbc/fixture.json --bundle dist/HandmadeBT-0.6-magnet-browser-win64 --task-id 2
```

本轮未修改或重建发布程序；GUI、CLI、本地连接程序和扩展 worker 的 SHA-256 与 0.6 包清单一致。既有 61 项测试是此前记录，本轮未冒称重新运行。临时网页与做种服务在验收后停止。第 3 个重复测试任务保持暂停、未删除，不计作已完成下载。公网 DHT/Tracker 发现、Chrome 实机和默认 Magnet 应用选择仍未完成验收；本次通过的是日常 Edge 与受控本机资源的完整链路。

## 直接使用

解压整个 `HandmadeBT-0.6-magnet-browser-win64.zip`，双击 `HandmadeBT.cmd`。
粘贴 HTTP、HTTPS 或 `magnet:?xt=urn:btih:...` 链接，点击添加下载。
每行一条，可混合添加。Magnet 本版支持 v1 单文件、多文件和中文路径；完成后文件在该任务目录的 `files` 下。任务目录名暂为 `编号-magnet`。
Magnet 无需填写单文件 SHA-256，软件先按种子分块哈希校验，再读回每个文件并生成 SHA-256 清单 `files.record`。
暂停、恢复、关闭重开使用同一套界面按钮和任务列表。恢复时重新检查实际数据，不信任预分配长度或上次进度。

链接由资源发布者提供：普通文件通常可右键网页的下载按钮复制链接；磁力链接通常标为 Magnet/磁力下载。网页地址不一定是文件直链。软件不内置资源搜索，也不会凭资源名称生成链接。
没有做种节点的磁力资源可能一直等待；元数据阶段等待三分钟仍无结果会报错，可重试。用户提供的 `63DDE13A0EBEABF4EE7E682563184ADE0CDBE3C4` 未作为验收资源下载，不能据本地受控测试保证该资源有种。

## 浏览器一次性设置（Edge / Chrome）

1. 将解压文件夹放到准备长期保留的位置，双击 `Browser-Setup.cmd`。无需管理员权限，注册当前用户的连接程序，保留旧注册值供撤销。
2. Edge 打开 `edge://extensions`，Chrome 打开 `chrome://extensions`；启用开发人员模式，选择“加载解压缩的扩展程序”，选本包 `browser/extension` 文件夹。
3. 网页上右键文件或磁力链接，选择“使用 HandmadeBT 下载链接”。软件未打开时会启动，已打开时加入现有任务列表。
4. 如需点击网页下载按钮后自动交给 HandmadeBT，在扩展按钮的面板里勾选“接管浏览器新开始的普通文件下载”。默认不勾选。启用后，扩展先暂停浏览器下载，收到任务已持久保存的确认才取消浏览器对应下载；交接失败则恢复浏览器下载。

浏览器可能显示正常的扩展/外部应用确认，需要用户自行确认。开发版扩展尚未上架商店，不能免去首次安装。测试只使用隔离浏览器配置，没有把扩展装入用户的日常浏览器。

直接左键点击 Magnet 并通过 Windows 默认应用唤醒：安装脚本登记了 HandmadeBT 的 Magnet 处理能力，但不会更改已有默认应用。可在 Windows 设置 → 应用 → 默认应用中为 MAGNET 选择 HandmadeBT。此 OS 默认选择流程本轮未代用户执行；不选择时仍可使用扩展右键交接或复制粘贴。

撤销当前包的注册：`powershell -NoProfile -ExecutionPolicy Bypass -File browser/Register-Browser.ps1 -Unregister`。
脚本只撤销仍指向本次安装的值，并恢复备份，不删除下载数据。扩展从浏览器扩展页移除。移动包后需重新运行设置；旧包仍在使用时不要同时注册多个版本。

## 当前明确限制

- BT v2/hybrid、按文件选择、做种管理、移动/改名仍未实现。保留较完整 libtorrent session 作为内部 BT 子系统，是源码融合的中间状态。
- 浏览器交接只提交链接，不传递 Cookie、Authorization、POST 表单或网页私有凭据。登录专用下载、blob、HLS/DASH、网页视频解析不在本轮支持范围。自动接管可随时关闭。
- HTTP 原有限制继续适用：重定向/未知长度/chunked/压缩正文等未接入。自动接管尽量使用浏览器提供的最终 URL；右键交接含重定向的原始 URL 仍可能报错。HTTPS 保持 Windows 证书验证。
- 收到交接确认意味着任务已保存，不保证远端随后一定下载成功；真实错误显示在任务列表。
- 网络发现启用 DHT/LSD，接受 Magnet tracker 和显式 peer；未启用 UPnP/NAT-PMP 自动端口映射。公网可达性依赖网络、资源做种状况和用户防火墙策略。

## 实现与来源

| 文件 | 实际职责与来源 |
|---|---|
| `src/magnet.cpp` | 新实现：上游 `parse_magnet_uri`、metadata exchange、session/torrent_handle；元数据阶段使用上游 disabled disk，载荷阶段必须用自定义磁盘；停止后读回分块及逐文件哈希 |
| `src/bt_files.hpp` | 新实现：上游 file_storage.map_block 将跨文件块映射到公共 FileStore；pad 文件补零；拒绝路径穿越、Windows 保留名称、重复路径、symlink/reparse 路径 |
| `src/bt_disk.cpp` | 原 BSD 衍生磁盘接口继续保留版权；原单文件读写扩展为映射读写，保留队列、回调线程与背压 |
| `src/gui.cpp` | 同进程调用 HTTP 或 Magnet 核心；用户按钮暂停、统一任务列表，WM_COPYDATA 保存成功后确认 |
| `src/browser_bridge.hpp` | URL 校验、命令行转义、启动互斥、窗口消息；只交接 URL，无下载后端 |
| `src/browser_host.cpp` | Native Messaging 长度帧、固定扩展来源校验；只做 IPC，不运行 CLI/aria2c/RPC，不访问下载数据 |
| `browser/extension/*` | 新 MV3 扩展：链接右键菜单、手工提交、可选 downloads.onCreated 接管及失败回退 |
| `browser/Register-Browser.ps1` | 当前用户 Chrome/Edge NativeMessagingHosts、独立 handmadebt: 协议和 Magnet 候选应用注册；可恢复旧值 |

新文件遵循项目 GPL-2.0-or-later；上游 BSD/GPL 许可和来源保持原样。工具链/上游基线不变，见 `provenance/inputs.lock.json`；发布包中的 `source.zip` 含应用、扩展、脚本、原始依赖源码。`provenance/package-inputs.json` 和 `manifest.json` 固定程序/源包内容，旧包不覆盖。
HTTP 继续使用已迁移 aria2 解析器；本轮没有引入完整 aria2 DownloadEngine。BT picker、choking、uTP、DHT 均保留上游实现。不同任务使用同一 FileStore 实现；GUI 目前顺序运行任务，未声称实现全局并发资源治理。

浏览器协议实现依据：[Chrome Native Messaging](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging)、[Downloads API](https://developer.chrome.com/docs/extensions/reference/api/downloads)、[Edge Native Messaging](https://learn.microsoft.com/en-us/microsoft-edge/extensions-chromium/developer-guide/native-messaging)。

## 可重现验证

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1
python tests/integration.py --online
python tests/crash_recovery.py
python tests/magnet_integration.py
# 开发机浏览器集成测试需要 Node + Playwright；使用系统 Edge 和全新隔离配置。
$env:HBT_PLAYWRIGHT = '已安装的 playwright 模块绝对路径'
node tests/browser_integration.cjs
```

浏览器测试会临时注册自己的独立连接程序，结束恢复旧值。只在生成的测试目录启动 GUI/浏览器，真实发送 HTTP 与 Magnet，最终核验内容；测试通过浏览器 API 触发下载，不冒充已点击网页链接的人工验收。

包内双击 `run-selftest.cmd` 可运行单元、HTTP/HTTPS/BT、强杀恢复和 Magnet 测试，无需安装 Python。双击 `Magnet-Isolated-Test.cmd` 可独立验收 Magnet 界面：生成自己的三份文件（含中文目录及空文件）、本地做种节点和 `magnet.txt`；复制链接添加，点击暂停、关闭，控制台 `r` 重开后点恢复；`f` 加速，`v` 独立校验全部文件，`q` 结束。不会使用真实用户任务目录。

测试记录与已验证/待验证边界见 `docs/results/magnet-browser-06.json`。单次局域网测试不代表性能优势；本轮未重写调度优化。
