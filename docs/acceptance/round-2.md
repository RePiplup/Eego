# 第二轮交付 · 2026-11-02

内核抽离、UI 文件选择和独立验证并行推进；本轮已完成共用 TaskService 第一阶段，且新版 UI 已跑通真实 Magnet 多文件下载。

## 入口与试用

双击项目根 `HandmadeBT-Round2.cmd`，或本目录 `Start-UI.cmd`。已有服务时会打开它；否则启动服务并打开 http://127.0.0.1:18766/?mode=real 。使用现有本机 Python，不需另装下载器。

1. 页面右上确认“真实内核”。点击列表底部“BT 多文件测试”。
2. 等任务出现“选择文件”，点击它；取消 `design-assets.bin`，保留 `sample-video.bin` 与 `阅读说明.txt`，点击“下载选中文件”。
3. 下载中点击暂停，待状态变成“已暂停”后点击继续。完成后点击任务名称，查看所选文件完成信息和未选择文件；“查看文件清单”此时只读。
4. 点击“HTTP 测试下载”可检验 HTTP；“新建下载”可粘贴自己的 HTTP/HTTPS/Magnet 链接。
5. 右上切换模拟模式，可独立试用全部状态与文件选择；`../ui-next/Start-Mock.cmd` 直接打开本地模拟界面，不依赖内核。

真实数据仅存于 `runtime/userdata`，原 dist 和日常 userdata 未覆盖。运行的最终内核为 `build-round2/task_service_host_r2.exe`，哈希见 `runtime/config.json`。原生预览是 `build-round2/handmadebt.exe --data-dir <另一个隔离目录>`，它使用相同 TaskService。

浏览器关闭不等于服务退出。在服务终端 Ctrl+C 停止服务；原生服务管道关闭会尝试有序暂停。重开后未完成任务为暂停，继续时核验断点。自产测试源端口随整个预览服务重启更换，因此旧测试任务的本机 URI 可能不可用，应新建测试任务；强杀恢复验收保持测试源运行、只强杀下载服务，避免把源消失混作恢复失败。

## 实际验证结果

| 项目 | 本轮结果 |
|---|---|
| 新 UI 真实 Magnet 文件选择 | 通过；浏览器实际勾选两个文件、排除一个，暂停继续后完成 |
| 所选文件内容和未选文件保护 | 通过；独立读取完成文件 SHA-256 与自产源一致，未选文件未发布 |
| 新 UI HTTP 与 BT API | 通过；真实下载、非法/空/重复选择拒绝、部分选择、暂停继续、完成和只读清单 |
| HTTP 下载中强杀恢复 | 通过；直接 kill 下载进程，再启动同目录、继续、最终独立 SHA-256 一致 |
| BT 下载中强杀恢复 | 通过；保留所选集合，恢复后两文件独立 SHA-256 一致，未选文件不存在 |
| 公网 Magnet/BT | 通过本次资源与网络；Sintel 129,302,391 字节、11文件，89.02秒完成，987个piece独立校验全部通过 |
| DHT-only | 尚未证明；本次URI含tracker，DHT节点计数为0，不将公网成功归因于DHT |
| 新原生 GUI | 真实HTTP下载及独立hash回归通过；共用服务并非只供新版UI使用 |
| 编译回归 | 最终CTest 5/5通过；原生管理器完整集成回归ALL PASS（构建身份与最后修复范围详见架构报告） |
| 断电、磁盘满、断网自动重试、长期负载 | 尚未验证；进程强杀不等于断电 |

发现并修复：子集选择后重启暂停快照曾显示全种子大小；现选择时立即持久化选中大小，重启从元数据和选择记录重算。另修复启动准备失败的幽灵 running 状态和实例锁释放早于 worker 停止的问题。

公网传输在本轮较早构建 `task_service_host.exe` 上执行，最终 `_r2` 构建修正任务锁与选择总量，未修改网络协议实现。两者 SHA-256 和适用证据分别记录，不能冒称公网结果是最终文件的重复实测。最终版本另行复验强杀恢复、原生GUI和新版UI接口。

公网合法来源：[WebTorrent 官方公开测试资源](https://webtorrent.io/free-torrents)。测试使用仅含xt/dn/tr的Magnet，无显式本机peer、web seed或HTTP元数据；原torrent只用作独立校验。存在tracker错误计数，不表示所有tracker均可用。

## 架构进度（与实测分开）

**已完成这一阶段**：Task创建/编号、单队列调度、worker生命周期、任务记录、暂停/继续/重试、文件选择、速率采样、有序停止移入 `core::TaskService`。原生 GUI 与无界面 host 都链接同一 download_core。新版 UI 通过结构化接口取得精确整数，不再读原生窗口控件；Python只提供传输和测试源。

**尚未完成统一内核目标**：HTTP/BT运行时与恢复文件格式仍不同；单活跃调度、全局预算尚未统一；GUI仍直接操作部分公开容器，painted/删除字段及Win32 Inventory耦合未完全收拢；快照JSON在host而非稳定DTO，仍轮询；错误类型和逐文件实时进度仍不完整。恢复前记录中的bytes不是已经重验的磁盘偏移。

## 工作分工与证据

- 内核工作线：`src/task_service.*`、`src/task_service_host.cpp`、原生GUI委托调用及独立 `build-round2`。详见 `../kernel-round2/REPORT.md`。
- UI 工作线：`../ui-next`，两种适配、文件选择与真实浏览器画面；`evidence-round2/browser-results.json` 及模拟检查记录。
- 验证工作线：`../kernel-round2-validation/REPORT.md`，独立强杀和公网内容核验。
- 主任务：本目录共同接口、Python传输与整合、启动入口、端到端独立内容核验。接口见 `CONTRACT.md`。
- 新UI API证据：`runtime/api-acceptance.json`；浏览器创建文件独立核验：`runtime/ui-output-verification-initial.json` 与最终 `runtime/ui-output-verification.json`。
- 实际画面：`../ui-next/evidence-round2/real-file-selection.jpg`、`real-bt-paused.jpg`、`real-bt-completed.jpg`。
- 最终构建浏览器复验保留旧任务3的完成/只读状态，新建任务6仅选阅读说明并完成；精确文件大小32,769字节，界面舍入显示32.0 KiB。新图 `final-build-bt-completed.jpg`；浏览器记录不将舍入显示当精确计数。

## 下一轮优先

1. 把GUI直接任务容器操作、删除/回收和展示字段继续移出核心任务模型；形成稳定快照DTO与事件接口。
2. 单独验证DHT-only与断网重试、磁盘错误；继续区分可恢复行为和数据完整性，不扩大本轮通过范围。
3. 提供真实逐文件进度与更明确的恢复/错误提示，再推进全局带宽、连接与磁盘预算；不在现有全局回调上直接开启多任务并发。
