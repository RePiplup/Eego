"""Real Windows HTTP/BT transfer verification. Never deletes previous runs."""
import argparse, datetime, hashlib, http.server, json, pathlib, random, socket, ssl
import subprocess, sys, threading, time, uuid
sys.stdout.reconfigure(encoding='utf-8')
sys.stderr.reconfigure(encoding='utf-8')
ROOT=pathlib.Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--exe',type=pathlib.Path,default=ROOT/'build/core_cli.exe')
parser.add_argument('--output-root',type=pathlib.Path,default=ROOT/'artifacts/test-runs')
parser.add_argument('--online',action='store_true')
args=parser.parse_args()
RUN=args.output_root.resolve()/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:6])
RUN.mkdir(parents=True)
EXE=args.exe.resolve()
DATA=random.Random(20261026).randbytes(4*1024*1024+37)
HASH=hashlib.sha256(DATA).hexdigest()
records=[]; requests=[]; changed=False
def call(name,args,code=0):
    start=time.perf_counter()
    p=subprocess.run([str(EXE),*map(str,args)],capture_output=True,text=True,encoding="utf-8",timeout=150,creationflags=subprocess.CREATE_NO_WINDOW)
    record=dict(name=name,command=list(map(str,args)),returncode=p.returncode,seconds=round(time.perf_counter()-start,3),stdout=p.stdout,stderr=p.stderr)
    records.append(record)
    (RUN/'results.json').write_text(json.dumps(dict(expected_sha256=HASH,tests=records),indent=2))
    if p.returncode!=code:raise AssertionError(f'{name}: expected {code}, got {p.returncode}: {p.stdout} {p.stderr}')
    if p.stdout.strip().startswith('{'):record['metrics']=json.loads(p.stdout.strip().splitlines()[-1])
    print(name+': PASS',flush=True)
    return record
def verify(p,expected=HASH):
    actual=hashlib.sha256(p.read_bytes()).hexdigest()
    assert actual==expected,(p,actual,expected)
    records[-1]['sha256']=actual
    records[-1].setdefault('verified_files',[]).append(dict(path=str(p),sha256=actual))
