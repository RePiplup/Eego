from pathlib import Path
import subprocess,json,time,http.server,ssl,threading,hashlib
R=Path(__file__).resolve().parents[2];E=R/'artifacts/test-runs/eego-next'/('https-'+str(time.time_ns()));E.mkdir(parents=True,exist_ok=True)
fixture=json.loads((R/'tests/fixtures/https-source.json').read_text())
class H(http.server.BaseHTTPRequestHandler):
 def do_GET(self):self.send_response(200);self.end_headers();self.wfile.write(b'untrusted')
 def log_message(self,*a):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),H);ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);ctx.load_cert_chain(R/'tests/fixtures/self-signed.pem',R/'tests/fixtures/test-key.pem');server.socket=ctx.wrap_socket(server.socket,server_side=True);threading.Thread(target=server.serve_forever,daemon=True).start()
p=subprocess.Popen([R/'dist/Eego/core/task_service_host_r3.exe','--data-dir',E/'library'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=(E/'stderr.txt').open('w'),text=True,encoding='utf8');records=[]
def call(op,**kw):
 p.stdin.write(json.dumps(dict(op=op,**kw))+'\n');p.stdin.flush();v=json.loads(p.stdout.readline());records.append(v);return v
try:
 for url,wanted in [(fixture['url'],'completed'),(f'https://localhost:{server.server_port}/bad.bin','failed')]:
  i=call('create',url=url)['id'];deadline=time.time()+90
  while time.time()<deadline:
   t=next(t for t in call('list')['tasks'] if t['id']==i)
   if t['status'] in ['failed','completed']:break
   time.sleep(.25)
  assert t['status']==wanted,t
  if wanted=='completed':assert hashlib.sha256(Path(t['path']).read_bytes()).hexdigest()==fixture['sha256']
  else:assert not Path(t['path']).exists() and '12175' in t['error']['message'],t
 call('shutdown');p.wait(timeout=10);(E/'result.json').write_text(json.dumps({'passed':True,'records':records},indent=2));print(E)
finally:
 if p.poll() is None:p.kill()
 server.shutdown()
