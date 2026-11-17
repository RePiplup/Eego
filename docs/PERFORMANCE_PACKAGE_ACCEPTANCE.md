# 0.3 最终便携包验收（2026-10-26）

通过：2 个单元测试程序、20 项在线集成检查、30 项恢复/保护检查。原 28 项保持，新增两项验证首块已写入但未提交批量检查点时强杀和恢复；实际记录已完成块数为零，恢复后 SHA-256 一致。检查项包含下载准备及拒绝错误状态，不能当成 30 次独立下载。

- ZIP：`dist/HandmadeBT-0.3-checkpoint-win64.zip`，47,688,807 字节。
- SHA-256：`2b728d1a6d853e80d08731497f4749dc79d2730e6359dc7aa8cd977387c36e6a`。
- exe：`714147a7ff87404abc9bcabcbac049daa0905708b7d20e6d9dfc15b8af125a35`，与已做性能测量的 build 产物一致。
- 解压到新建含空格目录、从无关工作目录运行，子进程 PATH 仅 Windows 系统目录，使用包内 Python 与 DLL。
- 2,428 个上游源码文件摘要核验通过；完整 source.zip 自检通过。许可证/来源保留。
- 修改 README 的完整性负例被拒绝，之后恢复；所有完成下载核验内容。
- 本次执行在线入口；没有单独重跑离线入口，没有在全新机器或中文目录验收。

详细 [性能方法及优化前后数据](CHECKPOINT_PERFORMANCE.md)。本机中位数变化不是公网速度承诺，强杀恢复不是断电安全承诺。现有 BT 仍为单文件 v1 中间子系统；没有扩展协议或 GUI。

证据：[包验收](results/performance-package-acceptance.json)、[在线回归](results/performance-package-online.json)、[恢复与保护](results/performance-package-crash.json)、[环境](results/performance-package-environment.json)。报告在包验收后生成，不改写已验收 ZIP；0.1/0.2 历史包保留。

复验：`python tools/accept_package.py dist/HandmadeBT-0.3-checkpoint-win64.zip`。一键试用入口为包内 `run-selftest.cmd`，详细构建及基准命令见 CHECKPOINT_PERFORMANCE.md。
