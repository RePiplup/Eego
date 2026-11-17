"""Read-only verification after manual/Computer Use public GUI acceptance."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
from verify_browser_magnet import sealed, digest
from public_magnet_acceptance import decode

p = argparse.ArgumentParser()
p.add_argument('--run', type=Path, required=True)
p.add_argument('--reference', type=Path, required=True)
p.add_argument('--torrent', type=Path, required=True)
p.add_argument('--bundle', type=Path, required=True)
a = p.parse_args()
ref = json.loads(a.reference.read_text(encoding='utf-8'))
raw = a.torrent.read_bytes()
assert hashlib.sha256(raw).hexdigest() == ref['torrent_sha256']
info = decode(raw)[b'info']
record = a.run/'userdata/tasks.record'
tasks = [shlex.split(line) for line in sealed(record).decode().splitlines()[1:] if line]
assert len(tasks) == 1
task = tasks[0]
assert task[1] == '3' and task[2] == task[3] == str(ref['expected_bytes'])
assert task[4] == ref['magnet']
folder = Path(task[5]); manifest = sealed(folder/'files.record')
assert hashlib.sha256(manifest).hexdigest() == task[7]
files = []
for wanted in ref['files']:
    f = folder/'files'/wanted['path']
    actual = dict(path=wanted['path'], bytes=f.stat().st_size, sha256=digest(f))
    assert actual == wanted, wanted['path']
    files.append(actual)
assert {f.relative_to(folder/'files').as_posix() for f in (folder/'files').rglob('*') if f.is_file()} == {f['path'] for f in files}
stream = bytearray(); pieces = []; size = info[b'piece length']
for entry in info[b'files']:
    f = folder/'files'/info[b'name'].decode()/Path(*[s.decode() for s in entry[b'path']])
    with f.open('rb') as source:
        while block := source.read(1048576):
            stream.extend(block)
            while len(stream) >= size:
                pieces.append(hashlib.sha1(stream[:size]).digest()); del stream[:size]
if stream: pieces.append(hashlib.sha1(stream).digest())
assert b''.join(pieces) == info[b'pieces']
assert (a.run/'original-tasks.record').read_bytes() == (a.bundle/'userdata/tasks.record').read_bytes()
assert (a.run/'paused-tasks.record').read_bytes() == (a.run/'reopened-tasks.record').read_bytes()
paused = shlex.split(sealed(a.run/'paused-tasks.record').decode().splitlines()[1])
assert paused[1] == '2' and 0 < int(paused[2]) < int(paused[3])
report = dict(passed=True, files=files, verified_pieces=len(pieces),
              manifest_sha256=task[7], paused_bytes=int(paused[2]), total_bytes=int(task[3]),
              original_tasks_unchanged=True, reopened_record_matches=True,
              gui_sha256=digest(a.bundle/'bin/handmadebt.exe'))
output = a.run/'content-verification.json'
output.write_text(json.dumps(report, indent=2), encoding='utf-8')
print('PASS: ' + str(output))
