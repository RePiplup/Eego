HandmadeBT 0.8.1 — 下载与任务管理

修复：安全 HTTP 重定向（最多10次，拒绝 HTTPS 降级、URL 凭据和循环）；中文响应头；最多32 MiB的 chunked 报表。
登录页 HTML 不标记完成。浏览器只传 URL，不传登录会话；需要登录的网站请关闭扩展自动接管并使用浏览器下载。

新增：全部 / 下载中 / 已完成分类；Ctrl/Shift 多选、分类全选；批量暂停、恢复、重试、移除；右键菜单、打开文件/文件夹；速度及剩余时间。
删除分为仅移除任务与同时回收文件；无法确认归属或无法可靠回收时停止，无永久删除降级。
仅移除后保留全部数据与恢复记录，没有按文件名扫描历史残留。旧任务无创建归属清单时不能自动删除文件。
Magnet 获取元数据后，选中任务，点击“选择 / 查看文件”，勾选后开始下载。
已经开始下载的任务本版只能查看选择，不能中途改变；需改变时请新建任务。
暂时找不到元数据节点不等于资源不存在，可稍后重试；无人做种的资源无法保证下载。
更新和本轮测试说明：docs/TASK_MANAGEMENT_08.md。

启动：解压整个包，双击 HandmadeBT.cmd（或 bin/handmadebt.exe）。
支持 HTTP/HTTPS、v1 Magnet 多文件、单条/多行添加、真实进度、暂停/恢复、关闭重开任务列表。
正常任务保存在包根目录 userdata，下载文件默认在 userdata/Downloads。

独立界面验收：双击 GUI-Isolated-Test.cmd。
它生成隔离数据、真实本地服务器、预期哈希和测试链接，并打开独立任务界面。
控制台 f 加速、s 慢速、r 重开隔离界面、v 核验、q 停止。
具体说明：docs/GUI_QUICKSTART.md。

原底层自动回归：run-selftest.cmd（默认包含公开 HTTPS，需要联网）。
原 --offline 模式只跳过公开 HTTPS；自动回归不是界面点击验收。

GUI 直接链接同一个 C++ download_core，不调用 CLI/aria2c/RPC。
完成前重新读取文件校验；可输入 URL | SHA-256 验证来源给定哈希。
不支持 Range 的服务器暂停后从零重下，不能断点续传。
未受信任证书必须失败，不安装测试证书，不关闭证书检查。
本版不支持认证会话传递、代理、非 chunked 未知长度、零字节或压缩正文。分块报表超出32 MiB会停止，暂停后从零接收。
浏览器：运行 Browser-Setup.cmd，再在 Edge/Chrome 加载 browser/extension。
支持右键链接交接及可选普通下载自动接管；详见 docs/MAGNET_BROWSER.md。
Magnet 界面独立验收：Magnet-Isolated-Test.cmd。
v2/hybrid、网页视频、登录凭据传递仍未实现。

source.zip 包含核心与上游源码；LICENSES、COPYING 和 provenance 保留来源。
provenance/package-inputs.json 记录版本、上游提交和 GUI/CLI 摘要。
旧版记录不得通过删除未知文件强行接管；本版不承诺断电和所有磁盘故障恢复。
