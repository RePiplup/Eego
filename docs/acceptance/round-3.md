# 第三轮交付 · 2026-11-02

本轮核心、UI、故障验证并行推进，主任务负责协议与整合。独立构建 `build-round3`、服务 `integration-round3`、故障证据 `kernel-round3-validation`，原有 dist 和日常 userdata 不覆盖。

## 可运行入口与试用

双击项目根 `HandmadeBT-Round3.cmd` 或本目录 `Start-UI.cmd`，打开 http://127.0.0.1:18768/?mode=real 。需要现有本机Python；已运行时直接打开页面。

1. 确认右上“真实内核”，点击“BT 多文件测试”，等待元数据后选择文件并开始。
2. 点击任务名称，在详情观察每个选中文件的字节和进度条；测试做种节点刻意限速128 KiB/s，便于观察。未选文件标记未选择，未知进度不伪造百分比。
3. 下载中点击“测试重启恢复”。它有序重启本预览的隔离内核，保持测试源在线；原任务恢复为暂停，详情显示待校验，逐文件进度暂为未知。
4. 点击继续，内核核验已有数据后更新实际文件进度，最终完成；这是可点击的有序重启演示，独立强杀证据另列。
5. 可使用HTTP测试、新建URL及暂停/重试。右上切换模拟模式，体验恢复/校验提示；独立 `../ui-next/Start-Mock.cmd` 不依赖内核。模拟状态始终明确标识。

任务文件位于本目录 `runtime/userdata`。关闭浏览器不会停止服务；服务终端Ctrl+C停止它。整个预览服务重启会更换自产测试源端口，旧本机测试链接可能失效，需新建测试任务；页面“测试重启恢复”只重启内核，保留源端口。真实外部链接不受测试源端口影响。

## 能力实测（不等同架构完成）

| 项目 | 结果与边界 |
|---|---|
| HTTP事件增量 | 通过；真实完成事件包含任务快照，seq单调，无旧实例混用 |
| 实际UI恢复闭环 | 通过；浏览器任务3全选三文件，非零逐文件进度→点击有序重启→全部待校验→继续→全部完成，三个文件精确大小/SHA256独立核验一致 |
| BT真实逐文件进度 | 通过；捕获非零且未完成的piece级文件字节，不按预分配文件大小估算 |
| 强杀重启语义 | 通过；重启后recheck_required、选中文件completed=null，继续后两文件独立哈希一致，未选文件不存在 |
| 源侧连接中断恢复 | 通过；HTTP Range源中途TCP reset，任务进入failed；恢复源后手动retry复用checkpoint，最终SHA256一致 |
| 磁盘访问冲突恢复 | 通过；对本任务.part持有真实Windows共享锁触发FileStore错误32，解除后手动retry完成并独立核验SHA256 |
| 事件丢失/重启 | 通过；instance变化、过期/超前游标要求全量快照重同步，重复/无变化不重复应用 |
| 严格 DHT-only | 本轮实测未通过发现验收；181.30秒、90个样本，节点/对端/候选均为0，未取得元数据；根因尚未确定 |
| 自动断网重试 | 未实现/未宣称；本轮使用显式手动retry |
| 全机断网、磁盘满、断电、物理介质故障 | 尚未验证；源侧RST和共享锁不代表这些场景 |

DHT-only的限定时长结果及诊断见 `../kernel-round3-validation/REPORT.md` 和原始公网记录。测试采用新的userdata，仅xt/dn Magnet，关闭tracker、LSD、PEX、显式peer与web seed；没有把上一轮含tracker的公网成功当作本轮DHT-only成功。只读DNS诊断确认解析成功，但这不证明UDP可达，也不足以断言特定内核缺陷或网络阻断。

## 统一内核架构进度

已完成：TaskSnapshot/FileSnapshot/RecoverySnapshot/Event DTO归TaskService；host只序列化。任务vector改私有，删除请求、停止等待、归属核验、持久化回滚和移除进入服务；GUI注入回收动作，painted展示缓存移回GUI。新版UI按共同DTO和事件游标更新，不读取原生控件或自行映射内核状态。

事件保留512条，250ms节流；每个服务实例有独立instanceId，实例更换/游标过期时全量重同步。它是内存事件流加短轮询传输，不是持久日志或SSE。接口约定见 `CONTRACT.md`。

仍未完成：legacy_tasks视图仍允许间接访问内部对象，原生GUI部分展示仍读内部任务；Win32 Inventory/部分字段仍耦合；HTTP和BT运行时/恢复格式/多数错误类型尚未统一；仍单活跃队列，全局资源预算和FileStore观察者不支持直接开启安全多任务并发。稳定DTO是已实现的对外边界，不声称整个内部模型已经不可变。

BT文件进度是piece已核验字节，不等于fsync持久化偏移；重启后的任务记录字节数也需重新核验。UI以明确提示区分这两种信息。

## 证据与责任

- 核心源码/构建/原生回归：`../kernel-round3/REPORT.md`，最终CTest 5/5；原生manager完整回归ALL PASS。
- 独立故障与公网：`../kernel-round3-validation/REPORT.md`；`faults-2df34128/results.json`、`run-e725ca36/results.json`、`public-36efabad/results.json`。
- 整合接口与内容：`runtime/api-acceptance.json`、`runtime/ui-output-verification.json`。
- 浏览器操作与真实画面：`../ui-next/evidence-round3/browser-results.json`、`real-file-progress.jpg`及同目录恢复/完成画面。
- 最终原生GUI额外修复同批部分删除失败后的旧视图引用；新增回归并再次通过manager完整验收，`../kernel-round3/manager08-221572b6/results.json`。GUI修改不改变本轮被测host哈希。
- 核心最终host：`build-round3/task_service_host_r3.exe`，SHA256 `d4e3dcdcba77f80c1643a30b72436da3161ad35e88e83ba340c3aa605a96418f`。本轮验证绑定该构建，不默认复用历史结果。

## 下一轮优先

继续去掉legacy可变对象访问，让原生GUI也完全按DTO渲染；细化网络/磁盘/校验错误与恢复策略。依据本轮DHT-only证据定位发现链路，再扩展有边界的网络与磁盘故障测试。任务并发和全局预算需先消除共享回调限制，不能仅通过同时启动worker实现。
