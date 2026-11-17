"""Isolated real HTTP/HTTPS fixture for independent GUI acceptance.
No UI automation, no external account, never installs its untrusted TLS cert.
The only writable files are inside the freshly generated test directory.
"""
import argparse,datetime,hashlib,http.server,json,pathlib,random,ssl,subprocess,sys,threading,time,uuid
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.stdout.reconfigure(encoding='utf-8')
p=argparse.ArgumentParser()
p.add_argument('--exe',type=pathlib.Path,default=ROOT/'dist/HandmadeBT/HandmadeBT.exe')
p.add_argument('--output-root',type=pathlib.Path,default=ROOT/'artifacts/test-runs')
p.add_argument('--launch',action='store_true')
p.add_argument('--interactive',action='store_true')
args=p.parse_args()
run=args.output_root.resolve()/('界面 验收 '+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+' '+uuid.uuid4().hex[:6])
run.mkdir(parents=True)
data=random.Random(20260501).randbytes(8*1024*1024+37)
sha=hashlib.sha256(data).hexdigest()
(run/'expected.bin').write_bytes(data)
(run/'speed.json').write_text('{"delay":0.2}')
guard=threading.Lock()
class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def log_message(self,*args):pass
    def do_GET(self):
        if self.path=='/':
            page=('<!doctype html><meta charset="utf-8"><title>HandmadeBT 受控下载验收</title>'
                  '<h1>HandmadeBT 受控下载验收</h1><p>自产 8 MiB 测试文件；用于实际点击、暂停恢复与哈希核验。</p>'
                  '<p><a href="/slow.bin" download>下载慢速测试文件</a></p>'
                  '<p>预期 SHA-256：'+sha+'</p>').encode('utf-8')
            self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Content-Length',str(len(page)));self.end_headers();self.wfile.write(page);return
        if self.path=='/missing.bin':
            self.send_response(404);self.send_header('Content-Length','0');self.end_headers();return
        start,end=0,len(data)-1
        ranged=self.path!='/no-range.bin' and self.headers.get('Range')
        if ranged:
            first,last=self.headers['Range'].removeprefix('bytes=').split('-');start=int(first);end=int(last) if last else len(data)-1
        with guard:
            with (run/'requests.jsonl').open('a',encoding='utf-8') as f:f.write(json.dumps(dict(path=self.path,start=start,end=end,time=time.time()))+'\n')
        self.send_response(206 if ranged else 200)
        self.send_header('Content-Length',str(end-start+1));self.send_header('ETag','"gui-fixture-1"')
        self.send_header('Content-Type','application/octet-stream');self.send_header('Content-Disposition','attachment; filename="handmadebt-test.bin"')
        if ranged:self.send_header('Content-Range',f'bytes {start}-{end}/{len(data)}')
        self.send_header('Connection','close');self.end_headers()
        limit=end+1
        if self.path=='/short.bin' and end>0:limit=start+(end-start)//2
        try:
            for at in range(start,limit,8192):
                self.wfile.write(data[at:min(at+8192,limit)]);self.wfile.flush()
                if self.path in ['/slow.bin','/no-range.bin']:
                    try:delay=json.loads((run/'speed.json').read_text())['delay']
                    except (ValueError,OSError):delay=.2
                    time.sleep(delay)
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass
        self.close_connection=True
class Server(http.server.ThreadingHTTPServer):request_queue_size=64
http=Server(('127.0.0.1',0),Handler);tls=Server(('127.0.0.1',0),Handler)
ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(ROOT/'tests/fixtures/self-signed.pem',ROOT/'tests/fixtures/test-key.pem')
tls.socket=ctx.wrap_socket(tls.socket,server_side=True)
for server in [http,tls]:threading.Thread(target=server.serve_forever,daemon=True).start()
base=f'http://127.0.0.1:{http.server_port}'
manifest=dict(root=str(run),data_dir=str(run/'用户任务'),sha256=sha,size=len(data),
              http=base,untrusted_https=f'https://localhost:{tls.server_port}/range.bin',
              exe=str(args.exe.resolve()),exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest())
(run/'fixture.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
(run/'links.txt').write_text('\n'.join(base+route+' | '+sha for route in ['/slow.bin','/range.bin','/no-range.bin'])+'\n'+base+'/missing.bin\n'+manifest['untrusted_https']+'\n',encoding='utf-8')
(run/'single-link.txt').write_text(base+'/slow.bin | '+sha,encoding='utf-8')
(run/'batch-links.txt').write_text(base+'/range.bin | '+sha+'\n'+base+'/missing.bin\n'+manifest['untrusted_https']+'\n'+base+'/range.bin | '+'0'*64,encoding='utf-8')
(run/'README-test.txt').write_text('隔离界面验收\n1. 把 single-link.txt 的内容粘贴进窗口，点击添加下载。\n2. 看到实际进度增长后，选中任务并点击暂停。临时文件大小不代表下载进度。\n3. 关闭窗口并重开同一任务目录，确认任务保留；点击恢复。\n4. 在测试服务窗口输入 f 可加速；输入 r 可重开隔离界面；输入 s 恢复慢速。\n5. 粘贴 batch-links.txt，确认多行添加、404/证书/错误哈希均报错。\n6. 完成文件应与 expected.bin 的 SHA-256 相同：'+sha+'\n7. 在服务窗口输入 v 自动核验已完成文件，输入 q 结束服务。\n此目录与正式 userdata 完全隔离。不要关闭证书验证或安装测试证书。\n',encoding='utf-8')
print(json.dumps(manifest,ensure_ascii=False),flush=True)
def launch():subprocess.Popen([str(args.exe.resolve()),'--data-dir',str(run/'用户任务')],creationflags=subprocess.CREATE_NO_WINDOW)
if args.launch:launch()
print('隔离测试目录：'+str(run)+'\n输入 f 加速，s 慢速，r 重开界面，v 核验文件，q 退出测试服务。',flush=True)
try:
    while True:
        if not args.interactive:time.sleep(1);continue
        command=input('> ').strip().lower()
        if command=='q':break
        if command in ['f','s']:(run/'speed.json').write_text(json.dumps(dict(delay=0 if command=='f' else .2)))
        if command=='r':launch()
        if command=='v':subprocess.run([sys.executable,'-I',str(ROOT/'tests/gui_verify.py'),str(run/'fixture.json')])
except KeyboardInterrupt:pass
finally:http.shutdown();tls.shutdown()
