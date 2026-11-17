"""Run isolated real-core acceptance against the exact DLL/host payload shipped by the Qt dev build.
No UI automation: UI approval and visual acceptance are separate.
"""
from pathlib import Path
import hashlib,http.server,json,os,queue,subprocess,threading,time
ROOT=Path(__file__).resolve().parents[2]
RUN=ROOT/'artifacts/test-runs'/'eego-core'/str(time.time_ns())
RUN.mkdir(parents=True)
EXE=Path(os.environ.get('EEGO_TEST_HOST',str(ROOT/'dist/Eego/core/task_service_host_r3.exe')))
DATA=bytes(range(256))*32768
HASH=hashlib.sha256(DATA).hexdigest()
flaky=True
class Source(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  global flaky
  if '/retry.bin' in self.path and flaky:
   self.send_error(503);return
  chunked='/unknown.bin' in self.path
  start=0;end=len(DATA)-1
  if self.headers.get('Range') and not chunked:
   part=self.headers['Range'].split('=')[1].split('-');start=int(part[0]);end=int(part[1]) if part[1] else end
   self.send_response(206);self.send_header('Content-Range',f'bytes {start}-{end}/{len(DATA)}')
  else:self.send_response(200)
  self.send_header('ETag','"eego-test-v1"')
  if chunked:self.send_header('Transfer-Encoding','chunked')
  else:self.send_header('Content-Length',str(end-start+1));self.send_header('Accept-Ranges','bytes')
  self.end_headers()
  try:
   for i in range(start,end+1,65536):
    b=DATA[i:min(i+65536,end+1)]
    self.wfile.write((f'{len(b):x}\r\n'.encode()+b+b'\r\n') if chunked else b);self.wfile.flush();time.sleep(.035)
   if chunked:self.wfile.write(b'0\r\n\r\n')
  except OSError:pass
 def log_message(self,*a):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Source)
threading.Thread(target=server.serve_forever,daemon=True).start()
url=f'http://127.0.0.1:{server.server_port}'
class Client:
 def __init__(self):
  self.log=(RUN/'host-stderr.log').open('a',encoding='utf8')
  self.p=subprocess.Popen([EXE,'--data-dir',RUN/'library'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.log,text=True,encoding='utf8')
  self.responses=queue.Queue()
  threading.Thread(target=lambda:[self.responses.put(json.loads(l)) for l in self.p.stdout],daemon=True).start()
  self.serial=0
 def call(self,op,**args):
  self.serial+=1
  self.p.stdin.write(json.dumps(dict(op=op,requestId=str(self.serial),**args))+'\n');self.p.stdin.flush()
  r=self.responses.get(timeout=30)
  assert r['ok'],r
  return r
 def tasks(self):return self.call('list')['tasks']
 def until(self,id,state=None,predicate=None,timeout=50):
  deadline=time.monotonic()+timeout
  while time.monotonic()<deadline:
   t=next(t for t in self.tasks() if t['id']==id)
   if (predicate(t) if predicate else t['status']==state):return t
   time.sleep(.12)
  raise TimeoutError((id,state,t))
 def create(self,name):return self.call('create',url=url+'/'+name+' | '+HASH)['id']
 def close(self):
  self.call('shutdown');assert self.p.wait(30)==0;self.log.close()
c=None;result={'run':str(RUN),'exe':str(EXE),'core_sha256':hashlib.sha256(EXE.read_bytes()).hexdigest(),'checks':[],'files':[]}
def record(t):
 p=Path(t['path']);assert p.exists(),t
 actual=hashlib.sha256(p.read_bytes()).hexdigest();assert actual==HASH,(actual,HASH)
 result['files'].append(dict(name=t['name'],bytes=p.stat().st_size,sha256=actual,status=t['status']))
try:
 c=Client();assert c.tasks()==[]
 ident=c.create('long_filename_abcdefghijklmnopqrstuvwxyz_0123456789.bin')
 c.until(ident,predicate=lambda t:t['bytes']>500000 and t['status']=='downloading')
 c.call('pause',id=ident);paused=c.until(ident,'paused');assert paused['capabilities']['resume']
 result['paused_bytes']=paused['bytes'];assert 0<paused['bytes']<len(DATA)
 c.call('resume',id=ident);record(c.until(ident,'completed'))
 result['checks']+=['known-length HTTP + hash','pause confirmed + resume']
 ident=c.create('retry.bin');failed=c.until(ident,'failed');assert failed['error'] and failed['capabilities']['retry']
 result['initial_error']=failed['error'];flaky=False
 c.call('retry',id=ident);record(c.until(ident,'completed'));result['checks'].append('503 failure + successful retry')
 ident=c.create('unknown.bin');unknown=c.until(ident,predicate=lambda t:t['bytes']>0 and t['status']=='downloading')
 assert unknown['total'] is None
 result['unknown_observed']=unknown;record(c.until(ident,'completed'));result['checks'].append('unknown-length chunked HTTP + hash')
 ident=c.create('close_active.bin');c.until(ident,predicate=lambda t:t['bytes']>100000)
 c.close();c=Client();paused=c.until(ident,'paused');result['restart_status']=paused
 c.call('resume',id=ident);record(c.until(ident,'completed'));c.close();c=None
 result['checks'].append('shutdown active + restart paused + resume hash')
 result['passed']=True
finally:
 if c is not None:
  try:c.close()
  except Exception:
   c.p.kill();c.p.wait()
 server.shutdown()
 (RUN/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
 print(json.dumps(dict(run=str(RUN),passed=result.get('passed',False),checks=result['checks'])))

