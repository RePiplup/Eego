# 0.5 最小下载界面：实际验收记录

日期：2026-10-26，Windows 本机。交付可直接打开的原生 Windows GUI，直接链接现有 C++ download_core。没有启动 aria2c/RPC 或调用 core_cli 作为界面后端。CLI 仅在独立回归脚本中使用。

## 明确启动入口

解压 `dist/HandmadeBT-0.5-gui-win64.zip`，双击 `HandmadeBT.cmd`，或直接打开 `bin/handmadebt.exe`。二者使用同一包根目录 userdata。独立验收入口 `GUI-Isolated-Test.cmd` 生成全新目录与自产 8 MiB 文件、真实 HTTP/不可信 HTTPS 服务、慢速/错误链接及预期 SHA-256；支持 f/s/r/v/q 控制，见 [GUI_QUICKSTART.md](GUI_QUICKSTART.md)。不改变系统证书或正常任务列表。

## 实际窗口操作与内容证据

通过 computer-use 的 Windows 原生窗口操作，真实点击添加、选择任务、暂停、恢复和关闭；没有使用 CLI 的 PAUSE_CHUNKS 作为按钮替代。界面总计添加 8 条任务：5 条完成、3 条正确失败。

- 单条慢速真实 HTTP：临时文件已预分配 8,388,645 字节时，窗口实际进度是 786,432 字节（9.4%），没有把文件长度当进度。
- 点击“暂停选中”：在接收过程中请求内核暂停，最终持久化 2,097,152 字节的完整块；正常关闭重开后任务仍在，点击恢复下载完成。
- 多行一次添加 3 条：正常 Range 文件完成；404 和不可信 TLS 分别显示实际错误（HTTP 404、WinHTTP 12175），不出现完成文件。
- 另一次多行添加：错误预期 SHA-256 拒绝提交；接收进度可达 100% 但状态为“错误”。固定公开 HTTPS 文件通过默认 Windows TLS 验证与已知源文件 SHA-256 验证。
- 不支持 Range 的服务显示“恢复需重下”；接收约 458,752 字节时关闭窗口，连同后面的排队任务一起保存为暂停。重开后 8 条均保留，随后分别恢复并完成。
- 5 个完成文件全部独立读取并比对已知 SHA-256；3 个错误任务均无正式完成文件。[独立核验结果](results/gui-independent-verification.json)、[外部哈希](results/gui-verified-files.json)、[窗口观察记录](results/gui-ui-observations.txt)。

测试原始目录：`test-output/界面 验收 20261026-172815 3d336e`。包内 `tests/gui_verify.py fixture.json` 是独立内容验证器，不伪装为自动点击测试。

## 最终包验收与可追溯版本

- 包大小：47,850,305 字节。
- ZIP SHA-256：`60707c128823b8dc80a40b0e06a49f6c56b3687304382d5b394914d0d5a9af70`。
- GUI SHA-256：`f0db0908b56c13c6f3c8b367be1909f8b1b490bdcb2d169aa7fb2de1223506b9`。
- CLI SHA-256：`ac279018cf5c4357dc8482deb8e19f8ab4adbc0ed56027c190880657f5360bf8`。
- GUI 与最终界面操作使用的构建一致；测试构建说明见 [记录](results/gui-tested-build.json)。最早一次按钮暂停发生在最终非 Range 进度重置/默认 userdata 路径微调之前，按钮处理与 Range 暂停实现没有随后更改；后续批量、恢复、关闭重开和内容校验使用上述最终 GUI。
- ZIP 在新的中文空格目录解压，限制子进程 PATH 为 Windows 系统目录，实际通过 2 个单元测试程序、20 项在线下载回归、30 项强杀恢复/文件保护检查。
- 核验 2,428 个上游源码文件、source.zip 完整性、包文件篡改拒绝。最终包 GUI/CLI 摘要与 build 一致。[最终包记录](results/gui-package-acceptance.json)、[回归](results/gui-package-integration.json)、[恢复保护](results/gui-package-crash.json)、[环境](results/gui-package-environment.json)。
- 包内 `GUI-Isolated-Test.cmd` 在交互终端真实启动内置 Python、本地服务及 `dist/.../bin/handmadebt.exe`，控件显示新生成的隔离任务目录；输入 f 和 q 正常处理。该启动冒烟检查不替代前述完整窗口操作。
- `provenance/package-inputs.json`、`manifest.json`、`source.zip` 提供准确来源和文件摘要。上游原始许可证保留。该报告在最终包验收后生成，放在工作区，不回写已验收 ZIP。

## 失败、限制与尚未验证

1. 一次重开测试误在网络受限沙箱中运行，窗口显示真实 12029 连接错误，未计作成功；在允许回环网络的环境重开后恢复成功。
2. 自动窗口操作因用户切换/最小化出现 `user input was detected in this window` / geometry unavailable；取得用户短暂配合后完成主要窗口操作，没有绕过保护。个别 UIA 文本快照滞后于屏幕，最终状态以截图、持久记录与文件哈希交叉核验。
3. 隔离启动器第一次经无 stdin 管道启动遇到 EOFError，未计作入口通过；真实交互终端启动通过。目前交互入口应在正常控制台运行；无输入自动化应直接使用 gui_fixture.py 的非 interactive 模式。
4. 这是最小界面：单活动任务、最多 500 条保留任务，无删除/排序等完整任务管理；不支持通用重定向、chunked、未知长度、零字节、压缩、认证/代理；非 Range 暂停后从零重下。暂停等待当前 WinHTTP 调用返回或超时，不保证瞬时停止。
5. 仅本机验证，未做全新机器、断电、长期公网负载、所有磁盘故障；未声称独立源码快照重建已通过。BT 尚未进入界面，其余 BT、浏览器接管与 HLS/DASH 保留在最终需求中。

## 构建及复验

```powershell
./tools/build.ps1
python tests/integration.py --online
python tests/crash_recovery.py
python tests/gui_fixture.py --launch --interactive
python tools/accept_package.py dist/HandmadeBT-0.5-gui-win64.zip --unicode
```

下一步应由用户按 GUI_QUICKSTART 中的隔离流程独立试用，反馈实际界面问题；不在此阶段继续扩展新协议或性能重构。
