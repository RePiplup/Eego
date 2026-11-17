# 恢复记录性能验收（0.3，2026-10-26）

## 结论及最小改动

先增加测量而不改变行为，确认恢复记录持久化开销，再将 src/http.cpp 中每完成一块保存改为每完成两块保存。保存仍在同一个 FileStore 队列内完成：数据 FlushFileBuffers → 写入并刷新恢复记录临时文件 → 原子替换。没有关闭刷盘、证书检查或哈希重验，没有改变 BT 算法。开始下载前的归属记录、暂停/错误/结束时剩余块的保存保持。

统计 checkpoint_count 包含首次归属、周期保存和退出前最后一次保存；checkpoint_us 是持久化操作实际耗时之和（含 flush、记录创建/写入/刷新/替换），checkpoint_wait_us 是调用保存的等待时间（含序列化、排队和执行）。两者不可相加，也不是纯磁盘硬件耗时。统计不含最终完成文件的独立 flush。

修正测试服务器 listen backlog 后，32 MiB 四路的基线持久化耗时中位数约 514 ms，总下载墙钟中位数约 613 ms，结合保存路径串行等待确认存在明显开销。两块合并后减少约一半保存次数，六组墙钟中位数下降约 23%–32%；这是本机本轮受控结果，不是公网性能优势或跨硬件承诺。

## 方法与结果

自产随机文件 4 MiB+37 字节、32 MiB+37 字节；真实本机 TCP/HTTP Range；1/4/8 路，每组 5 次，随机顺序逐个运行。优化前后各 30 次正式测量及一次预热，62 个完成文件全部 SHA-256 正确。哈希验证在计时之外；耗时包含 CLI 启动退出。服务端内容在内存中；Windows 缓存未清空，未控制杀毒、温度或系统其他负载。先基线后优化，未做交错 A/B，不能排除时间漂移；不使用单次最好结果。

| MiB（另有尾部字节） | 并发 | 原中位 ms | 新中位 ms | MiB/s 原/新 | 保存次数 原/新 | 保存耗时中位 ms 原/新 |
|---|---|---|---|---|---|---|
| 4.00 | 1 | 185.9 | 143.2 | 21.5 / 27.9 | 19 / 10 | 96.0 / 53.1 |
| 4.00 | 4 | 147.5 | 111.3 | 27.1 / 35.9 | 19 / 10 | 90.6 / 54.8 |
| 4.00 | 8 | 142.9 | 109.6 | 28.0 / 36.5 | 19 / 10 | 91.0 / 54.7 |
| 32.00 | 1 | 970.4 | 664.7 | 33.0 / 48.1 | 131 / 66 | 559.9 / 303.9 |
| 32.00 | 4 | 612.6 | 438.9 | 52.2 / 72.9 | 131 / 66 | 514.2 / 337.2 |
| 32.00 | 8 | 613.8 | 429.5 | 52.1 / 74.5 | 131 / 66 | 522.1 / 344.2 |

全部单次值、最小最大值、exe 摘要在 [优化前](results/checkpoint-before.json) / [优化后](results/checkpoint-after.json)。初次探索发现 8 路小文件波动，原因候选为 Python 服务器默认小 listen backlog；将 backlog 在 bind/listen 前设置为 64 后重跑全部基线，才与优化版比较。探索记录保存在 test-output/benchmark-20261026-132909-3cc3fa，不混入上表。

## 恢复语义与边界

合并期间可能有至多两块已完成但未提交恢复记录（512 KiB），强杀后需重新下载；另有每路正在接收的未完成块，不计入该界限。这是数据量边界，不是时间保证；极慢或卡住的网络下单块可等待下一块完成。保存中的强杀只能恢复旧记录或完整新记录，仍重读并校验每个记录块。正常暂停/错误退出/结束时保存剩余一块。无记录的数据绝不被当作已完成。文件保护和任务锁不变。

保留原 28 项恢复/保护检查，增加第一块完整写入但首个合并检查点尚未提交时强杀、随后恢复并核验哈希；断言检查点的已完成块数为零，避免只靠测试名称假设时机。使用 tests/crash_recovery.py（现在 30 项）。断电、磁盘满、低速公网长跑仍未验证。

0.2 的 HTTP2 检查点格式没有改变，新版继续校验它；0.1 旧格式仍拒绝。BT 归属/原生恢复与全量 recheck 未修改，仍是单文件 v1 中间运行时。

## 重现

```powershell
./tools/build.ps1
python tests/checkpoint_benchmark.py --label two-blocks --batch 2
python tests/crash_recovery.py
python tests/integration.py --online
python tools/package.py --output dist/HandmadeBT-0.3-checkpoint-win64
python tools/accept_package.py dist/HandmadeBT-0.3-checkpoint-win64.zip
```

基线保留在 test-output/performance-baseline（exe 与运行库），运行命令：
`python tests/checkpoint_benchmark.py --exe test-output/performance-baseline/core_cli.exe --label per-block --batch 1`。
[基线 HTTP 源码](results/http-per-block-baseline.cpp) 保存在文档证据中；其余核心源一致，可在独立源码副本中替换该文件后重建，不能覆盖当前源码进行日常运行。固定上游、工具链和许可证见 provenance 与 LICENSES。

最终包测试报告将在验收后写入工作区 PERFORMANCE_PACKAGE_ACCEPTANCE.md，不预先放入待验收包。
