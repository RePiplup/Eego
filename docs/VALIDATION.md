# 本轮实际验收结果

执行日期：2026-10-26（Asia/Singapore）。成功运行目录：`test-output/run-20261026-173308-6a1882`。

## 状态

本机 Release 构建成功；CTest 2/2 通过（自写增量分片/Range 测试、13 个上游 header 测试方法）。集成测试 20 项通过。原始记录：[results/latest.json](results/latest.json)。

运行环境：Windows-11-10.0.26200-SP0，Intel64 Family 6 Model 183 Stepping 1, GenuineIntel，Python 3.12.7。
工具链、源码和依赖固定在 [../provenance/inputs.lock.json](../provenance/inputs.lock.json)。

受控文件大小：4,194,341 字节（4 MiB + 37）。每次完成的本机 HTTP/BT 文件均完整读取后核验 SHA-256：

`056393720e234ffaf2f5aa1caa1a53ae2369c9c6682414120fcc36cdbd1aa2ad`

可信 HTTPS 测试下载固定 aria2 提交的 COPYING，与本地 git 对象原始内容的 SHA-256 比较，不使用网页进度或内容长度代替校验。自签名 localhost 证书在测试时处于有效期、主机名匹配但不受信任，必须拒绝；未安装测试证书到系统信任库。

## 逐项结果

退出码 3 为主动暂停成功，退出码 1 为预期拒绝；表中 PASS 指断言达到预期，不是把下载失败当成功。

| 测试 | 结果 | 子进程墙钟秒 | 内容/行为检查 |
|---|---|---:|---|
| http-single | PASS（退出码 0） | 0.245 | SHA-256 一致 |
| http-segmented | PASS（退出码 0） | 0.109 | SHA-256 一致 |
| http-pause | PASS（退出码 3） | 0.058 | 状态/错误/计数断言 |
| http-resume | PASS（退出码 0） | 0.099 | SHA-256 一致 |
| http-range-unsupported | PASS（退出码 1） | 0.039 | 状态/错误/计数断言 |
| http-bad-range | PASS（退出码 1） | 0.048 | 状态/错误/计数断言 |
| http-short | PASS（退出码 1） | 0.048 | 状态/错误/计数断言 |
| http-identity-pause | PASS（退出码 3） | 0.057 | 状态/错误/计数断言 |
| http-identity-change-rejected | PASS（退出码 1） | 0.034 | 状态/错误/计数断言 |
| https-untrusted-rejected | PASS（退出码 1） | 2.112 | 状态/错误/计数断言 |
| https-trusted | PASS（退出码 0） | 4.474 | SHA-256 一致 |
| make-torrent | PASS（退出码 0） | 0.062 | 状态/错误/计数断言 |
| bt-upstream-baseline | PASS（退出码 0） | 0.738 | SHA-256 一致 |
| bt-filestore | PASS（退出码 0） | 0.988 | SHA-256 一致 |
| bt-backpressure | PASS（退出码 0） | 4.909 | SHA-256 一致 |
| bt-pause | PASS（退出码 3） | 3.262 | 状态/错误/计数断言 |
| bt-resume-with-recheck | PASS（退出码 0） | 4.046 | SHA-256 一致 |
| mixed-shared-filestore | PASS（退出码 0） | 4.826 | SHA-256 一致 |
| bt-wrong-size-rejected | PASS（退出码 1） | 0.023 | 状态/错误/计数断言 |
| existing-output-rejected | PASS（退出码 1） | 0.022 | SHA-256 一致 |

## 背压和恢复证据

- 延迟磁盘模式在真实文件写入前增加 3 ms 等待；仍经真实网络下载、实际 FileStore 写盘及完整 hash 验证，非模拟数据传输。
- 写请求 257，写完成回调 257，高水位返回 249 次，低水位唤醒 8 次，错误回调线程 0。
- 本轮最大待写字节 1048576；高水位不是硬内存上限，已有在途数据仍会到达。
- HTTP 暂停保存完整区间，第二个 CLI 进程只补未完成区间；改换 ETag 时拒绝拼接。
- BT 暂停保存 resume 文件，故意改坏输出首字节后由第二个 CLI 进程重校验并修复。最终 hash 正确，写块数小于完整重下的 257 块，仍复用了其他已验证数据。
- `mixed` 在一个进程共享一个 FileStore，公共写入计数 8388682 字节，等于两份文件总量；分别核验两份文件 hash。
- BT 已有错误尺寸文件被拒绝，原内容保持；HTTP 已有最终文件拒绝覆盖。

## 初步性能记录（不是性能优势结论）

| 场景 | CLI 内计时 ms | 文件大小/计时 MiB/s | 背压次数 | 唤醒次数 |
|---|---:|---:|---:|---:|
| http-single | 44 | 90.91 | — | — |
| http-segmented | 86 | 46.51 | — | — |
| bt-upstream-baseline | 705 | 5.67 | 0 | 0 |
| bt-filestore | 958 | 4.18 | 239 | 10 |
| bt-backpressure | 4882 | 0.82 | 249 | 8 |

单次本机回环、小文件、缓存状态未控制；BT 计时含建连、校验、关闭会话等，不能代表稳定吞吐。3 ms 人工延迟是压力验证，不是正常性能。没有测量 CPU/GiB、内存峰值或长期公网吞吐，不声称优于上游。FileStore 当前逐次打开文件、单磁盘工作线程，有明确优化空间。

## 未验证/未完成

强杀恢复的专项结果见 [CRASH_RECOVERY.md](CRASH_RECOVERY.md)，中文与空格路径专项见 [UNICODE_PATHS.md](UNICODE_PATHS.md)。尚未验证断电恢复、真实磁盘满/权限变化、压力长跑和全新 Windows 机器部署；当前软件不是完整下载产品。多文件 BT、Magnet、v2/hybrid、HTTP 重定向和一般认证/代理等尚未迁移；浏览器接管和 HLS/DASH 仅记录后续接口需求。详见 [MIGRATION.md](MIGRATION.md)。

本轮已执行命令：`./tools/build.ps1`、`python tests/integration.py --online`。网络测试在允许本机及公网连接的执行环境中运行；默认沙箱首次回环请求 12029 的失败记录未掩盖。
