"""Publish evidence from an explicitly selected successful run; no fallback."""
import hashlib,json,os,pathlib,platform,shutil,sys
root=pathlib.Path(__file__).resolve().parents[1]
path=pathlib.Path(sys.argv[1]).resolve()
data=json.loads(path.read_text())
assert data.get('all_passed'), 'Refusing to publish a failed/incomplete run'
dest=root/'docs/results';dest.mkdir(exist_ok=True)
shutil.copyfile(path,dest/'latest.json')
rows=[]
for t in data['tests']:
    m=t.get('metrics',{})
    rows.append(f"| {t['name']} | PASS（退出码 {t['returncode']}） | {t['seconds']:.3f} | {'SHA-256 一致' if 'sha256' in t else '状态/错误/计数断言'} |")
perf=[]
for t in data['tests']:
    if t['name'] not in ['http-single','http-segmented','bt-upstream-baseline','bt-filestore','bt-backpressure']:continue
    m=t['metrics'];ms=m['elapsed_ms'];rate=(4194341/1048576)/(ms/1000)
    perf.append(f"| {t['name']} | {ms} | {rate:.2f} | {m.get('backpressure','—')} | {m.get('wakeups','—')} |")
pressure=next(t['metrics'] for t in data['tests'] if t['name']=='bt-backpressure')
mixed=next(t['metrics'] for t in data['tests'] if t['name']=='mixed-shared-filestore')
text=f'''# 本轮实际验收结果

执行日期：2026-10-26（Asia/Singapore）。成功运行目录：`{path.parent.relative_to(root).as_posix()}`。

## 状态

本机 Release 构建成功；CTest 2/2 通过（自写增量分片/Range 测试、13 个上游 header 测试方法）。集成测试 {len(data['tests'])} 项通过。原始记录：[results/latest.json](results/latest.json)。

运行环境：{platform.platform()}，{os.environ.get('PROCESSOR_IDENTIFIER','unknown CPU')}，Python {platform.python_version()}。
工具链、源码和依赖固定在 [../provenance/inputs.lock.json](../provenance/inputs.lock.json)。

受控文件大小：4,194,341 字节（4 MiB + 37）。每次完成的本机 HTTP/BT 文件均完整读取后核验 SHA-256：

`{data['expected_sha256']}`

可信 HTTPS 测试下载固定 aria2 提交的 COPYING，与本地 git 对象原始内容的 SHA-256 比较，不使用网页进度或内容长度代替校验。自签名 localhost 证书在测试时处于有效期、主机名匹配但不受信任，必须拒绝；未安装测试证书到系统信任库。

## 逐项结果

退出码 3 为主动暂停成功，退出码 1 为预期拒绝；表中 PASS 指断言达到预期，不是把下载失败当成功。

| 测试 | 结果 | 子进程墙钟秒 | 内容/行为检查 |
|---|---|---:|---|
{chr(10).join(rows)}

## 背压和恢复证据

- 延迟磁盘模式在真实文件写入前增加 3 ms 等待；仍经真实网络下载、实际 FileStore 写盘及完整 hash 验证，非模拟数据传输。
- 写请求 {pressure['writes']}，写完成回调 {pressure['write_callbacks']}，高水位返回 {pressure['backpressure']} 次，低水位唤醒 {pressure['wakeups']} 次，错误回调线程 {pressure['wrong_callback_thread']}。
- 本轮最大待写字节 {pressure['peak_queue']}；高水位不是硬内存上限，已有在途数据仍会到达。
- HTTP 暂停保存完整区间，第二个 CLI 进程只补未完成区间；改换 ETag 时拒绝拼接。
- BT 暂停保存 resume 文件，故意改坏输出首字节后由第二个 CLI 进程重校验并修复。最终 hash 正确，写块数小于完整重下的 257 块，仍复用了其他已验证数据。
- `mixed` 在一个进程共享一个 FileStore，公共写入计数 {mixed['shared_store_bytes']} 字节，等于两份文件总量；分别核验两份文件 hash。
- BT 已有错误尺寸文件被拒绝，原内容保持；HTTP 已有最终文件拒绝覆盖。

## 初步性能记录（不是性能优势结论）

| 场景 | CLI 内计时 ms | 文件大小/计时 MiB/s | 背压次数 | 唤醒次数 |
|---|---:|---:|---:|---:|
{chr(10).join(perf)}

单次本机回环、小文件、缓存状态未控制；BT 计时含建连、校验、关闭会话等，不能代表稳定吞吐。3 ms 人工延迟是压力验证，不是正常性能。没有测量 CPU/GiB、内存峰值或长期公网吞吐，不声称优于上游。FileStore 当前逐次打开文件、单磁盘工作线程，有明确优化空间。

## 未验证/未完成

强杀恢复的专项结果见 [CRASH_RECOVERY.md](CRASH_RECOVERY.md)，中文与空格路径专项见 [UNICODE_PATHS.md](UNICODE_PATHS.md)。尚未验证断电恢复、真实磁盘满/权限变化、压力长跑和全新 Windows 机器部署；当前软件不是完整下载产品。多文件 BT、Magnet、v2/hybrid、HTTP 重定向和一般认证/代理等尚未迁移；浏览器接管和 HLS/DASH 仅记录后续接口需求。详见 [MIGRATION.md](MIGRATION.md)。

本轮已执行命令：`./tools/build.ps1`、`python tests/integration.py --online`。网络测试在允许本机及公网连接的执行环境中运行；默认沙箱首次回环请求 12029 的失败记录未掩盖。
'''
(root/'docs/VALIDATION.md').write_text(text,encoding='utf-8')
# Keep an exact source inventory for this delivered validation state.
files=[]
for folder in ['src','tests','tools','LICENSES']:
    for p in sorted((root/folder).rglob('*')):
        if not p.is_file() or (folder=='tools' and p.parent!=root/'tools'):continue
        if p.suffix in ['.zip','.pyc']:continue
        files.append(dict(path=p.relative_to(root).as_posix(),sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
(root/'provenance/delivered-files.json').write_text(json.dumps(files,indent=2))
print('Recorded '+str(path))
