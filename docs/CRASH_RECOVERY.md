# 强制关闭恢复：修复及验收（0.2）

## 实际发现与修复

旧版真实复现：HTTP 收到数块数据后被 TerminateProcess 结束，再启动返回 `FileStore Windows error 80`，没有有效恢复记录；BT 对没有归属标记的同名、同尺寸文件重新下载并改写。全部复现都使用新建测试目录里的自产数据，没有操作用户文件。旧 BT 的正常内容重校验恢复本来可用，本轮保留它，只补文件归属保护。

修复：

- HTTP 创建数据文件前先记录任务身份。每个完整 256 KiB 区间下载完成后，经公共 FileStore 队列先 FlushFileBuffers，再以临时记录 + MoveFileEx 原子替换正式记录。保存的不只是位图，还包括块 SHA-256、URL、强 ETag、总长度和目标绝对路径。
- HTTP 重启严格校验记录格式、总校验和、身份、文件尺寸，再重读已完成块计算摘要；损坏块重新下载，正确块复用。没有有效记录的已有 `.part` 拒绝接管，旧数据保持。
- BT 在首次创建数据前保存 info-hash、路径和长度归属记录。没有归属的已有数据拒绝覆盖；错误尺寸/错误 torrent/损坏归属记录均拒绝。
- BT 原生 resume payload 包在同一带 SHA-256 的记录封装中原子保存。加载时只确认其 torrent 身份，不导入保存的路径重映射或信任标志；始终使用指定 .torrent 和自定义 FileStore 的完整重校验。下载中强杀即使没有原生 resume，也凭归属记录和 piece 校验重用数据。
- 两种任务均用 Windows 独占句柄锁防止同一任务被第二个进程同时写入。进程被强杀后 OS 自动释放句柄；锁文件不需要删掉。
- 存档残片 `.tmp-*` 不用于恢复，不自动清理未知文件。校验和用于发现意外损坏，不是签名或对抗本机恶意篡改的权限边界。

代码：`src/checkpoint.{hpp,cpp}`、`src/http.cpp`、`src/main.cpp`。`src/bt_disk.cpp`、piece picker、peer、choking、uTP、上游源码和许可证没有改动。无新协议/GUI/全局资源框架。

## 本机测试

用 8 MiB + 37 字节自产内容，在已核实至少三个完整块写入后真正强杀下载子进程，HTTP 另有连续两次强杀。不是主动暂停，也不是模拟数据。测试由 `tests/crash_recovery.py` 执行；临时 HTTP 服务慢速发送实际正文，BT 使用受控真实 seed。

受控文件 SHA-256：`b08dc981e218c23cc91a64d8612cc9cd986173c1f710ef5547b8b6bceea2de18`。

28 项检查全部通过（包括测试准备、实际强杀、负例与内容校验）。既有 20 项 HTTP/HTTPS/BT 集成测试和 2 个单元测试程序也通过；可信 HTTPS 及拒绝不可信证书仍有效。

| 检查 | 结果 | 证据类型 |
|---|---|---|
| http-hard-kill-duplicate-rejected | 安全拒绝 | 状态/保护断言 |
| http-hard-kill | 真实强杀 | 状态/保护断言 |
| http-restart-rechecks | 通过 | SHA-256 一致 |
| http-kill-first | 真实强杀 | 状态/保护断言 |
| http-kill-again | 真实强杀 | 状态/保护断言 |
| http-restart-after-two-kills | 通过 | SHA-256 一致 |
| http-prepare-corrupt | 通过 | 状态/保护断言 |
| http-reject-corrupt | 安全拒绝 | 状态/保护断言 |
| http-prepare-identity | 通过 | 状态/保护断言 |
| http-reject-identity | 安全拒绝 | 状态/保护断言 |
| http-prepare-wrong-url | 通过 | 状态/保护断言 |
| http-reject-wrong-url | 安全拒绝 | 状态/保护断言 |
| http-prepare-missing-record | 通过 | 状态/保护断言 |
| http-reject-missing-record | 安全拒绝 | 状态/保护断言 |
| http-prepare-wrong-size | 通过 | 状态/保护断言 |
| http-reject-wrong-size | 安全拒绝 | 状态/保护断言 |
| make-torrent | 通过 | 状态/保护断言 |
| bt-hard-kill-duplicate-rejected | 安全拒绝 | 状态/保护断言 |
| bt-hard-kill | 真实强杀 | 状态/保护断言 |
| bt-restart-rechecks | 通过 | SHA-256 一致 |
| bt-unrelated-same-size | 安全拒绝 | 状态/保护断言 |
| bt-prepare-record | 通过 | 状态/保护断言 |
| bt-reject-corrupt-record | 安全拒绝 | 状态/保护断言 |
| bt-reject-corrupt-ownership | 安全拒绝 | 状态/保护断言 |
| bt-reject-mismatched-resume | 安全拒绝 | 状态/保护断言 |
| make-other-torrent | 通过 | 状态/保护断言 |
| bt-reject-wrong-torrent | 安全拒绝 | 状态/保护断言 |
| bt-original-record-still-works | 通过 | SHA-256 一致 |

证据：[旧版复现](results/crash-baseline.json)、[修复后记录](results/crash-recovery.json)、[既有回归](results/latest.json)。旧版复现标记 `all_passed:false`，它是缺陷证据，不计作通过。

初次修复后，HTTP 强杀并破坏一个块再恢复只写入 7,602,213 字节，小于全文件 8,388,645 字节；BT 相同情境写入 481 块，小于完整下载的 513 块；两者最终 SHA-256 一致。连续两次 HTTP 强杀后也完成并核验内容。最终便携包另从新目录复验，结果见开发工作区 `RECOVERY_PACKAGE_ACCEPTANCE.md`。

## 性能与限制

本轮采取每个完整块一次持久化，保证恢复边界简单可验证，会增加 flush 和恢复记录写入量。回归中的 4 MiB 分段 HTTP 单次耗时 134 ms（本机测试与其他测试有重叠），不能作为稳定性能结论。未声称速度提升；后续应单独基准测量并考虑有界合并保存，不能牺牲数据先于进度落盘的顺序。

这是进程强杀恢复验收，不是物理断电、磁盘掉电缓存或文件系统故障验收。不支持 Range 的 HTTP 任务仍不能续传，已有未登记部分文件会安全拒绝。目标路径移动、硬链接/reparse-point 的完整安全策略、磁盘满/权限突变和长期公网负载不在本轮验证范围。

0.2 不自动接管 0.1 的 HBT1 记录或未带归属的旧 BT 数据，明确报错并保留；没有静默删除或破坏性迁移。缺失/损坏的正式记录也不会猜测恢复。完成文件始终拒绝覆盖，便携包的测试数据全在新目录内。

## 命令

```powershell
./tools/build.ps1
python tests/crash_recovery.py
python tests/integration.py --online
python tools/package.py --output dist/HandmadeBT-0.2-recovery-win64
python tools/accept_package.py dist/HandmadeBT-0.2-recovery-win64.zip
```

更新的一键自测同时运行普通下载回归和强杀恢复检查，仅结束自测创建的下载子进程。旧 0.1 包保留作历史基线，不再推荐用于试用。
