> 0.6 增量：Magnet、多文件 FileStore 映射与浏览器 IPC 已实现，见 [MAGNET_BROWSER.md](MAGNET_BROWSER.md)。下文为最初迁移基线记录，其当时未实现项以增量文档为准。

# 本轮迁移记录（2026-10-26）

本文件记录初始迁移边界。0.2 新增的恢复记录封装、原子保存、文件归属和任务锁见 [CRASH_RECOVERY.md](CRASH_RECOVERY.md)；协议范围及上游基线不变。

## 固定基线

| 输入 | 固定值 |
|---|---|
| aria2 | `9e7273583f83e881e3ec067b523ba88724088d2f` |
| libtorrent RC_2_0 | `eb397173da6189dad52f008916c27419dde32faa` |
| try_signal | `105cce59972f925a33aa6b1c3109e4cd3caf583d` |
| 仅用于差异检查的 RC_2_1 | `87b553a1813bb23f77311d0063461b731ba35bdb` |
| llvm-mingw | `20261020-ucrt-x86_64`；实际 clang `23.1.4`，LLVM commit `7a941dfb2877a0b8cf30323fefa85ad019e0b0f5` |
| CMake / Ninja wheel | `3.31.6` / `1.11.1.3`；Ninja 自报 `1.11.1.git.kitware.jobserver-1` |
| Boost | 官方 `1.82.0` tar.bz2，仅使用头文件 |
| 本机测试脚本运行时 | Python `3.12.7`，Windows x64 |

实际构建使用官方 Boost 归档。最初探测过 Anaconda 已有 Boost，并在初次运行中使用；最终验收已切换官方归档，不再依赖 Anaconda 的 C++ 库。归档校验值和版本在 `provenance/inputs.lock.json`。输入固定不等同于承诺二进制逐字节可复现。

2.0/2.1 只检查了 `disk_interface.hpp`、`session.cpp` 和磁盘实现相关差异：2.1 将 `status_t` 改为位标志/`disk_status`，调整磁盘实现；外部 io_context 和磁盘工厂的接入方式仍存在。此次自定义后端不依赖 2.1 新磁盘实现，选择已审计 2.0，避免额外适配。证据：`provenance/bt-2.0-2.1-interface.diff`。没有维护第二套后端。

## 迁移来源清单

| 上游来源 | 工程文件 | 保留/调整 |
|---|---|---|
| aria2 `src/HttpHeaderProcessor.{h,cc}` | `src/aria2/` 同名文件 | 保留增量解析状态机；替换 utility/异常/common 依赖；常量展开；没有 Command 或 DownloadEngine |
| aria2 `src/HttpHeader.{h,cc}` | `src/aria2/` 同名文件 | 保留头字段存储、Range 解析；新增 ETag 字段以约束续传身份 |
| aria2 `src/Range.{h,cc}` | `src/aria2/` 同名文件 | 保留值类型和运算；移除未使用 unistd 依赖 |
| aria2 `test/HttpHeaderProcessorTest.cc` | `tests/upstream_header_test.cpp` | 保留原 13 个测试方法，替换 CppUnit 注册/断言框架；断言失败用独立 logic_error，不能被预期解析异常吞掉 |
| libtorrent `examples/custom_storage.cpp` | `src/bt_disk.cpp` | 借鉴接口实现结构；原 RAM 存储被替换为真实 FileStore 队列；新增背压、屏障、错误和回调计数。原完整版权在文件头和 `LICENSES/libtorrent-custom-storage.txt` |
| libtorrent `src/peer_connection.cpp`, `request_blocks.cpp`, `piece_picker.cpp`, `torrent.cpp` 等 | `audit/upstream/libtorrent` 原路径编译 | 原样保留，不改 peer 请求、picker、choking、uTP；完整运行时是本阶段明确允许的中间状态 |

`provenance/http-files.json` 保存每个 HTTP 来源文件的原始 SHA-256。原始仓库保留完整许可。`tools/import_http.py` 重现代码提取，修改点集中在包含文件、ETag 和测试 harness。

