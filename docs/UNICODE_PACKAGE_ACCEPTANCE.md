# 0.4 中文路径最终包验收（2026-10-26）

同一个 ZIP 分别在中文空格路径和英文空格路径独立解压，两套均通过：2 个单元测试程序、20 项在线集成检查、30 项强杀恢复与文件保护检查。每个完成文件核验 SHA-256；未降低原测试要求。测试项目包含准备与拒绝错误状态，不能当成 30 次独立下载。

- 最终包：`dist/HandmadeBT-0.4-unicode-win64-r1.zip`，47,704,320 字节。
- ZIP SHA-256：`d62f787c524470aa41fc748ab15dd5f536a57b1b1d1c1ef553f6701351941566`。
- exe SHA-256：`8cc36d5210d3077e84b412959573f23a658fe02e705d5b810f4011d25e94dbfd`，与直接测试 build 产物一致。
- 中文包根目录为 `中文 验收 …`，调用工作目录为 `无关 工作目录`；HTTP 文件名和 BT 种子/数据文件名也包含中文和空格。
- CMD 启动实际包内 Python，子进程 PATH 仅 Windows 系统目录，未依赖开发工具链。
- 两套均完成 2,428 个上游源码文件摘要验证、source.zip 自检及包文件完整性篡改拒绝检查。许可证及上游来源保留。
- 测试同时运行，不以本轮耗时作性能结论；两块合并保存逻辑未改。

证据：[中文路径验收](results/unicode-package-unicode-acceptance.json)、[中文下载](results/unicode-package-unicode-integration.json)、[中文恢复](results/unicode-package-unicode-crash.json)、[中文环境](results/unicode-package-unicode-environment.json)；[英文路径验收](results/unicode-package-ascii-acceptance.json)、[英文下载](results/unicode-package-ascii-integration.json)、[英文恢复](results/unicode-package-ascii-crash.json)。

[修复及旧版失败记录](UNICODE_PATHS.md)。初始 0.4 候选包因文档生成器仍残留“Unicode 未验证”而未交付，重新生成 r1 并验证实际包内源码文档已修正。该报告是最终验收后添加的外部工作区记录，不回写 ZIP。

## 已验证及未验证

已验证本机中文与空格安装/输出目录、文件名、真实 HTTP/HTTPS/BT、暂停/强杀恢复、原文件保护、原英文安装目录回归。没有修改系统区域设置。

未验证：其他 Windows 版本/区域设置、全新机器、超长路径、网络共享、emoji/所有特殊字符、断电和长期公网负载。现有包为 CLI 验证版；BT 仍限单文件 v1，没有新增协议或 GUI。本轮没有实际从包内源码在独立目录重编译，这是下一项交付验收。

复验：

```powershell
python tools/accept_package.py dist/HandmadeBT-0.4-unicode-win64-r1.zip --unicode
python tools/accept_package.py dist/HandmadeBT-0.4-unicode-win64-r1.zip
```

用户入口仍为解压后的 `run-selftest.cmd`。旧包和未验收候选保留作记录，推荐使用 r1。
