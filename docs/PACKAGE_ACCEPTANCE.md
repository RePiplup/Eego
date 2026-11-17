# 便携包实际交付验收（2026-10-26）

## 交付物

- ZIP：[HandmadeBT-0.1-validation-win64.zip](../dist/HandmadeBT-0.1-validation-win64.zip)，47,618,159 字节（45.41 MiB）。
- SHA-256：`5a02cc3f2bd002b4486744e953be525433ddca0184d8546089eaa97be12e6153`。
- 使用说明：[PORTABLE.md](PORTABLE.md)。解压双击 `run-selftest.cmd`，默认跑包含公网 HTTPS 的自测。
- 本轮没有改动内核 14 个源码文件；打包二进制与原已验证的 `build/core_cli.exe` 一致，SHA-256：`aaec6cefed326a93d6b065e165c7917d6cf2bb590efa62671cfc4a7a3f9f47c5`。

## 已实现且验证

| 项目 | 实际结果 |
|---|---|
| 新目录解压 | 从最终 ZIP 解压到含空格的 Clean Room 目录；从另一个无关工作目录启动 |
| 无开发工具 PATH | 启动环境及子进程 PATH 仅为 `C:\WINDOWS\System32;C:\WINDOWS`；包内 Python 3.12.7 与 bin 内 DLL |
| 完整入口 | CMD 启动器 → 包内 Python → 2 个单元测试程序 + 20 项集成测试，全通过 |
| 离线入口 | 2 个单元测试程序 + 19 项集成测试，全通过；公开可信 HTTPS 明确标记 SKIPPED |
| 真实内容核验 | HTTP、BT、恢复和混合下载输出完整 SHA-256 一致；混合测试分别记录两个文件摘要 |
| HTTPS | 公网可信证书下载内容正确；本机不可信证书被拒绝，未关闭验证或导入证书 |
| 共用存储 | 一个进程共享一个 FileStore；总写入 8,388,682 字节，与两份文件一致 |
| 完整性保护 | 故意修改 README 后启动返回 1，下载前拒绝，未生成新的测试运行目录；随后恢复文件 |
| 源码交付 | source.zip CRC 完整；逐文件核验 2428 个上游快照文件，并确认本项目、构建脚本、Boost 头文件存在 |
| 许可证 | 保留 aria2、libtorrent、Boost、LLVM/libc++/libunwind/compiler-rt、MinGW、Python 及其随附第三方许可 |

本次测试入口已经不依赖 Git 获取 HTTPS 预期内容：预期摘要来自固定 aria2 Git 对象，记录于 tests/fixtures/https-source.json。Python 嵌入式归档来源与 SHA-256 在包内 provenance/package-inputs.json。源码快照不含 .git 或开发机凭据。

## 证据

- [汇总](results/package-acceptance.json)
- [完整模式逐项结果](results/package-online.json) / [运行环境](results/package-online-environment.json)
- [离线模式逐项结果](results/package-offline.json) / [运行环境](results/package-offline-environment.json)
- 新目录根：`D:\CXworkspace\HandmadeBT\package-acceptance\Clean Room 20261026-013526 b2aa0f\HandmadeBT-0.1-validation-win64`。

实际执行：`python tools/package.py --output dist/HandmadeBT-0.1-validation-win64`；`python tools/accept_package.py dist/HandmadeBT-0.1-validation-win64.zip`；解压目录内 `run-selftest.cmd --offline`。

## 已解决的打包问题

1. 最初用 with_suffix 生成 ZIP，版本号的小数点被误认为扩展名，文件名截短。已改为保留完整目录名追加 .zip，最终归档重新生成、实际验收。
2. Windows 环境变量复制到普通 dict 后名称大小写不再透明；验收器读取 SystemRoot 出现 KeyError。改从 os.environ 读取，并同步修正便携入口；最终包已复验。
3. 保留候选阶段的失败记录；不将候选成功或旧目录运行冒充最终 ZIP 的验收。修复均限于打包/自测，不改下载协议。

## 未完成或未验证

仍是源码迁移验证版：BT 较完整 session 是中间状态，仅单文件 v1/受控 peer；Magnet、多文件/v2、通用 HTTP 重定向/认证/代理、浏览器接管、HLS/DASH、GUI 尚未实现。本轮不扩大这些能力。

已证明本机干净目录运行，而非全新操作系统验收。尚未在另一台机器、全新 Windows VM、中文路径验证；未代码签名或对外发布。强杀/断电恢复与长期负载尚未验收。source.zip 已核验内容，但本轮没有重新在全新机器编译源码；不承诺二进制逐字节可复现。

下一小阶段建议：对强制关闭后的单文件 HTTP/BT 恢复做真实故障验收，并修复发现的问题；不新增协议或 GUI。
