# 中文及空格路径验收（0.4，2026-10-26）

## 复现与最小修复

旧 0.3 包实际解压到 `package-acceptance/中文 验收 …`，从 `无关 工作目录` 启动。首个 HTTP 下载返回 `ERROR: locale not supported`，没有完成。旧自测随后因为 UTF-8/GBK 解码不一致再次失败，日志出现 U+FFFD 无法编码错误。失败记录见 [旧包结果](results/unicode-baseline-failure.json)。没有把失败当成功。

- `src/main.cpp`：以 `wmain` 接收 Windows UTF-16 参数，严格转为 UTF-8 后送入既有逻辑；`CMakeLists.txt` 为固定 MinGW 工具链加入 `-municode`。不改变系统代码页或区域设置。
- 种子输出文件显式用 `filesystem::u8path` 进入宽字符文件系统路径，避免 UTF-8 字节被按 ANSI 解释。旧未使用的 load 辅助函数也使用相同转换。
- Python 自测和验收工具显式输出 UTF-8，并按 UTF-8 读取 CLI 输出和 seed 日志。没有用忽略错误或替换错误字符作为验收成功的条件。
- `tools/accept_package.py --unicode` 在新建中文空格目录解压，从另一个中文空格工作目录启动实际 CMD 入口。仍只把 Windows 系统目录放进子进程 PATH，仍使用包内 Python/DLL。
- 真实下载测试包含中文 HTTP 文件名 `下载 http-single.bin`、中文种子 `测试 种子.torrent`、BT 内容 `测试 数据.bin`；BT 自定义磁盘和上游做种/对照后端均参与。原测试断言保留，只有路径和日志编码调整。

没有修改 HTTP 两块合并保存、检查点格式/校验、TaskLock、FileStore、BT 调度、TLS 证书验证或上游源码/许可证。

## 已完成的直接验证

编译成功，2 个单元测试程序通过。中文输出根目录下 20 项在线 HTTP/HTTPS/BT 集成检查、30 项恢复/文件保护检查通过，所有完成下载核验 SHA-256。覆盖暂停恢复、强杀、重复启动、首个批量检查点之前强杀、损坏块重验、错配/残损检查点和无关同名文件保护。

证据：[下载回归](results/unicode-integration.json)、[强杀与文件保护](results/unicode-crash.json)。曾在测试脚本增加日志配置时遗漏 sys 导入，运行立即报 NameError，补回后重跑成功；这次失败没有计作通过。

最终便携包还需要从 ZIP 在中文路径独立解压复验；验收后报告写入工作区 `UNICODE_PACKAGE_ACCEPTANCE.md`，不预先放入待验收包。ASCII 路径模式保留并单独复验。

## 重现

```powershell
./tools/build.ps1
python tests/integration.py --online --output-root 'test-output/中文 下载验收'
python tests/crash_recovery.py --output-root 'test-output/中文 下载验收'
python tools/package.py --output dist/HandmadeBT-0.4-unicode-win64-r1
python tools/accept_package.py dist/HandmadeBT-0.4-unicode-win64-r1.zip --unicode
python tools/accept_package.py dist/HandmadeBT-0.4-unicode-win64-r1.zip
```

仍是本机 Windows 验收，不等于所有 Windows 区域设置、其他系统版本、超长路径、网络共享、emoji 或特殊设备名都已支持。没有测试 GUI 交互、全新机器部署、断电或长期公网下载。性能核心没有修改，本轮不重复宣称性能提升。

BT 仍限单文件 v1，完整内部运行时融合、Magnet/多文件、浏览器接管和视频流功能尚未完成。
