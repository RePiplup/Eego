# HandmadeBT 0.8.1 修复交接（2026-10-28）

1. 修复内容

HTTP逐跳处理301/302/303/307/308和相对地址，最多10跳；每跳拒绝非HTTP(S)、URL凭据与HTTPS降级，保留系统证书验证。关闭自动登录和Cookie转发。循环、无/多Location、未受信任证书、HTML页面均失败，不伪报完成。Unicode响应头转为UTF-8交给现有aria2解析器。没有改写BT调度。

实际报表还需要chunked支持：采用最多32 MiB的有界接收，WinHTTP校验分块结束；完整收到后才创建文件，写入后重读验证SHA-256。超限/截断停止。此路径暂停会丢弃内存缓冲，下次从零接收；普通Range任务仍沿用原恢复记录。未知历史文件不清理。

2. 真实链接与回归证据

任务8—14对应两个不同PDF URL。实际请求均302到人社局`/uamsso/login`，最终200 HTML。新版正确拒绝登录页：这七项尚未下载到PDF，不能宣称已解决登录访问。当前扩展只传URL，没有登录会话；对这种网站应关闭自动接管并在已登录浏览器下载。

新增任务15的报表链接返回200、chunked、中文Content-Disposition。新版CLI实际下载5124字节PDF；严格解析为1页，正文261字符，渲染可读，与独立WinHTTP请求得到的PDF正文完全一致。初次核验SHA-256为`323b1f504aef69f6b458be7cc0f224ab07acc1c08d09dc9ddccc4ce05169a04d`。动态PDF可能因生成时间等元数据产生不同字节摘要，不能把跨请求哈希不同直接当损坏。用户报表及个人信息不随包分发。

回归包括4个单元程序；原HTTP/BT/HTTPS集成20项、恢复保护30项、管理器31项；新增重定向/分块实际请求25项，验证分段/恢复字节、循环/超限/协议/凭据/证书/HTML/截断拒绝；HTTPS降级由生产共用URL检查函数的单元测试覆盖，没有安装受信任测试证书。真实Edge独立配置验证native冷启动、自动接管、失败恢复浏览器、Magnet、Windows协议及注入拒绝。机器可读结果见`results/fix-081.json`。

3. 启动与迁移

新便携目录：`dist/HandmadeBT-0.8.1-safe-redirect-win64`，入口`HandmadeBT.cmd`。本机更新浏览器native-host和自定义协议注册，保留回滚信息；已有扩展ID和协议不变，不要求新增Cookie权限。

迁移时复制旧版当前任务记录，保存独立备份并校验哈希；任务ID、URL和原绝对下载路径保留，原目录不移动、不删除。新目录的userdata为本机迁移数据，不包含在发行ZIP中。没有从历史备份复活已移除任务。

4. 仍有边界

这不是任意网站兼容声明。需登录的两个PDF地址仍不能由URL-only内核取得正文；HTML防误报不替代身份验证。chunked仅限32 MiB且不做断点续传；压缩正文、非chunked未知长度、代理和浏览器会话传递未实现。

实现依据：[Microsoft WinHTTP安全说明](https://learn.microsoft.com/en-us/windows/win32/winhttp/winhttp-security-considerations)、[UrlCombineW](https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-urlcombinew)。默认证书策略未修改。

## 本机交付补验

打包后关闭旧版时发现新增任务16，最终迁移的是ID 8—16共9项，记录备份见`results/migration-081.json`。Chrome/Edge native host已核验指向0.8.1；新窗口实际全选并重试全部9项。任务15完成5124字节，GUI产物SHA-256为`61874650b760d9a61de7a71003177f66c9f8171f5f0521407560e4732d899169`，严格PDF解析通过、正文与独立请求一致；其余8项拒绝登录HTML。所有旧有文件摘要保持不变。

实际重试后的关闭重开因检测到用户正在操作而未执行，窗口保留；已独立检查落盘任务记录。隔离重开测试仍是通过的，不混为本机任务手工重开证据。详细补验见`results/fix-081-followup.json`；其中也修正了初版机器记录对管理器checks字段的计数显示（实际31项）。

发行ZIP：`dist/HandmadeBT-0.8.1-safe-redirect-win64.zip`，52,680,817字节，SHA-256 `05436ab7d18a9424f154d72880f075f795e2ba03396ad04a77674e5be0f1d636`。包内清单核验通过，ZIP不包含用户任务、报表或测试输出。包自带Python/CLI再次通过25项重定向/分块测试。本节为打包后的本机验收补充，未修改发行ZIP。
