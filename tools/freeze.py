"""Capture the exact verified inputs. Run only when intentionally updating baseline."""
import hashlib,json,pathlib,subprocess,platform
root=pathlib.Path(__file__).resolve().parents[1]
def sha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
artifacts=[]
for name,path,url in [
 ('llvm-mingw','tools/llvm-mingw.zip','https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/llvm-mingw-20261020-ucrt-x86_64.zip'),
 ('boost','tools/downloads/boost_1_82_0.tar.bz2','https://archives.boost.io/release/1.82.0/source/boost_1_82_0.tar.bz2'),
 ('cmake','tools/downloads/cmake-3.31.6-py3-none-win_amd64.whl',None),
 ('ninja','tools/downloads/ninja-1.11.1.3-py3-none-win_amd64.whl',None)]:
    artifacts.append(dict(name=name,path=path,url=url,sha256=sha(root/path)))
lock=dict(aria2='9e7273583f83e881e3ec067b523ba88724088d2f',libtorrent='eb397173da6189dad52f008916c27419dde32faa',try_signal='105cce59972f925a33aa6b1c3109e4cd3caf583d',comparison_only_2_1='87b553a1813bb23f77311d0063461b731ba35bdb',python=platform.python_version(),artifacts=artifacts)
(root/'provenance/inputs.lock.json').write_text(json.dumps(lock,indent=2))
