"""Extract and execute the archive with only Windows system directories in PATH.
The host Python is the external test driver, not a runtime dependency of the bundle.
"""
import argparse,datetime,hashlib,json,os,pathlib,subprocess,sys,uuid,zipfile
sys.stdout.reconfigure(encoding='utf-8')
sys.stderr.reconfigure(encoding='utf-8')
ROOT=pathlib.Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('archive',type=pathlib.Path)
parser.add_argument('--unicode',action='store_true')
args=parser.parse_args()
archive=args.archive.resolve()
run=ROOT/'artifacts/package-tests'/(('中文 验收 ' if args.unicode else 'Clean Room ')+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+' '+uuid.uuid4().hex[:6])
run.mkdir(parents=True)
with zipfile.ZipFile(archive) as z:
    for name in z.namelist():
        if not (run/name).resolve().is_relative_to(run):raise RuntimeError('Unsafe archive member')
    z.extractall(run)
bundle=run/archive.stem
env=os.environ.copy();windows=pathlib.Path(os.environ['SystemRoot']);env['PATH']=str(windows/'System32')+';'+str(windows)
for k in list(env):
    if k.upper().startswith(('PYTHON','CONDA','VIRTUAL_ENV')):env.pop(k,None)
cwd=run/('无关 工作目录' if args.unicode else 'Unrelated Working Directory');cwd.mkdir()
command=f'"{windows / "System32/cmd.exe"}" /d /s /c ""{bundle / "run-selftest.cmd"}" --online"'
with (run/'launcher.log').open('w',encoding='utf-8') as log:
    process=subprocess.run(command,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=420,creationflags=subprocess.CREATE_NO_WINDOW)
print((run/'launcher.log').read_text(encoding='utf-8'),flush=True)
if process.returncode:raise RuntimeError('Clean directory selftest failed: '+str(run))
summary_paths=list(bundle.glob('test-output/acceptance-*/summary.json'))
assert len(summary_paths)==1
summary=json.loads(summary_paths[0].read_text())
assert summary['passed'] and summary['online'] and summary['integration_tests']==20
assert summary['magnet_tests']>=8
crash=json.loads(pathlib.Path(summary['crash_results']).read_text())
assert crash['all_passed'] and summary['crash_recovery_tests']==len(crash['tests'])
assert any(t.get('killed') for t in crash['tests'])
evidence=json.loads(pathlib.Path(summary['environment']).read_text())
assert pathlib.Path(evidence['python']).is_relative_to(bundle/'python')
assert evidence['child_path']==env['PATH']
result=json.loads(pathlib.Path(summary['results']).read_text())
assert pathlib.Path(result['executable']).is_relative_to(bundle/'bin')
mixed=next(t for t in result['tests'] if t['name']=='mixed-shared-filestore')
assert len(mixed['verified_files'])==2
assert mixed['metrics']['shared_store_bytes']==2*4194341
# Verify source delivery contents and revisions separately, without building again.
with zipfile.ZipFile(bundle/'source.zip') as source:
    assert source.testzip() is None
    snapshots=json.loads(source.read('provenance/source-snapshot.json'))
    total=0
    for relative,record in snapshots.items():
        for name,expected in record['files'].items():
            assert hashlib.sha256(source.read(relative+'/'+name)).hexdigest()==expected
            total+=1
    for required in ['CMakeLists.txt','src/bt_disk.cpp','src/http.cpp','tools/build.ps1','tools/bootstrap.py','dependencies/boost_1_82_0/boost/version.hpp','COPYING']:
        assert required in source.namelist()
# Deliberately tamper with README after successful test; guard must reject before new transfers.
readme=bundle/'README.txt';original=readme.read_bytes()
try:
    readme.write_bytes(original+b'\nintentional integrity-test modification\n')
    bad=subprocess.run([str(bundle/'python/python.exe'),'-I',str(bundle/'selftest.py'),'--offline'],cwd=cwd,env=env,capture_output=True,text=True,encoding="utf-8",timeout=30,creationflags=subprocess.CREATE_NO_WINDOW)
    assert bad.returncode==1 and 'integrity check failed: README.txt' in bad.stderr
    assert len(list(bundle.glob('test-output/acceptance-*')))==1
finally:readme.write_bytes(original)
report=dict(passed=True,archive=str(archive),archive_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),extracted_root=str(bundle),working_directory=str(cwd),path=env['PATH'],summary=summary,source_snapshot_files_verified=total,tamper_rejected=True,core_executable_sha256=hashlib.sha256((bundle/'bin/core_cli.exe').read_bytes()).hexdigest())
if (bundle/'bin/handmadebt.exe').exists():
    assert (bundle/'HandmadeBT.cmd').is_file() and (bundle/'GUI-Isolated-Test.cmd').is_file()
    gui_hash=hashlib.sha256((bundle/'bin/handmadebt.exe').read_bytes()).hexdigest()
    assert gui_hash==json.loads((bundle/'provenance/package-inputs.json').read_text())['gui_executable_sha256']
    report['gui_executable_sha256']=gui_hash
    report['gui_interaction_test']='Separate recorded UI acceptance; this script tests the core and package integrity.'
(run/'acceptance.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('PACKAGE ACCEPTANCE PASS: '+str(run/'acceptance.json'),flush=True)
