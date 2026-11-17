"""Serve an owned Magnet link for real browser context-menu acceptance."""
import functools,hashlib,html,http.server,json,pathlib,random,socket,subprocess,sys,threading,time,urllib.parse,uuid
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.stdout.reconfigure(encoding='utf-8')
run=ROOT/'artifacts/test-runs'/('browser-magnet-click-'+uuid.uuid4().hex[:8]);source=run/'seed'/'网页磁力测试';source.mkdir(parents=True)
expected={}
for name,size in [('内容.bin',4*1024*1024+31),('空文件.txt',0)]:
 data=random.Random(size).randbytes(size);(source/name).write_bytes(data);expected['网页磁力测试/'+name]=hashlib.sha256(data).hexdigest()
cli=ROOT/'dist/HandmadeBT-0.6-magnet-browser-win64/bin/core_cli.exe';torrent=run/'test.torrent'
subprocess.run([str(cli),'make-torrent-dir',str(source),str(torrent)],check=True,creationflags=subprocess.CREATE_NO_WINDOW)
with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
uri=subprocess.check_output([str(cli),'magnet-uri',str(torrent),str(port)],text=True,encoding='utf-8',creationflags=subprocess.CREATE_NO_WINDOW).strip()
(run/'index.html').write_text('<!doctype html><meta charset="utf-8"><title>HandmadeBT 磁力交接验收</title><h1>HandmadeBT 磁力交接验收</h1><p>自产文件，本机受控做种。</p><p><a href="'+html.escape(uri,quote=True)+'">磁力测试链接（右键交给 HandmadeBT）</a></p><p><a href="handmadebt:'+urllib.parse.quote(uri,safe='')+'">通过已注册应用协议打开同一磁力任务</a></p>',encoding='utf-8')
class Handler(http.server.SimpleHTTPRequestHandler):
 def log_message(self,*a):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(run)))
threading.Thread(target=server.serve_forever,daemon=True).start()
seed=subprocess.Popen([str(cli),'bt-seed',str(torrent),str(source.parent),str(port),'65536','1800'],stdout=(run/'seed.log').open('w'),stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
info=dict(root=str(run),url=f'http://127.0.0.1:{server.server_port}/',magnet=uri,expected=expected)
(run/'fixture.json').write_text(json.dumps(info,indent=2),encoding='utf-8');print(json.dumps(info,ensure_ascii=False),flush=True)
try:
 while True:
  command=input('f=fast q=quit > ').strip()
  if command=='q':break
  if command=='f':
   seed.terminate();seed.wait();seed=subprocess.Popen([str(cli),'bt-seed',str(torrent),str(source.parent),str(port),'4194304','1800'],stdout=(run/'seed-fast.log').open('w'),stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
finally:
 seed.terminate();seed.wait();server.shutdown()
