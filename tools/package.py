"""Create a new portable validation bundle and complete core source snapshot.
Never overwrites a previous package. Requires the already validated build.
"""
import argparse,datetime,hashlib,json,pathlib,shutil,subprocess,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
VERSION='0.8.1-safe-redirect'
PY_URL='https://www.python.org/ftp/python/3.12.7/python-3.12.7-embed-amd64.zip'
PY_SHA='0d57bb6cb078b74d23dbfe91f77d6780d45bed328911609f1f7ee2ba1606bf44'
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=pathlib.Path,required=True);args=ap.parse_args()
    package=args.output.resolve()
    if not package.is_relative_to(ROOT):raise RuntimeError('Package must be inside workspace')
    if package.exists():raise RuntimeError('Refusing to overwrite package directory')
    runtime=ROOT/'tools/downloads/python-3.12.7-embed-amd64.zip'
    if sha(runtime)!=PY_SHA:raise RuntimeError('Python runtime checksum mismatch')
    lock=json.loads((ROOT/'provenance/inputs.lock.json').read_text())
    for name in ['aria2','libtorrent']:
        repo=ROOT/'audit/upstream'/name
        if subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()!=lock[name]:raise RuntimeError('Unexpected upstream revision')
        if subprocess.check_output(['git','-C',str(repo),'status','--porcelain'],text=True).strip():raise RuntimeError('Upstream source is modified')
    package.mkdir(parents=True)
    def copy(src,dst):
        target=package/dst;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/src,target)
    for name in ['redirect_policy_test.exe','manager_test.exe','handmadebt.exe','handmadebt_host.exe','core_cli.exe','parser_test.exe','upstream_header_test.exe','checkpoint_test.exe','libc++.dll','libunwind.dll']:copy('build/'+name,'bin/'+name)
    for name in ['HandmadeBT.cmd','GUI-Isolated-Test.cmd','Browser-Setup.cmd','Magnet-Isolated-Test.cmd']:copy('packaging/'+name,name)
    zipfile.ZipFile(runtime).extractall(package/'python')
    copy('packaging/README.txt','README.txt');copy('packaging/selftest.py','selftest.py');copy('packaging/run-selftest.cmd','run-selftest.cmd');copy('COPYING','COPYING')
    for directory in ['tests','LICENSES','provenance','browser']:
        for p in (ROOT/directory).rglob('*'):
            if p.is_file() and '__pycache__' not in p.parts:copy(p.relative_to(ROOT),p.relative_to(ROOT))
    for name in ['DELIVERY_081.md','TASK_MANAGEMENT_08.md','TASK_MANAGEMENT_07.md','MIGRATION.md','VALIDATION.md','PORTABLE.md','CRASH_RECOVERY.md','CHECKPOINT_PERFORMANCE.md','UNICODE_PATHS.md','GUI_QUICKSTART.md','MAGNET_BROWSER.md']:copy('docs/'+name,'docs/'+name)
    copy('README.md','docs/PROJECT-README.md');copy('docs/results/latest.json','docs/results/latest.json')
    for name in ['magnet-browser-06.json','magnet-06.json','regression-06.json','crash-06.json','magnet-ui-hashes-06.json','magnet-ui-completed.txt','browser-06-1.json','browser-06-2.json']:
        copy('docs/results/'+name,'docs/results/'+name)
    for pattern in ['*07*.json','*08*.json']:
        for p in (ROOT/'docs/results').glob(pattern):copy(p.relative_to(ROOT),p.relative_to(ROOT))
    provenance=dict(version=VERSION,python=dict(version='3.12.7',url=PY_URL,sha256=PY_SHA),core_executable_sha256=sha(package/'bin/core_cli.exe'),gui_executable_sha256=sha(package/'bin/handmadebt.exe'),source_lock=lock)
    provenance.update(browser_host_sha256=sha(package/'bin/handmadebt_host.exe'),extension_id=(ROOT/'browser/extension-id.txt').read_text().strip())
    (package/'provenance/package-inputs.json').write_text(json.dumps(provenance,indent=2),encoding='utf-8')
    # Original core sources and dependency source, not just URLs or a source offer.
    snapshots={}
    with zipfile.ZipFile(package/'source.zip','w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for directory in ['src','tests','tools','packaging','docs','LICENSES','provenance','browser']:
            entries=(ROOT/directory).glob('*') if directory=='tools' else (ROOT/directory).rglob('*')
            for p in sorted(entries):
                if not p.is_file() or '__pycache__' in p.parts:continue
                if directory=='tools' and p.parent!=ROOT/'tools':continue
                if directory=='tools' and p.suffix not in ['.py','.ps1']:continue
                z.write(p,p.relative_to(ROOT).as_posix())
        for name in ['README.md','CMakeLists.txt','COPYING','.gitignore']:z.write(ROOT/name,name)
        for name,relative in [('aria2','audit/upstream/aria2'),('libtorrent','audit/upstream/libtorrent'),('try_signal','audit/upstream/libtorrent/deps/try_signal')]:
            repo=ROOT/relative;files={}
            for raw in subprocess.check_output(['git','-C',str(repo),'ls-files','-z']).split(b'\0'):
                if not raw:continue
                rel=raw.decode('utf-8');p=repo/rel
                if p.is_file():z.write(p,relative+'/'+rel);files[rel]=sha(p)
            snapshots[relative]=dict(revision=lock[name],files=files)
        for p in sorted((ROOT/'dependencies/boost_1_82_0').rglob('*')):
            if p.is_file():z.write(p,p.relative_to(ROOT).as_posix())
        z.writestr('provenance/source-snapshot.json',json.dumps(snapshots,indent=2))
    manifest=dict(version=VERSION,files={})
    for p in sorted(package.rglob('*')):
        if p.is_file():manifest['files'][p.relative_to(package).as_posix()]=sha(p)
    (package/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    archive=package.parent/(package.name+'.zip')
    if archive.exists():raise RuntimeError('Refusing to overwrite archive')
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for p in sorted(package.rglob('*')):
            if p.is_file():z.write(p,package.name+'/'+p.relative_to(package).as_posix())
    archive.with_suffix('.zip.sha256').write_text(sha(archive)+'  '+archive.name+'\n')
    print(json.dumps(dict(package=str(package),archive=str(archive),sha256=sha(archive),bytes=archive.stat().st_size),indent=2))
if __name__=='__main__':main()
