"""Controlled real multi-file swarm for manual GUI pause/reopen acceptance."""
import argparse,datetime,hashlib,json,pathlib,random,socket,subprocess,sys,time,uuid
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.stdout.reconfigure(encoding='utf-8')
p=argparse.ArgumentParser();p.add_argument('--bin',type=pathlib.Path,default=None);p.add_argument('--interactive',action='store_true');a=p.parse_args()
run=ROOT/'artifacts/test-runs'/('Magnet 界面 '+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+' '+uuid.uuid4().hex[:6]);source=run/'seed'/'中文测试资源';source.mkdir(parents=True)
expected={}
for name,size in [('第一份.bin',4*1024*1024+17),('子目录/第二份.bin',2*1024*1024+29),('empty.txt',0)]:
 data=random.Random(size).randbytes(size);path=source/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data);expected['中文测试资源/'+name]=hashlib.sha256(data).hexdigest()
cli=(a.bin or ROOT/'build').resolve()/'core_cli.exe';gui=(a.bin or ROOT/'dist/HandmadeBT').resolve()/'handmadebt.exe';torrent=run/'test.torrent'
subprocess.run([str(cli),'make-torrent-dir',str(source),str(torrent)],check=True,creationflags=subprocess.CREATE_NO_WINDOW)
with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
uri=subprocess.check_output([str(cli),'magnet-uri',str(torrent),str(port)],text=True,encoding='utf-8',creationflags=subprocess.CREATE_NO_WINDOW).strip()
(run/'magnet.txt').write_text(uri,encoding='utf-8');(run/'fixture.json').write_text(json.dumps(dict(root=str(run),expected=expected,magnet=uri,exe_sha256=hashlib.sha256(gui.read_bytes()).hexdigest()),indent=2),encoding='utf-8')
def seed(rate):
 return subprocess.Popen([str(cli),'bt-seed',str(torrent),str(source.parent),str(port),str(rate),'1800'],stdout=(run/'seed.log').open('w'),stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
seeder=seed(65536)
def launch():return subprocess.Popen([str(gui),'--data-dir',str(run/'用户任务')],creationflags=subprocess.CREATE_NO_WINDOW)
g=launch();print('READY: '+str(run),flush=True)
print('粘贴 magnet.txt 添加任务；点击暂停、关闭、r 重开后恢复；f 加速/s 慢速；v 校验；q 结束。',flush=True)
try:
 while True:
  if not a.interactive:time.sleep(1);continue
  command=input('> ').strip().lower()
  if command=='q':break
  if command=='r':g=launch()
  if command in ['f','s']:seeder.terminate();seeder.wait();seeder=seed(4*1024*1024 if command=='f' else 65536)
  if command=='v':
   folders=list((run/'用户任务'/'Downloads').glob('*-magnet'))
   assert folders
   result={}
   for folder in folders:
    assert (folder/'files.record').exists(),'Not completed'
    for relative,h in expected.items():
     actual=hashlib.sha256((folder/'files'/relative).read_bytes()).hexdigest();assert actual==h;result[str(folder/'files'/relative)]=actual
   (run/'verified.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print('HASH PASS: '+str(run/'verified.json'),flush=True)
except KeyboardInterrupt:pass
finally:
 seeder.terminate();seeder.wait()
 if g.poll() is None:g.terminate();g.wait()