class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def log_message(self,*args):pass
    def do_GET(self):
        global changed
        req=self.headers.get('Range');start,end=0,len(DATA)-1
        ranged=req is not None and self.path!='/no-range'
        if ranged:start,end=map(int,req.removeprefix('bytes=').split('-'))
        requests.append(dict(path=self.path,range=req))
        self.send_response(206 if ranged else 200)
        self.send_header('ETag','"changed"' if changed and self.path=='/identity' else '"fixture-v1"')
        self.send_header('Content-Length',str(end-start+1))
        if ranged:self.send_header('Content-Range',f'bytes {start+1 if self.path=="/bad-range" and end>0 else start}-{end}/{len(DATA)}')
        self.send_header('Connection','close');self.end_headers()
        body=DATA[start:end+1]
        if self.path=='/short' and end>0:body=body[:len(body)//2]
        try:self.wfile.write(body)
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass
        self.close_connection=True
def certificate():
    # Deliberately public test key; never install this certificate as trusted.
    for name in ['self-signed.pem','test-key.pem']:
        (RUN/name).write_bytes((ROOT/'tests/fixtures'/name).read_bytes())
def free_port():
    with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
base=f'http://127.0.0.1:{server.server_port}'
seed=None;tls=None
try:
    for name,route,n in [('http-single','/no-range',1),('http-segmented','/range',4)]:
        target=RUN/('下载 '+name+'.bin');call(name,['http',base+route,target,n]);verify(target)
    target=RUN/'http-resumed.bin'
    call('http-pause',['http',base+'/range',target,4,3],3)
    assert not target.exists() and pathlib.Path(str(target)+'.resume').exists()
    call('http-resume',['http',base+'/range',target,4]);verify(target)
    for name,route in [('http-range-unsupported','/no-range'),('http-bad-range','/bad-range'),('http-short','/short')]:
        target=RUN/('下载 '+name+'.bin');call(name,['http',base+route,target,4],1);assert not target.exists()
    target=RUN/'identity.bin'
    call('http-identity-pause',['http',base+'/identity',target,2,2],3);changed=True
    call('http-identity-change-rejected',['http',base+'/identity',target,2],1);assert not target.exists()
    certificate();tls=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(RUN/'self-signed.pem',RUN/'test-key.pem');tls.socket=context.wrap_socket(tls.socket,server_side=True)
    threading.Thread(target=tls.serve_forever,daemon=True).start()
    call('https-untrusted-rejected',['http',f'https://localhost:{tls.server_port}/range',RUN/'untrusted.bin',1],1)
    if args.online:
        fixture=json.loads((ROOT/'tests/fixtures/https-source.json').read_text())
        target=RUN/'https-COPYING.txt';call('https-trusted',['http',fixture['url'],target,1]);verify(target,fixture['sha256'])
    source=RUN/'seed';source.mkdir();(source/'测试 数据.bin').write_bytes(DATA)
    torrent=RUN/'测试 种子.torrent';call('make-torrent',['make-torrent',source/'测试 数据.bin',torrent])
    port=free_port();seedlog=open(RUN/'seed.log','w')
    seed=subprocess.Popen([str(EXE),'bt-seed',str(torrent),str(source),str(port)],stdout=seedlog,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
    for _ in range(1000):
        if 'SEED_READY' in (RUN/'seed.log').read_text(encoding='utf-8'):break
        if seed.poll() is not None:raise RuntimeError((RUN/'seed.log').read_text(encoding='utf-8'))
        time.sleep(.02)
    else:raise RuntimeError('seed startup timeout')
    for name,mode,delay in [('bt-upstream-baseline','bt-baseline-get',0),('bt-filestore','bt-get',0),('bt-backpressure','bt-get',3)]:
        folder=RUN/name;folder.mkdir();rec=call(name,[mode,torrent,folder,port,0,delay]);verify(folder/'测试 数据.bin')
        if mode=='bt-get':
            m=rec['metrics'];assert m['writes']>0 and m['writes']==m['write_callbacks'] and m['wrong_callback_thread']==0
            if delay:assert m['backpressure']>0 and m['wakeups']>0
    folder=RUN/'bt-resumed';folder.mkdir()
    call('bt-pause',['bt-get',torrent,folder,port,512*1024,1],3)
    assert (folder/'task.resume').exists()
    # Damage a previously downloaded byte: resume must recheck, repair it,
    # and still reuse the other valid pieces instead of trusting saved progress.
    with (folder/'测试 数据.bin').open('r+b') as f:
        byte=f.read(1);f.seek(0);f.write(bytes([byte[0]^255]))
    rec=call('bt-resume-with-recheck',['bt-get',torrent,folder,port,0,1]);verify(folder/'测试 数据.bin')
    assert rec['metrics']['writes']<257,'resume should reuse verified data'
    folder=RUN/'mixed-bt';folder.mkdir();target=RUN/'mixed-http.bin'
    rec=call('mixed-shared-filestore',['mixed',base+'/range',target,torrent,folder,port])
    verify(target);verify(folder/'测试 数据.bin')
    assert rec['metrics']['shared_store_bytes']==2*len(DATA)
    folder=RUN/'bt-wrong-size';folder.mkdir();(folder/'测试 数据.bin').write_bytes(b'preserve-existing')
    call('bt-wrong-size-rejected',['bt-get',torrent,folder,port],1)
    assert (folder/'测试 数据.bin').read_bytes()==b'preserve-existing'
    # Existing-file ownership: neither protocol may overwrite a completed HTTP output.
    target=RUN/'下载 http-single.bin';call('existing-output-rejected',['http',base+'/range',target,1],1);verify(target)
    (RUN/'requests.json').write_text(json.dumps(requests,indent=2))
    (RUN/'results.json').write_text(json.dumps(dict(expected_sha256=HASH,tests=records,all_passed=True,online=args.online,executable=str(EXE),python=sys.executable),indent=2))
    print('ALL PASS: '+str(RUN),flush=True)
finally:
    if seed:seed.terminate();seed.wait(timeout=10);seedlog.close()
    server.shutdown()
    if tls:tls.shutdown()
