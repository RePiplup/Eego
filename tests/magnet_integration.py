"""Real metadata exchange and multi-file BT through the common FileStore."""
import argparse,datetime,hashlib,json,pathlib,random,socket,subprocess,sys,time,uuid
sys.stdout.reconfigure(encoding='utf-8')
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--exe',type=pathlib.Path,default=ROOT/'build/core_cli.exe');p.add_argument('--output-root',type=pathlib.Path,default=ROOT/'artifacts/test-runs');args=p.parse_args()
exe=args.exe.resolve();run=args.output_root.resolve()/('magnet-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:6]);run.mkdir(parents=True)
records=[];expected={}
def save(ok=False):(run/'results.json').write_text(json.dumps(dict(all_passed=ok,executable=str(exe),expected=expected,tests=records),indent=2),encoding='utf-8')
def call(name,cmd,code=0):
 started=time.perf_counter();r=subprocess.run([str(exe),*map(str,cmd)],capture_output=True,text=True,encoding='utf-8',timeout=220,creationflags=subprocess.CREATE_NO_WINDOW);records.append(dict(name=name,returncode=r.returncode,elapsed_seconds=round(time.perf_counter()-started,4),stdout=r.stdout,stderr=r.stderr));save();assert r.returncode==code,(name,r.stderr);print(name+': PASS',flush=True);return r.stdout.strip()
def verify(root):
 hashes={}
 for relative,h in expected.items():
  path=root/'files'/relative;actual=hashlib.sha256(path.read_bytes()).hexdigest();assert actual==h;hashes[relative]=actual
 records[-1]['verified_files']=hashes;save()
seed=None
try:
 source=run/'seed'/'测试资源';source.mkdir(parents=True)
 for name,size in [('一.bin',6*1024*1024+37),('子目录/二.bin',3*1024*1024+71),('empty.txt',0)]:
  path=source/name;path.parent.mkdir(parents=True,exist_ok=True);data=random.Random(size).randbytes(size);path.write_bytes(data);expected['测试资源/'+name]=hashlib.sha256(data).hexdigest()
 torrent=run/'fixture.torrent';call('make-multifile',['make-torrent-dir',source,torrent])
 with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
 uri=call('magnet-uri',['magnet-uri',torrent,port]);(run/'magnet.txt').write_text(uri,encoding='utf-8')
 log=(run/'seed.log').open('w',encoding='utf-8');seed=subprocess.Popen([str(exe),'bt-seed',str(torrent),str(source.parent),str(port),'1048576'],stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
 end=time.monotonic()+20
 while 'SEED_READY' not in (run/'seed.log').read_text(encoding='utf-8'):
  assert seed.poll() is None and time.monotonic()<end;time.sleep(.05)
 folder=run/'download'
 call('real-magnet-pause',['magnet-get',uri,folder,2500],3)
 assert (folder/'magnet.owner').exists() and not (folder/'files.record').exists()
 call('metadata-and-multifile-resume',['magnet-get',uri,folder]);verify(folder)
 # Completed data modified on disk: native picker/checker must repair it.
 target=folder/'files/测试资源/一.bin'
 with target.open('r+b') as f:f.write(b'corrupt')
 call('repair-damaged-piece',['magnet-get',uri,folder]);verify(folder)
 unrelated=run/'unowned';unrelated.mkdir();(unrelated/'keep.txt').write_text('preserve')
 call('unowned-directory-protected',['magnet-get',uri,unrelated],1);assert (unrelated/'keep.txt').read_text()=='preserve'
 call('invalid-magnet',['magnet-get','magnet:?xt=urn:btih:bad',run/'invalid'],1)
 # Real kill after payload allocation; metadata and pieces survive without a saved bitmap.
 killed=run/'killed';proc=subprocess.Popen([str(exe),'magnet-get',uri,str(killed)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=subprocess.CREATE_NO_WINDOW)
 try:
  end=time.monotonic()+20
  while not (killed/'files/测试资源/一.bin').exists():
   assert proc.poll() is None and time.monotonic()<end;time.sleep(.05)
  time.sleep(2);assert proc.poll() is None;proc.kill();proc.wait();records.append(dict(name='hard-kill',killed=True));save()
 finally:
  if proc.poll() is None:proc.kill();proc.wait()
 call('hard-kill-recovery',['magnet-get',uri,killed]);verify(killed)
 save(True);print('MAGNET PASS: '+str(run/'results.json'),flush=True)
finally:
 if seed:seed.terminate();seed.wait(timeout=10);log.close()
