import pathlib,subprocess,json,time,socket,hashlib,threading,http.server,struct,os
R=pathlib.Path(__file__).resolve().parents[2];E=R/'artifacts/test-runs/eego-next'/('run-'+str(time.time_ns()));E.mkdir(parents=True,exist_ok=True)
P=R/'dist/Eego';cli=R/'dist/Eego/core/core_cli.exe';src=E/'seed/bundle';src.mkdir(parents=True,exist_ok=True)
for n in ['a.bin','b.bin']:(src/n).write_bytes(bytes(range(256))*2048)
torrent=E/'sample.torrent';subprocess.run([cli,'make-torrent-dir',src,torrent],check=True,capture_output=True)
with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
class Tracker(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  body=b'd8:intervali1e5:peers6:'+socket.inet_aton('127.0.0.1')+struct.pack('>H',port)+b'e'
  self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
 def log_message(self,*a):pass
tracker=http.server.ThreadingHTTPServer(('127.0.0.1',0),Tracker);threading.Thread(target=tracker.serve_forever,daemon=True).start()
announce=f'http://127.0.0.1:{tracker.server_port}/announce'.encode();raw=torrent.read_bytes();torrent.write_bytes(b'd8:announce'+str(len(announce)).encode()+b':'+announce+raw[1:])
uri=subprocess.check_output([cli,'magnet-uri',torrent,str(port)],text=True).strip()
(E/'magnet.txt').write_text(uri)
seed=subprocess.Popen([cli,'bt-seed',torrent,src.parent,str(port),'1048576','600'],stdout=(E/'seed.log').open('w'),stderr=subprocess.STDOUT)
library=E/'library';library.mkdir();(library/'dht-bootstrap.txt').write_text('127.0.0.1:1')
host=subprocess.Popen([os.environ.get('EEGO_TEST_HOST',P/'core/task_service_host_r3.exe'),'--data-dir',library],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=(E/'host.log').open('w'),text=True,encoding='utf8')
records=[]
def call(op,**kw):
 host.stdin.write(json.dumps(dict(op=op,**kw))+'\n');host.stdin.flush();r=json.loads(host.stdout.readline());records.append(r);return r
def wait(id,status):
 end=time.time()+90
 while time.time()<end:
  t=next(t for t in call('list')['tasks'] if t['id']==id)
  if t['status']==status:return t
  if t['status']=='failed':raise AssertionError(t)
  time.sleep(.3)
 raise AssertionError(t)
try:
 d=E/'chosen-directory';d.mkdir(exist_ok=True)
 (d/'a.bin').write_bytes(b'existing');(d/'bundle').mkdir()
 a=call('create',url=uri,destination=str(d));assert a['ok'];i=a['id'];t=wait(i,'choosing');assert len(t['files'])==2
 assert not call('select',id=i,indexes=[])['ok']
 assert call('select',id=i,indexes=[t['files'][0]['index']])['ok'];done=wait(i,'completed')
 payload=pathlib.Path(done['path']);assert payload==d/'a (2).bin';assert payload.read_bytes()==(src/'a.bin').read_bytes();assert (d/'a.bin').read_bytes()==b'existing'
 a=call('torrent',file=str(torrent),destination=str(d));assert a['ok'];j=a['id'];t=wait(j,'choosing');assert len(t['files'])==2
 assert call('torrent',file=str(torrent),destination=str(d))['id']==j
 assert not (pathlib.Path(t['path'])/'files').exists()
 assert call('select',id=j,indexes=[f['index'] for f in t['files']])['ok'];td=wait(j,'completed');tp=pathlib.Path(td['path']);assert tp==d/'bundle (2)'
 assert all((tp/name).read_bytes()==(src/name).read_bytes() for name in ['a.bin','b.bin'])
 assert all(pathlib.Path(f['diskPath']).is_file() for f in td['files'])
 assert not list(d.glob('*-magnet')) and not list(library.glob('completion-*.record')) and (library/'completion.record').exists()
 bad=E/'bad.torrent';bad.write_text('invalid');assert not call('torrent',file=str(bad),destination=str(d))['ok']
 call('shutdown');host.wait(timeout=15)
 (E/'result.json').write_text(json.dumps({'pass':True,'records':records,'selected_sha256':hashlib.sha256(payload.read_bytes()).hexdigest()},indent=2))
 print('PASS',E)
finally:
 if host.poll() is None:host.kill()
 seed.terminate();seed.wait()
 tracker.shutdown()
