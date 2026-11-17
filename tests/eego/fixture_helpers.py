import pathlib,json,subprocess,threading,time,hashlib,http.server,socket,random,os,queue,traceback
BASE=pathlib.Path(os.environ.get('HBT_TEST_ROOT',pathlib.Path(__file__).resolve().parents[2]/'artifacts/test-runs/eego')).resolve();BASE.mkdir(parents=True,exist_ok=True)
RUN=BASE/('evidence-'+str(time.time_ns()));RUN.mkdir()
BIN=pathlib.Path(os.environ.get('HBT_CORE_BIN',pathlib.Path(__file__).resolve().parents[2]/'dist/Eego/core')).resolve();env=os.environ.copy();env['PATH']=str(BIN)+';'+env['PATH']
records=[];children=[];sources={};request_log=[]
def note(name,**kw):
 v=dict(case=name,time=time.monotonic(),**kw);records.append(v);(RUN/'results.json').write_text(json.dumps(dict(status='RUNNING',checks=records),indent=2),encoding='utf8');print(name,flush=True)
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*a):pass
 def do_HEAD(self):self.serve(False)
 def do_GET(self):self.serve(True)
 def serve(self,body):
  key=self.path.split('?')[0].lstrip('/');data=sources.get(key)
  if data is None:self.send_error(404);return
  start=0;end=len(data)-1;r=self.headers.get('Range');status=200
  if r:
   start,endstr=r[6:].split('-');start=int(start);end=int(endstr) if endstr else end;status=206
  self.send_response(status);self.send_header('Content-Length',str(end-start+1));self.send_header('Accept-Ranges','bytes');self.send_header('ETag','"'+hashlib.sha256(data).hexdigest()+'"')
  if status==206:self.send_header('Content-Range',f'bytes {start}-{end}/{len(data)}')
  self.end_headers();request_log.append(dict(t=time.monotonic(),key=key,range=r,body=body))
  if body:
   try:
    for at in range(start,end+1,16384):self.wfile.write(data[at:min(end+1,at+16384)]);self.wfile.flush();time.sleep(.125)
   except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
