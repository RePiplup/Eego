# 便携验证包

0.5 提供最小 Windows 下载界面，入口 HandmadeBT.cmd。独立界面验收入口 GUI-Isolated-Test.cmd；详见 GUI_QUICKSTART.md。

## 使用

从 `dist` 下的 ZIP 解压到可写的英文目录（允许空格）。双击 `run-selftest.cmd`，等待 `ALL TESTS PASSED`。默认包含一次公网可信 HTTPS 下载，需要网络；本机 HTTP 服务及 BT seed 由测试自动创建并关闭，不需要用户准备文件。

自动化命令：

```powershell
# 完整自测，不等待按键
./run-selftest.cmd --online
# 仅离线自测；明确跳过公网可信 HTTPS
./run-selftest.cmd --offline
```

退出码 0 表示所有已选择的检查通过；1 表示失败。控制台会给出日志位置。每次运行在 `test-output/acceptance-*` 中创建独立数据，保留以前结果。

## 包内内容

- `bin/`：core_cli、两个解析器测试程序、libc++.dll、libunwind.dll。
- `python/`：官方 Python 3.12.7 Windows x64 嵌入式发行包，原样包含自身许可和必要 DLL。无需安装 Python、pip 或设置 PATH。
- `tests/`：真实传输验收及固定 HTTPS 摘要；无需 Git 查询预期内容。TLS 测试证书只作不可信证书拒绝测试，不写系统信任库。
- `source.zip`：本项目源码、测试、构建脚本、固定的 aria2/libtorrent/try_signal 原始源码和实际 Boost 头文件。无 Git 元数据、无本机账户或凭据。
- `COPYING`、`LICENSES/`、`provenance/`：许可证、版权、版本、来源和归档校验值。
- `manifest.json`：包内文件 SHA-256，启动前核验。它是完整性清单，不是数字签名。ZIP 旁另有 `.sha256`。

`selftest.py` 使用自身包内的 Python，给子进程设置仅包含 Windows 系统目录的 PATH；核心运行库必须来自 bin。C++ 核心的 HTTPS 仍由 WinHTTP 默认验证证书。本机 BT/HTTP 的数据面、FileStore 和哈希实现没有改动。

## 生成和验收命令

以下是开发者命令，已由代理执行，不要求产品用户手动操作：

```powershell
python tools/package.py --output dist/HandmadeBT-0.5-gui-win64
python tools/accept_package.py dist/HandmadeBT-0.5-gui-win64.zip
```

生成器拒绝覆盖已有目录或 ZIP。再次生成应使用新目录名。

验收器从 ZIP 解压到新的含空格目录，从无关工作目录调用 CMD 启动器；子进程 PATH 仅含 Windows 目录。完整自测包括 HTTP/BT 强杀重启，完成后核验源码快照，再故意修改 README 验证完整性错误能在新下载开始前被拒绝，随后恢复该文件。0.5 最终报告保存在开发工作区的 `docs/GUI_ACCEPTANCE.md`，不会预先放入待测包。`PACKAGE_ACCEPTANCE.md` 是 0.1 历史记录。

当前记录证明在本机新目录可独立运行，不代表已经在全新 Windows 虚拟机、其他操作系统版本完成测试。未签名、未制作安装程序、未对外发布。

## 限制继续有效

BT 运行时仍是中间状态，只测试单文件 v1 和受控 peer；没有 Magnet、多文件/v2、浏览器接管、HLS/DASH；已有最小 HTTP/HTTPS GUI。HTTP 的重定向、认证、代理等兼容范围未增加。共享 FileStore 不是完整资源治理和完整任务管理。强杀恢复已验收，断电和长期公网负载仍未验收；见 `CRASH_RECOVERY.md`。

对应源码的 bootstrap 支持带哈希清单的无 Git 源码快照。首次重建仍需下载固定工具链；运行测试包不需要这些构建工具。本轮没有声称在另一台机器重编译成功或二进制逐字节可复现。
