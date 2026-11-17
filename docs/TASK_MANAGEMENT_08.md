# HandmadeBT 0.8：基础下载任务管理器

## 启动与使用

解压完整便携包后双击 `HandmadeBT.cmd`，或运行 `bin/handmadebt.exe`。
源码构建入口为 `tools/build.ps1`，构建输出 `build/handmadebt.exe`。
使用 `--data-dir 绝对目录` 可隔离数据。任务格式继续为 GUI2，兼容 GUI1。

分类为全部、下载中、已完成。下载中包含所有未完成任务：等待、运行、暂停、失败、待选文件、暂未找到节点。不会将失败和暂停隐藏成“无任务”。
Ctrl/Shift 多选；全选按钮或列表内 Ctrl+A 只选择当前分类。切换分类清除选择，避免批量操作影响不可见任务。暂停适用于等待和运行，恢复适用于暂停，重试适用于失败/元数据超时；不适用项跳过。仍逐个启动任务，沿用成熟 BT session 调度。
右键提供常用操作；Delete 打开移除确认；双击打开已完成文件。Magnet 多文件任务“打开文件”进入文件列表，可选择已下载文件再打开文件或所在文件夹；未选择下载的条目标明“未下载”并拒绝打开。HTTP 的“打开保存目录”打开目标父目录。
进度来自内核实际接收量，速度为一秒采样的有效进度变化，连接/重验/暂停阶段显示 0。剩余秒数为按当前速度估算，不代表网络吞吐统计或保证完成时间。未知总量明确显示未知。

## 官方交互依据（2026-10-27 核实）

参考 [IDM 官方主窗口说明](https://www.internetdownloadmanager.com/support/main.html)：任务表格中的状态、速度、剩余时间；按选择启用/禁用控制；停止选中文件；右键复用主操作；分类导航。分类在此采用状态而非文件扩展名，移除增加更明确的两种动作。
参考 [FDM 官方功能说明](https://www.freedownloadmanager.org/features.htm) 的暂停后恢复能力。本轮没有照搬品牌、图片、图标、广告、会员或云功能，也没有移入视频功能。
回收使用 [Microsoft IFileOperation 标志](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifileoperation-setoperationflags) 中 RECYCLEONDELETE、EARLYFAILURE，并由 PreDeleteItem 拒绝无回收标志的永久删除回退。

## 文件保留与清理规则

- 暂停、失败、正常关闭：保留输出/part、恢复记录、Magnet 元数据、选择、BT 归属及 boundary-cache。重新打开不会自动启动等待或运行中的任务，而是转为暂停；完成项仍重读校验。
- 仅移除：运行任务先暂停、等待线程和写盘结束，再保存列表。所有下载数据、恢复记录、缓存和归属审计记录保留。移除没有撤销或自动重新挂载；历史残留不按名称或前缀清扫。新建任务不会接管历史文件。
- 同时删除：同样先停止；仅对当前任务的显式创建清单逐文件核对身份、大小、修改时间。HTTP 输出、part、resume 及实际登记的临时记录，BT 的实际创建文件、元数据、选择记录、磁盘归属/恢复记录和边界缓存可回收。不递归删除目录。
- 清单来自 FileStore CREATE_NEW 与成功原子改名事件，不从文件名、后缀或目录内容推断所有权。清单绑定任务 ID、URL、绝对目标路径，并沿用带校验和的原子记录。停止后保存；若进程崩溃导致新创建文件未登记，则安全保留，不能自动删除。
- 旧任务没有 0.8 创建清单时保留数据；遇到未登记目标、额外用户文件、被替换/修改文件（重试不会重新授予删除资格）、硬链接、重解析路径、身份读取失败，停止该任务的文件删除并保留任务和错误。可选择仅移除。旧记录不会被偷偷升级为删除授权。
- 删除期间持有原内核任务锁；内部重入保护阻止 Shell 回调启动另一轮删除。回收仅允许本地固定磁盘。Shell 返回取消、失败或永久删除回退时停止，无 DeleteFile/永久删除兜底。批量回收不是跨文件事务，失败前部分文件可能已进入回收站，错误会提示检查；任务仍保留。
- 锁文件、空目录以及 userdata 中 ownership 审计记录保留；来源不明的原子写入临时文件保留。回收站由 Windows 管理，可从中恢复；软件不清空回收站。
- HTTP 成功提交时按原内核行为移除已失效 resume；BT 完成后保留恢复/校验材料直到显式回收，便于后续完整性核验。

## 实施范围

`src/gui.cpp` 为原生任务管理与交互，`src/managed_files.hpp` 为创建清单、保护预检和 Shell 回收。`src/file_store.*`、`src/checkpoint.cpp`、`src/http.cpp` 仅增加创建/改名观察事件；GUI 仍源码链接同一 download_core。未改 WinHTTP 证书校验、libtorrent peer/picker/choking 调度或默认磁盘边界。`src/gui.manifest` 启用 Windows v6 公共控件供 TaskDialog 使用。

## 实测与复验

`tests/manager_integration.py` 生成独立 HTTP、BT 源文件和任务目录，运行包含生产控制器与真实 Win32 控件的 `manager_test.exe`，不是模拟下载。覆盖批量状态和分类选择、运行中移除/回收、真实回收站、失败重试、历史/额外/修改文件保护、硬链接和永久删除回退拒绝。HTTP 完成文件由独立 Python SHA-256 比对；BT 既校验种子分片，也逐字节比较独立源文件。另启新进程重载生产记录，核验完成、暂停、失败状态。
原集成、HTTPS、强杀恢复及选文件损坏修复继续通过。结果为 `docs/results/*08*.json`。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1
python tests/manager_integration.py
python tests/integration.py --online
python tests/crash_recovery.py
python tests/magnet_selection.py
```

便携包 `run-selftest.cmd` 已加入管理器验收，包含 Python、全部必要 DLL、内核与上游源码及许可证。测试仅使用新隔离目录；不安装证书，不关闭安全校验。

当前桌面锁屏，读取到了控件树，但截图是锁屏画面、激活和实际点击失败。因此没有将本轮自动控制器验收描述为人工点击验收；视觉布局、真实右键/删除弹窗点击仍需在解锁桌面后补验。网络盘/可移动盘因不能承诺回收而拒绝，未做断电、并发恶意替换攻击或所有磁盘故障认证。原 HTTP 协议限制及 v2/hybrid 限制仍见项目 README。


2026-10-28 补验：锁屏已解除，已完成实际添加、批量暂停恢复、关闭重开、分类、右键及两种删除模式的限定范围 UI 验收。详见 [补验记录](results/ui-08-followup.json) 和 [最新交接](DELIVERY_08.md)。发行包二进制和 ZIP 未改变；包内旧文档保留打包时记录。
