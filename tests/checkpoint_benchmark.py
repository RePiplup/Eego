"""Real loopback HTTP benchmark; unique outputs, full SHA256, no mock transport.
Run sequentially with no other download tests. Results retain every observation.
"""
import argparse, datetime, hashlib, http.server, json, pathlib, platform
import random, statistics, subprocess, sys, threading, time, uuid

sys.stdout.reconfigure(encoding='utf-8')
sys.stderr.reconfigure(encoding='utf-8')
ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--exe', type=pathlib.Path, default=ROOT/'build/core_cli.exe')
p.add_argument('--label', default='two-blocks')
p.add_argument('--repeats', type=int, default=5)
p.add_argument('--batch', type=int, default=2)
args = p.parse_args()
assert args.repeats >= 3
exe = args.exe.resolve()
run = ROOT/'artifacts/test-runs'/('benchmark-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:6])
run.mkdir(parents=True)
data = random.Random(20261026).randbytes(32*1024*1024+37)
sizes = [4*1024*1024+37, len(data)]
hashes = {n:hashlib.sha256(data[:n]).hexdigest() for n in sizes}

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *args): pass
    def do_GET(self):
        size = int(self.path[1:])
        assert size in sizes
        start, end = map(int, self.headers['Range'].removeprefix('bytes=').split('-'))
        assert 0 <= start <= end < size
        self.send_response(206)
        self.send_header('ETag', '"benchmark-v1"')
        self.send_header('Content-Length', str(end-start+1))
        self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
        self.send_header('Connection','close')
        self.end_headers()
        try: self.wfile.write(memoryview(data)[start:end+1])
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError): pass
        self.close_connection = True

class BenchmarkServer(http.server.ThreadingHTTPServer):
    request_queue_size = 64
server = BenchmarkServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
records = []
report = dict(label=args.label, executable=str(exe), executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
              platform=platform.platform(), python=platform.python_version(), repeats=args.repeats,
              methodology='One warmup then randomized sequential runs; server in memory, Windows cache uncontrolled; hash outside measured time; no concurrent benchmark suites.',
              records=records, all_passed=False)
def persist(): (run/'results.json').write_text(json.dumps(report,indent=2), encoding='utf-8')
def transfer(size, segments, warmup=False):
    out = run/f'{len(records):03d}.bin'
    command = [str(exe),'http',f'http://127.0.0.1:{server.server_port}/{size}',str(out),str(segments)]
    begin = time.perf_counter()
    proc = subprocess.run(command,capture_output=True,text=True,encoding="utf-8",timeout=180,creationflags=subprocess.CREATE_NO_WINDOW)
    seconds = time.perf_counter()-begin
    rec = dict(size=size, segments=segments, warmup=warmup, seconds=seconds, returncode=proc.returncode, stderr=proc.stderr)
    records.append(rec); persist()
    assert proc.returncode == 0, proc.stderr
    rec['metrics'] = json.loads(proc.stdout.strip())
    rec['sha256'] = hashlib.sha256(out.read_bytes()).hexdigest()
    assert rec['sha256'] == hashes[size]
    assert rec['metrics']['written_bytes'] == size
    assert rec['metrics']['checkpoint_count'] == ((size+262143)//262144)//args.batch+2
    rec['mib_s'] = size/1048576/seconds
    persist()
    print(f'{args.label}: {size} bytes, {segments} workers, {seconds:.3f}s, {rec["mib_s"]:.1f} MiB/s',flush=True)
try:
    transfer(sizes[0],4,True)
    jobs = [(size,segments) for _ in range(args.repeats) for size in sizes for segments in [1,4,8]]
    random.Random(20261026).shuffle(jobs)
    for size,segments in jobs: transfer(size,segments)
    report['summary'] = []
    for size in sizes:
        for segments in [1,4,8]:
            rs = [r for r in records if not r['warmup'] and r['size']==size and r['segments']==segments]
            report['summary'].append(dict(size=size,segments=segments,
                median_seconds=statistics.median(r['seconds'] for r in rs),
                min_seconds=min(r['seconds'] for r in rs),max_seconds=max(r['seconds'] for r in rs),
                median_mib_s=statistics.median(r['mib_s'] for r in rs),
                checkpoint_count=rs[0]['metrics']['checkpoint_count'],
                median_checkpoint_ms=statistics.median(r['metrics']['checkpoint_us']/1000 for r in rs),
                median_checkpoint_fraction=statistics.median(r['metrics']['checkpoint_us']/max(1,r['metrics']['elapsed_ms']*1000) for r in rs)))
    report['all_passed'] = True; persist()
    print('BENCHMARK PASS: '+str(run/'results.json'),flush=True)
finally:
    server.shutdown(); server.server_close()
