from fixture_helpers import *
import psutil
h=Host(RUN/'edge-cases')
def wait(f,timeout=40):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  ts=h.list()
  if f(ts):return ts
  time.sleep(.15)
 raise AssertionError(ts)
try:
 urls=[]
 for n in range(4):
  name=f'edge-{n}.bin';urls.append(f'http://127.0.0.1:{server.server_port}/{name}')
  if n!=2:sources[name]=random.Random(n).randbytes(8*1024*1024+31)
 # All queued behind a live task, default 1: no request for the second URL yet.
 ids=[h.cmd('create',url=u)['id'] for u in urls]
 ts=h.list();assert sum(active(t) for t in ts)==1 and sum(t['status']=='queued' for t in ts)==3,ts
 assert not any(r['key']=='edge-1.bin' for r in request_log)
 baseline=psutil.Process(h.p.pid);one=dict(threads=baseline.num_threads(),handles=baseline.num_handles(),rss=baseline.memory_info().rss)
 h.cmd('settings',maxActiveTasks=2);wait(lambda ts:active(byid(ts,ids[1])))
 h.cmd('pause',id=ids[0]);ts=wait(lambda ts:byid(ts,ids[2])['status']=='failed' and active(byid(ts,ids[3])))
 note('failed queued HTTP frees slot, later eligible task fills, other running task survives')
 sources['edge-2.bin']=random.Random(2).randbytes(8*1024*1024+31)
 h.cmd('retry',id=ids[2]);ts=h.list();assert byid(ts,ids[2])['status']=='queued',ts
 note('retry after source repair obeys full limit')
 h.cmd('resume',id=ids[0]);h.cmd('settings',maxActiveTasks=4)
 ts=wait(lambda ts:sum(active(t) for t in ts)==4);before={t['id']:t['bytes'] for t in ts}
 ts=wait(lambda ts:all(t['bytes']>before[t['id']]+65536 for t in ts))
 four=dict(threads=baseline.num_threads(),handles=baseline.num_handles(),rss=baseline.memory_info().rss)
 note('upper bound 4 real HTTP tasks progressing',one=one,four=four)
 h.cmd('settings',maxActiveTasks=1);assert sum(active(t) for t in h.list())==4
 ts=wait(lambda ts:all(t['status']=='completed' for t in ts),90)
 for t in ts:assert hashlib.sha256(pathlib.Path(t['path']).read_bytes()).hexdigest()==hashlib.sha256(sources[t['url'].split('/')[-1]]).hexdigest()
 note('4 to 1 leaves existing transfers intact; retry payload and all final hashes correct')
 h.stop()
 # Missing local peer exercises metadata lifecycle and cooperative cancellation.
 h=Host(RUN/'metadata-cancel');u='magnet:?xt=urn:btih:'+'b'*40+'&x.pe=127.0.0.1:1';i=h.cmd('create',url=u)['id'];h.cmd('pause',id=i);wait(lambda ts:byid(ts,i)['status']=='paused');h.stop();note('no-peer BT metadata pause and shutdown complete')
 (RUN/'results.json').write_text(json.dumps(dict(status='PASS',checks=records),indent=2),encoding='utf8')
except Exception:
 (RUN/'failure.txt').write_text(traceback.format_exc(),encoding='utf8');raise
finally:
 h.stop();server.shutdown();(RUN/'http-requests.json').write_text(json.dumps(request_log,indent=2),encoding='utf8');print('EVIDENCE',RUN)
