"""Real selective BT transfer, boundary pieces, restart and protection regression."""
import argparse,hashlib,json,pathlib,random,socket,subprocess,time,uuid
ROOT=pathlib.Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser();ap.add_argument('--exe',type=pathlib.Path,default=ROOT/'build/core_cli.exe');ap.add_argument('--output-root',type=pathlib.Path,default=ROOT/'artifacts/test-runs');args=ap.parse_args()
EXE=args.exe.resolve()
RUN=args.output_root.resolve()/('selection-'+uuid.uuid4().hex[:8]);RUN.mkdir(parents=True)
records=[]
def call(name,args,code=0):
    r=subprocess.run([str(EXE),*map(str,args)],capture_output=True,text=True,encoding='utf-8',timeout=120)
    records.append(dict(name=name,code=r.returncode,stdout=r.stdout,stderr=r.stderr))
    (RUN/'results.json').write_text(json.dumps(dict(passed=False,tests=records),indent=2))
    assert r.returncode==code,(name,r.stdout,r.stderr)
    print(name+': PASS',flush=True);return r.stdout
source=RUN/'seed'/'bundle';source.mkdir(parents=True)
expected={}
for name,size in [('a.bin',200003),('b.bin',2097183),('c.bin',1048591),('empty.txt',0)]:
    data=random.Random(size).randbytes(size);(source/name).write_bytes(data);expected[name]=hashlib.sha256(data).hexdigest()
torrent=RUN/'test.torrent';call('create',['make-torrent-dir',source,torrent])
with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
uri=call('uri',['magnet-uri',torrent,port]).strip()
seed=subprocess.Popen([str(EXE),'bt-seed',str(torrent),str(source.parent),str(port),'262144'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
try:
    time.sleep(1);download=RUN/'download'
    call('metadata-only',['magnet-inspect',uri,download],4)
    assert not (download/'files').exists()
    listing=call('list-files',['magnet-files',download]);mapping={}
    for line in listing.splitlines():
        index,size,name=line.split(' ',2);mapping[pathlib.Path(json.loads(name)).name]=int(index)
    call('empty-selection-rejected',['magnet-select',download,''],1)
    call('bad-index-rejected',['magnet-select',download,'999'],1)
    selection=f"{mapping['b.bin']},{mapping['empty.txt']}"
    call('select-middle-and-empty',['magnet-select',download,selection])
    call('pause',['magnet-get',uri,download,'2500'],3)
    call('resume',['magnet-get',uri,download])
    def verify():
        files=download/'files'/'bundle'
        assert {p.name for p in files.iterdir()}=={'b.bin','empty.txt'}
        for name in ('b.bin','empty.txt'):assert hashlib.sha256((files/name).read_bytes()).hexdigest()==expected[name]
        assert list((download/'boundary-cache').glob('*.part'))
    verify()
    call('started-selection-immutable',['magnet-select',download,str(mapping['a.bin'])],1)
    with (download/'files/bundle/b.bin').open('r+b') as f:f.write(b'CORRUPT')
    # Corrupt cross-file support bytes too: native piece recheck must repair them.
    for cache in (download/'boundary-cache').glob('*.part'):
        with cache.open('r+b') as f:f.seek(200003 if cache.stat().st_size>200003 else 0);f.write(b'BAD')
    call('repair-selected-and-boundary',['magnet-get',uri,download]);verify()
    saved=(download/'selection.record').read_bytes();(download/'selection.record').write_bytes(saved+b'x')
    call('tampered-selection-rejected',['magnet-get',uri,download],1)
    (download/'selection.record').write_bytes(saved)
    empty=RUN/'empty';call('empty-metadata',['magnet-inspect',uri,empty],4)
    call('select-empty-file',['magnet-select',empty,str(mapping['empty.txt'])])
    call('empty-file-completion',['magnet-get',uri,empty])
    assert (empty/'files/bundle/empty.txt').read_bytes()==b''
    (RUN/'results.json').write_text(json.dumps(dict(passed=True,tests=records,expected=expected),indent=2))
    print('ALL PASS: '+str(RUN),flush=True)
finally:
    seed.terminate();seed.wait(timeout=10)
