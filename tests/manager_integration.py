"""Production native controller + real HTTP + Windows Recycle Bin acceptance.
Creates a unique isolated directory. Never uses a user's task list or download.
"""
import argparse,hashlib,json,pathlib,subprocess,sys,uuid,os,random,socket,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--exe',type=pathlib.Path,default=ROOT/'build/manager_test.exe');p.add_argument('--output-root',type=pathlib.Path,default=ROOT/'artifacts/test-runs');args=p.parse_args()
run=args.output_root.resolve()/('manager08-'+uuid.uuid4().hex[:8]);run.mkdir(parents=True)
fixture=subprocess.Popen([sys.executable,str(ROOT/'tests/gui_fixture.py'),'--exe',str(next(p for p in (args.exe.resolve().parent/'handmadebt.exe',ROOT/'dist/HandmadeBT/HandmadeBT.exe') if p.exists())),'--output-root',str(run)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',creationflags=subprocess.CREATE_NO_WINDOW)
seed=None
try:
    core=args.exe.resolve().parent/'core_cli.exe'
    source=run/'seed/bundle';source.mkdir(parents=True)
    expected={}
    for name,size in [('a.bin',200003),('b.bin',2097183),('c.bin',1048591),('empty.txt',0)]:
        content=random.Random(size).randbytes(size);(source/name).write_bytes(content);expected[name]=hashlib.sha256(content).hexdigest()
    torrent=run/'bundle.torrent'
    subprocess.run([str(core),'make-torrent-dir',str(source),str(torrent)],check=True,capture_output=True)
    with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    uri=subprocess.check_output([str(core),'magnet-uri',str(torrent),str(port)],text=True).strip()
    seed=subprocess.Popen([str(core),'bt-seed',str(torrent),str(source.parent),str(port),'262144'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=subprocess.CREATE_NO_WINDOW)
    data=json.loads(fixture.stdout.readline());pathlib.Path(data['root'],'speed.json').write_text('{"delay":0.01}')
    result=subprocess.run([str(args.exe.resolve()),str(run/'tasks'),data['http'],data['sha256'],uri],capture_output=True,text=True,encoding='utf-8',timeout=150,creationflags=subprocess.CREATE_NO_WINDOW)
    (run/'console.log').write_text(result.stdout+'\n'+result.stderr,encoding='utf-8')
    reopened=subprocess.run([str(args.exe.resolve()),'--reopen-check',str(run/'tasks')],capture_output=True,text=True,encoding='utf-8',timeout=30) if result.returncode==0 else None
    if reopened:
        assert reopened.returncode==0,reopened.stderr
        result.stdout+=reopened.stdout
    checks=[line[5:] for line in result.stdout.splitlines() if line.startswith('PASS ')]
    evidence=dict(passed=result.returncode==0 and 'ALL PASS' in result.stdout,checks=checks,executable_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),fixture=data,returncode=result.returncode)
    kept=run/'tasks/Downloads/1-slow.bin'
    if evidence['passed']:
        evidence['retained_file_sha256']=hashlib.sha256(kept.read_bytes()).hexdigest()
        assert evidence['retained_file_sha256']==data['sha256']
        assert not (run/'tasks/Downloads/2-range.bin').exists()
    (run/'results.json').write_text(json.dumps(evidence,indent=2,ensure_ascii=False),encoding='utf-8')
    print(result.stdout,flush=True)
    assert evidence['passed'],result.stderr
    print('RESULTS: '+str(run/'results.json'),flush=True)
finally:
    if seed:seed.terminate();seed.wait(timeout=10)
    fixture.terminate();fixture.wait(timeout=10)
