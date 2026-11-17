# HandmadeBT 0.7：试用反馈修复

## 使用

启动 `dist/HandmadeBT-0.7-task-management-win64/HandmadeBT.cmd`。
Magnet 先获取元数据，在任务列表显示“请选择文件”后，选中任务并点击“选择 / 查看文件”。勾选需要的文件后开始下载。未取得元数据时无法知道文件列表。

“移除任务”停止对应传输并仅删除列表记录，保留完成文件、未完成文件和恢复记录。本版没有撤销按钮；重新添加会创建新任务，不能自动找回被移除的记录。
选文件只支持首次开始传输之前。已经开始传输的任务可查看原选择，但不能中途修改；旧版已开始的任务继续按原来的全部文件恢复。

## 实现边界与源码

- `src/gui.cpp`：原来每 200 ms 重写所有单元格，现在缓存显示值，仅写入变化单元格，保留 ListView 双缓冲；结构性移除才重建列表。避免选中通知递归刷新，初始化完成前不绘制。
- 同文件：进度条取 HTTP 实际接收量或 BT `total_wanted_done`，总量取选择文件大小，绝不取预分配文件长度。未知总量显示等待大小。窗口使用 `WS_OVERLAPPEDWINDOW` 和 `WM_SIZE` 布局，支持最大化/还原。
- 同文件：任务记录 GUI2，向后读取 GUI1；新增待选择、暂未找到节点状态。移除运行任务先请求暂停，等待工作线程退出，保存移除记录后更新界面。未删除数据文件。
- `src/magnet.cpp/.hpp`：保留 libtorrent 数据接收、piece picker、choking、uTP、哈希校验。GUI 元数据获取后停止，选择记录绑定 infohash 并校验；添加任务时传入 `file_priorities`，不重写协议调度器。
- `src/bt_disk.cpp`、`src/bt_files.hpp`：真正的自定义磁盘读写继续接公共 FileStore。只准备所选输出文件；跨文件分片必需的未选字节写入独立 `boundary-cache`。这是校验所需缓存，不是未选文件的完整下载。缓存逻辑长度可能很大，实际为 Windows 稀疏文件。
- `src/file_store.cpp/.hpp`：稀疏缓存通过 Windows FSCTL_SET_SPARSE 创建；保留路径、大小、重解析点及归属检查。完成后校验涉及所选文件的分片，再逐文件 SHA-256。
- `src/checkpoint.cpp`：真实浏览器回归遇到原子替换失败；受控打开目标但不共享删除时复现 Windows 错误 5。对 5/32/33 最多重试 500 ms，永久失败仍保留旧记录并报错。`tests/checkpoint_test.cpp` 覆盖短占用恢复及持续占用保护。
- `tests/magnet_selection.py`：真实受控做种，选择跨分片文件和空文件、暂停恢复、损坏数据/边界缓存重校验修复、非法选择拒绝、篡改选择记录拒绝。

GUI 仍直接链接 download_core，不使用 aria2c、RPC 或 CLI 作为后端。aria2 迁入的解析实现、WinHTTP HTTPS 证书校验以及 libtorrent 原协议运行时边界保持不变。上游原始源码、版权和许可证保留；发行包包含 source.zip 和固定版本/文件摘要。

## 磁力无资源的实际限制

三分钟无法取得元数据，只说明当前没有发现能提供元数据的节点，可能是资源离线、网络限制或发现失败，不足以证明资源不存在。0.7 改为可重试的明确状态；没有声称能下载无人做种资源，也没有擅自重新下载用户未知来源的磁力内容。公网发现能力仍沿用 0.6 已验证实现。

## 验收记录

详细结果见 `docs/results/*07*.json`。交互验收使用隔离目录，未删除用户原任务。
真实多文件界面：取消第一份文件，下载第二份与空文件；实际点击暂停于 999424/2097181 字节，关闭重开恢复，完成后独立 SHA-256 与自产源文件一致；输出仅两个选中文件。随后实际移除该任务，记录清空而文件哈希保持一致。
窗口最大化、还原以及真实字节进度条已观察；没有用单张截图声称已量化所有显示设备上的闪烁。拖边缩小触及最小窗口大小，未单独证明任意尺寸布局。
下载中的 HTTP 测试任务实际点击移除，记录清空，预分配 part 和 resume 保留，没有被标为完成。

## 构建与复验

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1
python tests/integration.py --online
python tests/crash_recovery.py
python tests/magnet_integration.py
python tests/magnet_selection.py
# 需设置 HBT_PLAYWRIGHT 为已安装 Playwright 的绝对路径
node tests/browser_integration.cjs
python tools/package.py --output dist/HandmadeBT-0.7-task-management-win64
python tools/accept_package.py dist/HandmadeBT-0.7-task-management-win64.zip --unicode
```

本版未扩展视频、登录 Cookie/表单、代理配置、v2/hybrid、运行中修改文件优先级或跨协议同文件下载。既定浏览器接管及后续视频需求保持不变。
