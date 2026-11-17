"""Kill real downloader processes; fixtures are generated and confined to a new directory."""
import argparse,datetime,hashlib,http.server,json,pathlib,random,socket,subprocess,sys,threading,time,uuid
sys.stdout.reconfigure(encoding='utf-8')
sys.stderr.reconfigure(encoding='utf-8')
P=argparse.ArgumentParser();ROOT=pathlib.Path(__file__).resolve().parents[1]
P.add_argument('--exe',type=pathlib.Path,default=ROOT/'build/core_cli.exe')
P.add_argument('--output-root',type=pathlib.Path,default=ROOT/'artifacts/test-runs')
P.add_argument('--baseline',action='store_true');args=P.parse_args()
EXE=args.exe.resolve();RUN=args.output_root.resolve()/('crash-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:6]);RUN.mkdir(parents=True)
DATA=random.Random(2026102602).randbytes(8*1024*1024+37);CHUNK=262144
digest=lambda b:hashlib.sha256(b).hexdigest()
HASH=digest(DATA);records=[];requests=[];changed=False
def persist(ok=False):
    (RUN/'results.json').write_text(json.dumps(dict(all_passed=ok,baseline=args.baseline,sha256=HASH,tests=records,executable=str(EXE)),indent=2))
def call(name,cmd,code=0):
    p=subprocess.run([str(EXE),*map(str,cmd)],capture_output=True,text=True,encoding="utf-8",timeout=150,creationflags=subprocess.CREATE_NO_WINDOW)
    r=dict(name=name,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr);records.append(r);persist()
    if code is not None:assert p.returncode==code,(name,p.stdout,p.stderr)
    if p.stdout.strip().startswith('{'):r['metrics']=json.loads(p.stdout.strip().splitlines()[-1])
    return r
def verified(path):
    h=digest(path.read_bytes());assert h==HASH,(path,h);records[-1].setdefault('verified_files',[]).append(dict(path=str(path),sha256=h))
def wait_for(predicate,process,timeout=30):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        if process.poll() is not None:raise AssertionError('Downloader exited before kill point')
        try:
            result=predicate()
            if result:return result
        except (FileNotFoundError,PermissionError):pass
        time.sleep(.01)
    raise AssertionError('Kill-point timeout')
def valid_chunks(path):
    b=path.read_bytes()
    return [i for i in range(len(DATA)//CHUNK) if b[i*CHUNK:(i+1)*CHUNK]==DATA[i*CHUNK:(i+1)*CHUNK]]
def kill_transfer(name,cmd,path,minimum=3):
    log=open(RUN/(name+'.log'),'w')
    p=subprocess.Popen([str(EXE),*map(str,cmd)],stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        chunks=wait_for(lambda: (c if len(c:=valid_chunks(path))>=minimum else None),p)
        if name in ['http-hard-kill','bt-hard-kill'] and not args.baseline:
            duplicate=call(name+'-duplicate-rejected',cmd,1)
            assert 'task already in use' in duplicate['stderr']
            assert p.poll() is None
        p.kill();p.wait(timeout=10)
        assert p.returncode!=0
        records.append(dict(name=name,killed=True,returncode=p.returncode,valid_chunks_before_kill=chunks));persist()
        return chunks
    finally:
        if p.poll() is None:p.kill();p.wait(timeout=10)
        log.close()
class Server(http.server.BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def log_message(self,*a):pass
    def do_GET(self):
        first,last=map(int,self.headers['Range'].removeprefix('bytes=').split('-'))
        requests.append((self.path,first,last))
        self.send_response(206);self.send_header('ETag','"changed"' if changed else '"crash-fixture"');self.send_header('Content-Length',str(last-first+1));self.send_header('Content-Range',f'bytes {first}-{last}/{len(DATA)}');self.send_header('Connection','close');self.end_headers()
        try:
            for at in range(first,last+1,16384):self.wfile.write(DATA[at:min(at+16384,last+1)]);self.wfile.flush();time.sleep(.004)
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass
        self.close_connection=True
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server);threading.Thread(target=server.serve_forever,daemon=True).start()
base=f'http://127.0.0.1:{server.server_port}'
seed=None
try:
    out=RUN/'http.bin';part=pathlib.Path(str(out)+'.part');cp=pathlib.Path(str(out)+'.resume')
    kill_transfer('http-hard-kill',['http',base+'/file',out,2],part)
    if args.baseline:
        r=call('http-restart-observation',['http',base+'/file',out,2],None)
        r['checkpoint_exists']=cp.exists();r['completed_hash_correct']=out.exists() and digest(out.read_bytes())==HASH
    else:
        assert cp.exists();before=cp.read_bytes()
        # A stale/unfinished temp checkpoint must not supersede the committed record.
        pathlib.Path(str(cp)+'.tmp-stale').write_bytes(b'incomplete checkpoint')
        # Damage a completed chunk. Resume must re-hash and fetch it again.
        with part.open('r+b') as f:f.write(bytes([DATA[0]^255]))
        r=call('http-restart-rechecks',['http',base+'/file',out,2]);verified(out)
        assert 0<r['metrics']['written_bytes']<len(DATA)
        # A second hard kill, while restoring a previous checkpoint.
        out2=RUN/'twice.bin';part2=pathlib.Path(str(out2)+'.part')
        kill_transfer('http-kill-first',['http',base+'/twice',out2,2],part2)
        kill_transfer('http-kill-again',['http',base+'/twice',out2,2],part2,6)
        call('http-restart-after-two-kills',['http',base+'/twice',out2,2]);verified(out2)
        # Kill with a single worker after the first chunk, before the two-chunk
        # checkpoint boundary. Unrecorded bytes must be safely downloaded again.
        early=RUN/'early.bin';earlypart=pathlib.Path(str(early)+'.part')
        kill_transfer('http-kill-before-batch',['http',base+'/early',early,1],earlypart,1)
        saved=pathlib.Path(str(early)+'.resume').read_bytes().splitlines()
        saved_chunks=sum(len(line)==64 and all(c in b'0123456789abcdef' for c in line) for line in saved)
        records[-1]['checkpoint_completed_chunks']=saved_chunks
        assert saved_chunks==0, 'kill must precede the first batched checkpoint'
        call('http-restart-before-batch',['http',base+'/early',early,1]);verified(early)
        for mode in ['corrupt','identity','wrong-url','missing-record','wrong-size']:
            target=RUN/(mode+'.bin');partial=pathlib.Path(str(target)+'.part');record=pathlib.Path(str(target)+'.resume')
            call('http-prepare-'+mode,['http',base+'/'+mode,target,2,3],3)
            if mode=='corrupt':record.write_bytes(record.read_bytes()[:30])
            if mode=='identity':changed=True
            if mode=='missing-record':record.unlink()
            if mode=='wrong-size':partial.write_bytes(b'unrelated short file')
            old=partial.read_bytes();oldcp=record.read_bytes() if record.exists() else None
            call('http-reject-'+mode,['http',base+('/other-url' if mode=='wrong-url' else '/'+mode),target,2],1)
            assert partial.read_bytes()==old and not target.exists()
            assert (record.read_bytes() if record.exists() else None)==oldcp
            changed=False
    source=RUN/'seed';source.mkdir();(source/'测试 数据.bin').write_bytes(DATA);torrent=RUN/'测试 种子.torrent'
    call('make-torrent',['make-torrent',source/'测试 数据.bin',torrent])
    with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    seedlog=open(RUN/'seed.log','w');seed=subprocess.Popen([str(EXE),'bt-seed',str(torrent),str(source),str(port)],stdout=seedlog,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
    wait_for(lambda:'SEED_READY' in (RUN/'seed.log').read_text(encoding='utf-8'),seed)
    folder=RUN/'bt-killed';folder.mkdir();file=folder/'测试 数据.bin'
    chunks=kill_transfer('bt-hard-kill',['bt-get',torrent,folder,port,1024*1024*1024,2],file)
    # Abrupt kill need not have produced a native resume payload. Full BT hash recheck is authoritative.
    with file.open('r+b') as f:f.seek(chunks[0]*CHUNK);f.write(bytes([DATA[chunks[0]*CHUNK]^255]))
    r=call('bt-restart-rechecks',['bt-get',torrent,folder,port,0,0]);verified(file)
    if not args.baseline:assert 0<r['metrics']['writes']<513
    unrelated=RUN/'bt-unrelated';unrelated.mkdir();uf=unrelated/'测试 数据.bin';uf.write_bytes(b'Q'*len(DATA));old=digest(uf.read_bytes())
    r=call('bt-unrelated-same-size',['bt-get',torrent,unrelated,port],None if args.baseline else 1)
    r['unrelated_preserved']=digest(uf.read_bytes())==old
    if not args.baseline:
        assert r['unrelated_preserved']
        folder=RUN/'bt-record';folder.mkdir();file=folder/'测试 数据.bin'
        call('bt-prepare-record',['bt-get',torrent,folder,port,524288,1],3)
        resume=folder/'task.resume';good=resume.read_bytes();previous=digest(file.read_bytes())
        resume.write_bytes(good[:20]);call('bt-reject-corrupt-record',['bt-get',torrent,folder,port],1);assert digest(file.read_bytes())==previous
        resume.write_bytes(good)
        owner=folder/'.hbt-owner';good_owner=owner.read_bytes()
        owner.write_bytes(good_owner[:-10]);call('bt-reject-corrupt-ownership',['bt-get',torrent,folder,port],1);assert digest(file.read_bytes())==previous;owner.write_bytes(good_owner)
        payload=good[len(b'HBT-CHECKPOINT-1\n'):-73];offset=payload.index(b'9:info-hash20:')+len(b'9:info-hash20:')
        wrong=payload[:offset]+b'R'*20+payload[offset+20:]
        resume.write_bytes(b'HBT-CHECKPOINT-1\n'+wrong+b'\nSHA256 '+digest(wrong).encode()+b'\n')
        call('bt-reject-mismatched-resume',['bt-get',torrent,folder,port],1);assert digest(file.read_bytes())==previous;resume.write_bytes(good)
        other=RUN/'other';other.mkdir();(other/'测试 数据.bin').write_bytes(b'X'*len(DATA));ot=RUN/'other.torrent';call('make-other-torrent',['make-torrent',other/'测试 数据.bin',ot])
        call('bt-reject-wrong-torrent',['bt-get',ot,folder,port],1);assert digest(file.read_bytes())==previous
        call('bt-original-record-still-works',['bt-get',torrent,folder,port]);verified(file)
    persist(not args.baseline);(RUN/'requests.json').write_text(json.dumps(requests))
    print(('BASELINE OBSERVED: ' if args.baseline else 'CRASH RECOVERY PASS: ')+str(RUN),flush=True)
finally:
    if seed:seed.terminate();seed.wait(timeout=10);seedlog.close()
    server.shutdown()
