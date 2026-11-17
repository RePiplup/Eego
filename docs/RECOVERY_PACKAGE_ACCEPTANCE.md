# 0.2 便携包实际验收

2026-10-26，本机 Windows。最终 ZIP 解压到新的含空格目录，从无关工作目录启动 CMD 自测；子进程 PATH 仅 `C:\WINDOWS\System32;C:\WINDOWS`，Python 来自包内。不是另一台全新机器验收。

- 包：`dist/HandmadeBT-0.2-recovery-win64.zip`，47,661,151 字节。
- SHA-256：`b413b132882eacf9beb3ad2fcc4e22229dab498955d1be68a548e59ddfbfda91`。
- 核心 exe SHA-256：`d88478aa9b76b8d18888276c5a81b8296dbf1f1229fda4ab89dcbd669fec08a1`；与本工作区 build/core_cli.exe 一致。
- 2 个单元测试程序、20 项在线集成检查、28 项强杀恢复检查全部通过。检查项包含准备、拒绝错误状态和真正下载，不能把 28 项都理解为独立下载次数。
- 真实 HTTP/BT 下载完成时核验 SHA-256；HTTPS 证书验证未关闭；同一 FileStore 混合下载核验两个文件。
- 包内 2,428 个上游源码快照文件逐一核验摘要，归档自检通过；保留源码、许可证和来源。
- 故意修改 README 后，完整性检查在新下载开始前拒绝运行，随后恢复原文件。
- 本次验收在线入口；更新后的离线入口未单独重复执行，不把在线结果冒充离线结果。

## 初步数据

仅一次本机受控传输，用于记录，不代表稳定吞吐或性能优势。每块持久化引入额外写盘开销，后续需要独立重复测量。

| 测试 | 核心报告耗时 ms |
|---|---|
| http-single | 31 |
| http-segmented | 122 |
| bt-filestore | 1182 |
| mixed-shared-filestore | — |

## 重现命令

```powershell
./tools/build.ps1
python tests/crash_recovery.py
python tests/integration.py --online
python tools/accept_package.py dist/HandmadeBT-0.2-recovery-win64.zip
```

也可解压包运行 `run-selftest.cmd`。生成器不覆盖已有包，重新打包须换输出目录。验收器每次新建目录，不覆盖用户文件。

## 证据和限制

- [最终包验收](results/recovery-package-acceptance.json)
- [在线回归](results/recovery-package-online.json)
- [真实强杀与恢复](results/recovery-package-crash.json)
- [运行环境](results/recovery-package-environment.json)
- [修复、旧版缺陷复现及兼容约束](CRASH_RECOVERY.md)

本报告在最终包验收后创建，放在开发工作区，不回写已验收 ZIP。0.1 历史包保留；0.2 安全拒绝旧记录和无归属的已有 BT 文件，不自动改写。BT 仍是完整运行时接入自定义存储的中间阶段，限单文件 v1；没有宣称迁移完整、断电安全、长期公网可靠或性能领先。浏览器接管、HLS/DASH、Magnet、多文件 BT 尚未实现。
