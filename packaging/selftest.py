"""Portable entry point. No Git, CMake, compiler, installed Python or pip."""
import argparse, datetime, hashlib, json, os, pathlib, subprocess, sys, uuid

sys.stdout.reconfigure(encoding='utf-8')
sys.stderr.reconfigure(encoding='utf-8')
ROOT=pathlib.Path(__file__).resolve().parent
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def main():
    p=argparse.ArgumentParser(description='HandmadeBT portable validation. Default includes public HTTPS.')
    p.add_argument('--offline',action='store_true',help='Skip public HTTPS only; still test untrusted local TLS rejection.')
    p.add_argument('--online',action='store_true',help='Explicit default; useful for non-interactive batch launch.')
    args=p.parse_args()
    if args.offline and args.online:p.error('choose either --offline or --online')
    print('HandmadeBT migration validation (not a complete downloader)',flush=True)
    manifest=json.loads((ROOT/'manifest.json').read_text(encoding='utf-8'))
    for name,expected in manifest['files'].items():
        candidate=(ROOT/name).resolve()
        if not candidate.is_relative_to(ROOT) or not candidate.is_file() or sha(candidate)!=expected:
            raise RuntimeError('Package integrity check failed: '+name)
    print('Package integrity: PASS',flush=True)
    # Only Windows system paths; child DLLs must come from the supplied bin directory.
    env=os.environ.copy()
    windows=pathlib.Path(os.environ.get('SystemRoot',r'C:\Windows'))
    env['PATH']=str(windows/'System32')+';'+str(windows)
    for key in list(env):
        if key.upper().startswith(('PYTHON','CONDA','VIRTUAL_ENV')):env.pop(key,None)
    output=ROOT/'test-output'/('acceptance-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:6])
    output.mkdir(parents=True)
    evidence=dict(package_root=str(ROOT),python=sys.executable,python_version=sys.version,python_path=sys.path,child_path=env['PATH'],original_path=os.environ.get('PATH',''),online=not args.offline,package_version=manifest['version'])
    (output/'environment.json').write_text(json.dumps(evidence,indent=2),encoding='utf-8')
    commands=[
        [str(ROOT/'bin/parser_test.exe')],
        [str(ROOT/'bin/upstream_header_test.exe')],
        [str(ROOT/'bin/checkpoint_test.exe')],
        [str(ROOT/'bin/redirect_policy_test.exe')],
        [sys.executable,'-I',str(ROOT/'tests/integration.py'),'--exe',str(ROOT/'bin/core_cli.exe'),'--output-root',str(output)],
    ]
    if not args.offline:commands[-1].append('--online')
    commands.append([sys.executable,'-I',str(ROOT/'tests/crash_recovery.py'),'--exe',str(ROOT/'bin/core_cli.exe'),'--output-root',str(output)])
    commands.append([sys.executable,'-I',str(ROOT/'tests/magnet_integration.py'),'--exe',str(ROOT/'bin/core_cli.exe'),'--output-root',str(output)])
    commands.append([sys.executable,'-I',str(ROOT/'tests/magnet_selection.py'),'--exe',str(ROOT/'bin/core_cli.exe'),'--output-root',str(output)])
    commands.append([sys.executable,'-I',str(ROOT/'tests/manager_integration.py'),'--exe',str(ROOT/'bin/manager_test.exe'),'--output-root',str(output)])
    commands.append([sys.executable,'-I',str(ROOT/'tests/redirect_integration.py'),'--exe',str(ROOT/'bin/core_cli.exe'),'--output-root',str(output)])
    for command in commands:
        with (output/'console.log').open('a',encoding='utf-8') as log:
            child=subprocess.Popen(command,cwd=output,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                for line in child.stdout:
                    print(line,end='',flush=True);log.write(line);log.flush()
                rc=child.wait()
            except BaseException:
                child.terminate();child.wait();raise
            if rc:raise RuntimeError('Self-test failed; see '+str(output/'console.log'))
    results=list(output.glob('run-*/results.json'))
    if len(results)!=1:raise RuntimeError('Missing or ambiguous test result')
    data=json.loads(results[0].read_text())
    if not data.get('all_passed'):raise RuntimeError('Incomplete test result')
    summary=dict(passed=True,unit_executables=4,integration_tests=len(data['tests']),online=not args.offline,results=str(results[0]),environment=str(output/'environment.json'))
    redirects=list(output.glob('redirect-*/results.json'))
    if len(redirects)!=1:raise RuntimeError('Missing redirect result')
    redirect=json.loads(redirects[0].read_text())
    if not redirect.get('passed'):raise RuntimeError('Redirect tests incomplete')
    summary.update(redirect_tests=len(redirect['tests']),redirect_results=str(redirects[0]))
    crashes=list(output.glob('crash-*/results.json'))
    if len(crashes)!=1:raise RuntimeError('Missing crash-recovery result')
    crash=json.loads(crashes[0].read_text())
    if not crash.get('all_passed'):raise RuntimeError('Crash-recovery tests incomplete')
    summary.update(crash_recovery_tests=len(crash['tests']),crash_results=str(crashes[0]))
    magnets=list(output.glob('magnet-*/results.json'))
    if len(magnets)!=1:raise RuntimeError('Missing Magnet result')
    magnet=json.loads(magnets[0].read_text())
    if not magnet.get('all_passed'):raise RuntimeError('Magnet tests incomplete')
    summary.update(magnet_tests=len(magnet['tests']),magnet_results=str(magnets[0]))
    selections=list(output.glob('selection-*/results.json'))
    if len(selections)!=1:raise RuntimeError('Missing selective download result')
    selection=json.loads(selections[0].read_text())
    if not selection.get('passed'):raise RuntimeError('Selective download tests incomplete')
    summary.update(selection_tests=len(selection['tests']),selection_results=str(selections[0]))
    managers=list(output.glob('manager08-*/results.json'))
    assert len(managers)==1
    manager=json.loads(managers[0].read_text(encoding='utf-8'));assert manager['passed']
    summary.update(manager_tests=len(manager['checks']),manager_results=str(managers[0]))
    (output/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
    print('\nALL TESTS PASSED. Results: '+str(output),flush=True)
    if args.offline:print('Public trusted HTTPS was SKIPPED (--offline).',flush=True)
    return 0
if __name__=='__main__':
    try:sys.exit(main())
    except Exception as e:
        print('\nFAILED: '+str(e),file=sys.stderr,flush=True);sys.exit(1)