class Host:
 def __init__(self,root):
  self.root=root;root.mkdir(exist_ok=True);(root/'dht-bootstrap.txt').write_text('127.0.0.1:1\n');self.serial=0;self.q=queue.Queue();self.trace=[]
  self.err=open(root/'host-stderr.log','a',encoding='utf8');self.p=subprocess.Popen([str(BIN/'task_service_host_r3.exe'),'--data-dir',str(root)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.err,text=True,encoding='utf8',env=env,creationflags=subprocess.CREATE_NO_WINDOW);children.append(self.p)
  threading.Thread(target=lambda:[self.q.put(json.loads(x)) for x in self.p.stdout],daemon=True).start()
 def cmd(self,op,ok=True,**kw):
  self.serial+=1;v=dict(op=op,requestId=str(self.serial),**kw);self.p.stdin.write(json.dumps(v)+'\n');self.p.stdin.flush();r=self.q.get(timeout=25);assert r['requestId']==str(self.serial),r;self.trace.append(dict(time=time.monotonic(),request=v,response=r));(self.root/'protocol.json').write_text(json.dumps(self.trace,indent=2),encoding='utf8');assert r['ok']==ok,r;return r
 def list(self):return self.cmd('list')['tasks']
 def until(self,f,timeout=70):
  end=time.monotonic()+timeout
  while time.monotonic()<end:
   ts=self.list()
   if f(ts):return ts
   if any(t['status']=='failed' for t in ts):raise AssertionError(ts)
   time.sleep(.15)
  raise AssertionError(('deadline',ts))
 def stop(self):
  if self.p.poll() is None:self.cmd('shutdown');self.p.wait(30)
  self.err.close()
def active(t):return t['status'] in ('fetching','downloading')
def byid(ts,i):return next(t for t in ts if t['id']==i)
def fixture(kind,index):
 data=random.Random(index+97).randbytes(8*1024*1024+31);digest=hashlib.sha256(data).hexdigest()
 if kind=='http':
  name=f'http-{index}.bin';sources[name]=data;return f'http://127.0.0.1:{server.server_port}/{name}',digest
 seed=RUN/f'seed-{index}';seed.mkdir();file=seed/f'bt-{index}.bin';file.write_bytes(data);torrent=RUN/f'bt-{index}.torrent'
 subprocess.run([str(BIN/'core_cli.exe'),'make-torrent',str(file),str(torrent)],env=env,check=True,capture_output=True)
 with socket.socket() as so:so.bind(('127.0.0.1',0));port=so.getsockname()[1]
 uri=subprocess.check_output([str(BIN/'core_cli.exe'),'magnet-uri',str(torrent),str(port)],env=env,text=True).strip()
 log=open(RUN/f'seed-{index}.log','w');p=subprocess.Popen([str(BIN/'core_cli.exe'),'bt-seed',str(torrent),str(seed),str(port),'524288'],env=env,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW);children.append(p)
 return uri,digest

def scenario(label,kinds,offset):
 h=Host(RUN/label);ids=[];expected={}
 try:
  assert h.cmd('list')['maxActiveTasks']==1
  for j,kind in enumerate(kinds):
   uri,digest=fixture(kind,offset+j);i=h.cmd('create',url=uri)['id'];ids.append(i);expected[i]=digest
   if kind=='bt':
    h.until(lambda ts:byid(ts,i)['status']=='choosing');h.cmd('select',id=i,indexes=[0])
   h.cmd('pause',id=i);h.until(lambda ts:byid(ts,i)['status']=='paused')
  note(label+' default1 and staging',ids=ids)
  h.cmd('settings',maxActiveTasks=2)
  for i in ids:h.cmd('resume',id=i)
  ts=h.until(lambda ts:all(active(byid(ts,i)) for i in ids[:2]) and byid(ts,ids[2])['status']=='queued')
  before={i:byid(ts,i)['bytes'] for i in ids[:2]};ts=h.until(lambda ts:all(byid(ts,i)['bytes']>before[i]+65536 for i in ids[:2]))
  note(label+' real overlapping progress and limit queue',before=before,after={i:byid(ts,i)['bytes'] for i in ids[:2]})
  first,second,third=ids;h.cmd('pause',id=first)
  ts=h.until(lambda ts:byid(ts,first)['status']=='paused' and active(byid(ts,third)))
  note(label+' pause releases slot',states={t['id']:t['status'] for t in ts})
  h.cmd('resume',id=first);ts=h.list();assert byid(ts,first)['status']=='queued',ts
  h.cmd('settings',maxActiveTasks=1);ts=h.list();assert sum(active(t) for t in ts)==2,ts
  assert not any(t['pauseRequested'] for t in ts),ts
  h.cmd('pause',id=third);h.until(lambda ts:byid(ts,third)['status']=='paused')
  ts=h.list();assert active(byid(ts,second)) and byid(ts,first)['status']=='queued',ts
  note(label+' resume capped and reduction drains without interruption')
  h.cmd('settings',maxActiveTasks=2);h.until(lambda ts:active(byid(ts,first)));h.cmd('resume',id=third)
  h.stop();h=Host(RUN/label);r=h.cmd('list');assert r['maxActiveTasks']==2 and all(t['status']=='paused' for t in r['tasks']),r
  note(label+' clean restart persists settings and paused tasks')
  for i in ids:h.cmd('resume',id=i)
  h.until(lambda ts:sum(active(t) for t in ts)==2 and sum(t['status']=='queued' for t in ts)==1)
  # Completion must fill the waiting slot; all final payloads must match source.
  ts=h.until(lambda ts:all(t['status']=='completed' for t in ts),timeout=130)
  inventories={}
  for t in ts:
   selected=next(f for f in t['files'] if f['selected'])
   payload=pathlib.Path(selected.get('diskPath') or t['path'])
   actual=hashlib.sha256(payload.read_bytes()).hexdigest();assert actual==expected[t['id']],(t,actual)
   rec=(h.root/f"ownership-{t['id']}.record").read_text(encoding='utf8');inventories[t['id']]=rec
   for other in ts:
    if other['id']!=t['id']:assert other['path'] not in rec,(t,other,rec)
   assert str(payload).replace('\\','\\\\') in rec,rec
  note(label+' completion refill, ownership isolation, final hashes',hashes=expected)
  (h.root/'inventories.json').write_text(json.dumps(inventories,indent=2),encoding='utf8')
 finally:h.stop()
