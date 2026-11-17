# 0.7 本机交付记录

正式入口：[HandmadeBT.cmd](../dist/HandmadeBT-0.7-task-management-win64/HandmadeBT.cmd)。新版已打开。

发行 ZIP：`dist/HandmadeBT-0.7-task-management-win64.zip`，SHA-256：
`243dbd564262ddd1458e3e3050491f1f2478f65515097f871be320c6ef1065c5`。
GUI SHA-256：`222b058b351dd4c4428e0ee89937100f40ca2e8a26e9b06a9a841abdce175cf6`。
固定上游提交及工具链见发行包 `provenance/package-inputs.json` 和 `provenance/inputs.lock.json`，全部源码见 `source.zip`。

## 本机升级

正常关闭 0.6 后，复制七个任务的记录到 0.7 的 userdata，另留 `tasks.record.before-07`。原记录和下载文件没有移动或删除。独立副本和正式新版界面均实测载入全部七个任务；已完成两项重读校验保持通过，暂停任务没有自动启动，失败任务仍保留原错误记录。

**旧任务仍引用 0.6 目录里的下载文件，因此暂勿删除旧版目录。** 新添加任务默认保存到 0.7 的 userdata/Downloads。ZIP 在复制本机任务之前已完成，不包含用户任务或下载内容。不要同时操作旧版与新版中的同一任务。

现有 Edge 扩展不需要重装，扩展 ID 不变；已将当前用户的 native-host 注册路径更新到 0.7。未修改 Windows 默认 Magnet 应用，也未提高权限或关闭证书验证。注册备份位于新版 browser/registration-backup.json；执行新版注册脚本 `-Unregister` 可恢复之前注册。

## 补充界面验收

最终版本在旧任务副本上通过 Windows 系统菜单“大小”及方向键扩大宽度，观察到窗口从 757 到 789 截图像素，输入区、列表和详情区随宽度变化。此前最大化及还原也已观察。这补充了 TASK_MANAGEMENT_07.md 中打包前的缩放验收记录。

## 结果索引

- `results/package-07.json`：独立中文路径解压、内置 Python、只保留系统 PATH，可信 HTTPS、原恢复和文件保护、选文件下载、源码与包完整性。
- `results/browser-07.json`：9 项隔离 Edge 扩展/native host/HTTP 哈希/Magnet 真实元数据/交接失败回退测试。
- `results/ui-07.json`：实际按钮暂停、关闭重开恢复、所选文件独立哈希、运行及完成任务移除且文件保留。
- `results/selection-07.json`：跨文件分片缓存、选文件、恢复、损坏修复和记录保护。

用户截图中无法找到节点的具体资源本轮没有重新下载，也没有证明资源不存在；新任务/重试后的元数据超时提示明确说明这个区别。源码修复与功能限制详见 TASK_MANAGEMENT_07.md。

下一步是试用修复后的列表、任务移除、文件选择和进度条；只针对复现的反馈修复，不扩展视频或重写 BT 调度。
