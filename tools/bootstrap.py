"""Install pinned portable build inputs INSIDE this project; no machine settings."""
import hashlib,json,pathlib,subprocess,sys,tarfile,urllib.request,zipfile
root=pathlib.Path(__file__).resolve().parents[1]
lock=json.loads((root/'provenance/inputs.lock.json').read_text())
snapshot_path=root/'provenance/source-snapshot.json'
snapshots=json.loads(snapshot_path.read_text()) if snapshot_path.exists() else {}
def run(*args):subprocess.run(args,cwd=root,check=True)
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def source_snapshot(relative,revision):
    record=snapshots.get(relative)
    if record is None or not (root/relative).is_dir() or not any((root/relative).iterdir()):return False
    if record['revision']!=revision:raise RuntimeError('Source snapshot revision mismatch: '+relative)
    for name,expected in record['files'].items():
        path=(root/relative/name).resolve()
        if not path.is_relative_to((root/relative).resolve()) or not path.is_file() or sha(path)!=expected:
            raise RuntimeError('Source snapshot modified/missing: '+str(path)+'; use build.ps1 directly after intentional edits')
    return True
for a in lock['artifacts']:
    dest=root/a['path'];dest.parent.mkdir(parents=True,exist_ok=True)
    if not dest.exists():
        if a['url']:urllib.request.urlretrieve(a['url'],dest)
        else:run(sys.executable,'-m','pip','download','--only-binary=:all:','--no-deps',{'cmake':'cmake==3.31.6','ninja':'ninja==1.11.1.3'}[a['name']],'-d',str(dest.parent))
    if sha(dest)!=a['sha256']:raise RuntimeError('Checksum mismatch: '+str(dest))
    if a['name']=='llvm-mingw':
        if not (root/'tools/llvm-mingw-20261020-ucrt-x86_64/bin/clang++.exe').exists():zipfile.ZipFile(dest).extractall(root/'tools')
    elif a['name']=='boost':
        if not (root/'dependencies/boost_1_82_0/boost/version.hpp').exists():
            with tarfile.open(dest) as t:t.extractall(root/'dependencies',members=(m for m in t if m.name.startswith('boost_1_82_0/boost/') or m.name=='boost_1_82_0/LICENSE_1_0.txt'),filter='data')
    else:
        if not (root/'tools/python-tools'/('cmake/data/bin/cmake.exe' if a['name']=='cmake' else 'ninja-1.11.1.3.data/scripts/ninja.exe')).exists():zipfile.ZipFile(dest).extractall(root/'tools/python-tools')
for name,url in [('aria2','https://github.com/aria2/aria2.git'),('libtorrent','https://github.com/arvidn/libtorrent.git')]:
    dest=root/'audit/upstream'/name
    if source_snapshot('audit/upstream/'+name,lock[name]):continue
    if not (dest/'.git').exists():
        dest.mkdir(parents=True,exist_ok=True)
        run('git','init',str(dest));run('git','-C',str(dest),'remote','add','origin',url)
        run('git','-C',str(dest),'fetch','--depth','1','origin',lock[name]);run('git','-C',str(dest),'checkout','--detach',lock[name])
    actual=subprocess.check_output(['git','-C',str(dest),'rev-parse','HEAD'],text=True).strip()
    if actual!=lock[name]:raise RuntimeError('Unexpected upstream revision; refusing reset: '+str(dest))
sub=root/'audit/upstream/libtorrent/deps/try_signal'
if not source_snapshot('audit/upstream/libtorrent/deps/try_signal',lock['try_signal']):
    if not (sub/'.git').exists():run('git','-C',str(root/'audit/upstream/libtorrent'),'submodule','update','--init','deps/try_signal')
    if subprocess.check_output(['git','-C',str(sub),'rev-parse','HEAD'],text=True).strip()!=lock['try_signal']:raise RuntimeError('Unexpected try_signal revision')
print('Pinned portable inputs verified. Run tools/build.ps1 next.')