## 新代码承担的职责

- `src/http.cpp`：WinHTTP 管理 DNS/TCP/TLS 和正文传输；将原始响应头交给迁入的 aria2 解析器，依解析结果判断状态码/Range/资源身份。WinHTTP 本身也做 HTTP framing，因此本轮并非移植了 aria2 整套网络栈。
- 新分段执行器：最多 16 个工作线程、256 KiB 单位领取区间；完整区间完成后才登记。暂停等待在途区间结束、FlushFileBuffers 后保存 checkpoint。恢复验证 URL/大小/强 ETag；失败不将 `.part` 发布为最终文件。
- `src/file_store.cpp`：一个 FIFO 工作队列，真实 Win32 读写和 flush；文件访问通过该实现完成。HTTP 使用 future 等待写完成，BT 在同一队列上读/写/hash，再 post 回 libtorrent 网络线程。
- `src/bt_disk.cpp`：复制输入块确保缓冲区生命期；高水位返回 true；低水位调用去重后的 disk_observer；检测回调线程；停止/清 piece 与先前 I/O 排序。读/hash 与写共享队列。读写错误送回 BT，flush 错误计入验收失败。
- `mixed`：HTTP 与 BT 共用一个 shared_ptr<FileStore>，同时下载两个独立文件；并非两个 FileStore 实例共用类名。测试核验两份文件和公共写入字节总量。

本轮未迁移 aria2 的完整 HTTP Command 链、SegmentMan、镜像/代理/认证/重试/解码及任务管理。未统一 BT session 和 HTTP 线程执行模型、全局连接预算、任务数据库、统一持久化格式。HTTP 使用本项目 checkpoint；BT 使用 libtorrent resume payload 并全量重校验。没有把这些未完成项目计作融合成果。

默认 BT 磁盘后端只出现在受控 seed 和显式 `bt-baseline-get` 测试；`bt-get` 与 `mixed` 的下载 session 指定了自定义工厂，下载写入不调用默认后端。未使用 aria2 完整可执行程序作基线：aria2 相关基线是迁移前已有的解析器测试方法，不是完整 aria2 性能比较。

## 后续确定需求需要的任务入口（仅记录，未实现空接口）

- 浏览器接管：提交 URL、目标名称、来源页面、必要请求头和安全凭据引用；返回持久 TaskId；可查询、暂停、恢复、取消。Cookie/Authorization 不应默认写明文日志或 checkpoint。
- HLS/DASH：父任务、子片段任务、指定字节区间、预期长度/摘要、输出顺序、依赖和取消传播；复用 HTTP 传输；容器合并属于后处理。保留直播清单更新需要的追加任务能力。
- 本轮 CLI 的暂停是显式完整块边界暂停并退出；尚无稳定对外 TaskService，也不宣称浏览器接管或视频流已实现。

## 实施中实际遇到的问题

1. 本机 PATH 无 C++ 工具链/CMake：使用项目本地便携输入，未改系统设置。
2. GitHub API 查询遇到 rate limit：通过公开 release 页面定位固定归档，随后以 SHA-256 锁定。
3. Ninja wheel 的 exe 不在预期 `ninja/data/bin`：修改脚本为实际 `ninja-1.11.1.3.data/scripts`。
4. HTTP compatibility header 缺少 `<iterator>`：编译报 `no member named back_inserter in namespace std`，补齐头文件。
5. 沙箱中本机 WinHTTP 请求报 12029；允许本机测试网络后所有真实传输通过。未关闭证书验证或系统防火墙。
6. 工具链对上游产生大量 lifetime-safety 建议警告；保持上游不改动，构建完成。没有将警告描述为已修复。
7. bootstrap 在沙箱执行 Git 的 shell 子命令遇到 `NtCreateDirectoryObject 0xC0000022`；已安装子模块现在直接校验 SHA，首次获取仍需普通网络/进程权限。
