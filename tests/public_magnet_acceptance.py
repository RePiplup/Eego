"""Bounded public BT acceptance; offline torrent is verification-only, never engine input."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
from urllib.parse import parse_qsl, urlencode


def decode(data):
    at = 0
    def item():
        nonlocal at
        c = data[at:at+1]
        if c == b'i':
            end = data.index(b'e', at); n = int(data[at+1:end]); at = end+1; return n
        if c in (b'l', b'd'):
            at += 1; out = [] if c == b'l' else {}
            while data[at:at+1] != b'e':
                if c == b'l': out.append(item())
                else:
                    key = item(); out[key] = item()
            at += 1; return out
        end = data.index(b':', at); n = int(data[at:end]); at = end+1
        value = data[at:at+n]; at += n; return value
    result = item()
    assert at == len(data)
    return result


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--exe', required=True)
    p.add_argument('--torrent', required=True)
    p.add_argument('--output', required=True)
    p.add_argument('--seconds', type=int, default=300)
    a = p.parse_args()
    exe = Path(a.exe).resolve(); torrent = Path(a.torrent).resolve()
    root = Path(a.output).resolve(); root.mkdir(parents=True, exist_ok=False)
    raw = torrent.read_bytes(); meta = decode(raw); info = meta[b'info']
    entries = info.get(b'files', [{b'path': [], b'length': info.get(b'length', 0)}])
    total = sum(e[b'length'] for e in entries)
    assert 0 < total <= 200*1024*1024, total
    uri = subprocess.check_output([str(exe), 'magnet-uri', str(torrent), '1'], text=True).strip()
    # No explicit peers, HTTP web seeds, or HTTP metadata sources. Only swarm discovery.
    pairs = [(k,v) for k,v in parse_qsl(uri.split('?',1)[1]) if k in ('xt','dn','tr')]
    uri = 'magnet:?' + urlencode(pairs)
    report = dict(magnet=uri, expected_bytes=total, executable=str(exe),
                  executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                  torrent_sha256=hashlib.sha256(raw).hexdigest(),
                  reference_source='https://github.com/webtorrent/webtorrent.io/blob/master/static/torrents/sintel.torrent',
                  method='Fresh Magnet only; no x.pe/ws/xs; independent official torrent piece verification and per-file SHA256',
                  passed=False, files=[])
    (root/'input.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    started = time.monotonic()
    with (root/'stdout.txt').open('w') as out, (root/'stderr.txt').open('w') as err:
        proc = subprocess.Popen([str(exe), 'magnet-get', uri, str(root/'download'), str(a.seconds*1000)], stdout=out, stderr=err)
        print(json.dumps(dict(pid=proc.pid, bytes=total, magnet=uri)), flush=True)
        try: code = proc.wait(timeout=a.seconds+60)
        except subprocess.TimeoutExpired:
            proc.kill(); proc.wait(); code = 'watchdog_killed'
    report.update(exit_code=code, elapsed_seconds=round(time.monotonic()-started,3),
                  stdout=(root/'stdout.txt').read_text(), stderr=(root/'stderr.txt').read_text())
    if code == 0:
        stream = bytearray(); pieces = []; piece_size = info[b'piece length']
        for e in entries:
            parts = [info[b'name'].decode()] + [s.decode() for s in e[b'path']]
            assert all(s not in ('','.','..') and '/' not in s and '\\' not in s for s in parts)
            file = root/'download'/'files'/Path(*parts)
            assert file.stat().st_size == e[b'length']
            sha = hashlib.sha256()
            with file.open('rb') as f:
                while block := f.read(1024*1024):
                    sha.update(block); stream.extend(block)
                    while len(stream) >= piece_size:
                        pieces.append(hashlib.sha1(stream[:piece_size]).digest()); del stream[:piece_size]
            report['files'].append(dict(path='/'.join(parts), bytes=e[b'length'], sha256=sha.hexdigest()))
        if stream: pieces.append(hashlib.sha1(stream).digest())
        assert b''.join(pieces) == info[b'pieces'], 'Independent reference piece hash mismatch'
        report.update(passed=True, verified_pieces=len(pieces))
    (root/'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2), flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
