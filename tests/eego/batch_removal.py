from pathlib import Path
import subprocess,os,threading,http.server,time,json,queue,hashlib,shutil
R=Path(__file__).resolve().parents[2]
E=Path(os.environ.get('HBT_TEST_ROOT', R/'artifacts/test-runs/eego'))/('batch-'+str(time.time_ns()));E.mkdir(parents=True)
env=os.environ.copy();env['PATH']=str(R/'dist/Eego/core')+';'+env['PATH']
checks=[]
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*a):pass
 def do_GET(self):
  if 'fail' in self.path:self.send_error(503);return
  slow='slow' in self.path;data=(b'x'*65536)* (1024 if slow else 1)
  self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers()
  try:
   for i in range(0,len(data),65536):self.wfile.write(data[i:i+65536]);self.wfile.flush();time.sleep(.03 if slow else 0)
  except (ConnectionError,OSError):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
def note(n):checks.append(n);print('PASS',n,flush=True)
class Host:
 def __init__(self,folder):
  self.root=folder;folder.mkdir(exist_ok=True);self.q=queue.Queue();self.serial=0;self.events=[]
  self.p=subprocess.Popen([str(R/'dist/Eego/core/task_service_host_r3.exe'),'--data-dir',str(folder)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=open(folder/'stderr.log','w'),text=True,encoding='utf8',env=env,creationflags=subprocess.CREATE_NO_WINDOW)
  threading.Thread(target=lambda:[self.q.put(json.loads(x)) for x in self.p.stdout],daemon=True).start()
 def cmd(self,op,**kw):
  self.serial+=1;req=dict(op=op,requestId=str(self.serial),**kw);self.p.stdin.write(json.dumps(req)+'\n');self.p.stdin.flush()
  while True:
   res=self.q.get(timeout=30);self.events.append(res)
   if res['requestId']==str(self.serial):return res
 def ls(self):return self.cmd('list')['tasks']
 def wait(self,predicate):
  end=time.time()+40
  while time.time()<end:
   ts=self.ls()
   if predicate(ts):return ts
   time.sleep(.1)
  raise AssertionError(ts)
 def add(self,name):return self.cmd('create',url=f'http://127.0.0.1:{server.server_port}/{name}')['id']
 def stop(self):self.cmd('shutdown');self.p.wait(25);(self.root/'protocol.json').write_text(json.dumps(self.events,indent=2),encoding='utf8')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
h=Host(E/'library')
try:
 a=h.add('keep.txt');b=h.add('recycle.txt');c=h.add('unselected.txt');d=h.add('modified.txt')
 ts=h.wait(lambda ts:len(ts)==4 and all(t['status']=='completed' for t in ts));paths={t['id']:Path(t['path']) for t in ts};unselected=sha(paths[c])
 assert h.cmd('batch',action='remove',ids=[a,999999],deleteFiles=False)['ok']==False
 assert len(h.ls())==4;note('stale mixed-ID batch rejected before any mutation')
 assert h.cmd('batch',action='remove',ids=[a],deleteFiles=False)['ok'];h.wait(lambda ts:not any(t['id']==a for t in ts));assert paths[a].exists();note('record-only removal preserves completed file')
 assert h.cmd('batch',action='remove',ids=[b],deleteFiles=True)['ok'];h.wait(lambda ts:not any(t['id']==b for t in ts));assert not paths[b].exists();note('owned completed file recycled through existing guarded shell path')
 paths[d].write_bytes(b'changed outside downloader')
 assert h.cmd('batch',action='remove',ids=[d],deleteFiles=True)['ok'];h.wait(lambda ts:any(t['id']==d and t['status']!='removing' for t in ts));assert paths[d].read_bytes()==b'changed outside downloader';assert any(e['requestId']=='remove-error' for e in h.events);note('modified file veto preserves file and task, reports failure')
 assert h.cmd('settings',maxActiveTasks=2)['ok']
 slow=[h.add('slow-A.bin'),h.add('slow-B.bin'),h.add('slow-C.bin')]
 h.wait(lambda ts:sum(t['status'] in ('fetching','downloading') for t in ts)==2)
 assert h.cmd('batch',action='pause',ids=slow)['accepted']==3
 h.wait(lambda ts:all(t['status']=='paused' for t in ts if t['id'] in slow));note('batch pause handles active and queued tasks')
 assert h.cmd('batch',action='resume',ids=slow)['accepted']==3
 h.wait(lambda ts:sum(t['status'] in ('fetching','downloading') for t in ts)==2);note('batch resume retains concurrency two')
 assert h.cmd('batch',action='remove',ids=slow[:2],deleteFiles=False)['accepted']==2
 h.wait(lambda ts:all(t['id'] not in slow[:2] for t in ts));assert any(t['id']==slow[2] for t in h.ls());assert sha(paths[c])==unselected;note('active removal stops selected workers only; unselected file and queued task survive')
 failed=[h.add('fail-one'),h.add('fail-two')];h.cmd('batch',action='pause',ids=[slow[2]])
 h.wait(lambda ts:all(any(t['id']==i and t['status']=='failed' for t in ts) for i in failed))
 assert h.cmd('batch',action='retry',ids=failed+[c])['accepted']==2;note('retry mixed selection only applies to eligible failed tasks')
 immediate=h.add('slow-shutdown.bin');h.wait(lambda ts:any(t['id']==immediate and t['status'] in ('fetching','downloading') for t in ts));h.cmd('batch',action='remove',ids=[immediate],deleteFiles=False)
 h.stop();h=Host(E/'library');assert not any(t['id'] in [a,b,immediate]+slow[:2] for t in h.ls());assert any(t['id']==c for t in h.ls());note('removal persisted across host restart including immediate shutdown while removing')
 (E/'results.json').write_text(json.dumps(dict(status='PASS',checks=checks),indent=2),encoding='utf8')
finally:
 h.stop();server.shutdown()
