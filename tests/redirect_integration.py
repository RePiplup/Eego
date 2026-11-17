"""Real WinHTTP redirect requests with byte verification and fail-closed checks."""
import argparse, datetime, hashlib, http.server, json, pathlib, ssl, subprocess, threading, uuid

ROOT = pathlib.Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser()
ap.add_argument('--exe', type=pathlib.Path, default=ROOT/'build/core_cli.exe')
ap.add_argument('--output-root', type=pathlib.Path, default=ROOT/'artifacts/test-runs')
a = ap.parse_args()
run = a.output_root.resolve()/('redirect-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:6])
run.mkdir(parents=True)
data = bytes(range(256))*4097
records, requests = [], []

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_GET(self):
        requests.append(dict(path=self.path, range=self.headers.get('Range'), cookie=self.headers.get('Cookie'), authorization=self.headers.get('Authorization')))
        route = self.path.split('?')[0]
        if route in ['/chunked','/chunked-short','/chunked-large']:
            self.send_response(200);self.send_header('Content-Type','application/octet-stream');self.send_header('Transfer-Encoding','chunked');self.end_headers()
            payload=data if route!='/chunked-large' else bytes(33*1024*1024)
            try:
                for i in range(0,len(payload),65536):
                    b=payload[i:i+65536]
                    self.wfile.write(f'{len(b):x}\r\n'.encode()+b+b'\r\n')
                if route!='/chunked-short':self.wfile.write(b'0\r\n\r\n')
            except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass
            return
        if route in ['/close-body','/close-large','/empty-body']:
            self.send_response(200);self.send_header('Content-Type','application/octet-stream');self.send_header('Connection','close');self.end_headers()
            self.close_connection=True
            payload=b'' if route=='/empty-body' else bytes(33*1024*1024) if route=='/close-large' else data
            try:self.wfile.write(payload)
            except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass
            return
        redirects = {'/relative':'data', '/absolute':base+'/data', '/network':'//127.0.0.1:'+str(server.server_port)+'/data', '/loop':'/loop', '/cycle-a':'/cycle-b', '/cycle-b':'/cycle-a', '/ftp':'ftp://127.0.0.1/file', '/credentials':base.replace('://','://user:secret@')+'/data', '/tls':tlsbase+'/data', '/cookie':'/data', '/html-redirect':'/html', '/query':'?final=1'}
        if route.startswith('/chain/'):
            redirects[route]='/chain/'+str(int(route.rsplit('/',1)[1])+1)
        if route.startswith('/status/'):
            code=int(route.rsplit('/',1)[1]); redirects[route]='/data'
        else: code=302
        if route in redirects and self.path!='/query?final=1':
            self.send_response(code); self.send_header('Location',redirects[route]); self.send_header('Content-Length','0'); self.send_header('Set-Cookie','secret=do-not-forward'); self.end_headers(); return
        if route in ['/missing','/duplicate']:
            self.send_response(302)
            if route=='/duplicate':
                self.send_header('Location','/data');self.send_header('Location','/other')
            self.send_header('Content-Length','0'); self.end_headers(); return
        if route=='/denied':
            self.send_response(403); self.send_header('Content-Length','0'); self.end_headers(); return
        start,end=0,len(data)-1
        if self.headers.get('Range'): start,end=map(int,self.headers['Range'][6:].split('-'))
        self.send_response(206);self.send_header('Content-Type','text/html' if route=='/html' else 'application/octet-stream')
        self.send_header('ETag','"redirect-data"');self.send_header('Content-Length',str(end-start+1));self.send_header('Content-Range',f'bytes {start}-{end}/{len(data)}');self.end_headers()
        try:self.wfile.write(data[start:end+1])
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass

server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
base=f'http://127.0.0.1:{server.server_port}'
tls=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(ROOT/'tests/fixtures/self-signed.pem',ROOT/'tests/fixtures/test-key.pem')
tls.socket=ctx.wrap_socket(tls.socket,server_side=True)
tlsbase=f'https://localhost:{tls.server_port}'
for s in [server,tls]: threading.Thread(target=s.serve_forever,daemon=True).start()
def call(route, error=None, stop=0, target=None):
    target=target or run/(route.strip('/').replace('/','-')+'.bin')
    p=subprocess.run([str(a.exe.resolve()),'http',base+route,str(target),'1' if route.startswith(('/chunked','/close','/empty')) else '2',str(stop)],capture_output=True,text=True,timeout=60,creationflags=subprocess.CREATE_NO_WINDOW)
    r=dict(route=route,returncode=p.returncode,stderr=p.stderr,stdout=p.stdout);records.append(r)
    assert p.returncode==(1 if error else 3 if stop else 0),r
    if error: assert error in p.stderr and not target.exists(),r
    elif not stop:
        assert target.read_bytes()==(bytes(33*1024*1024) if route in ('/close-large','/chunked-large') else data)
        r['sha256']=hashlib.sha256(target.read_bytes()).hexdigest()
    return target
try:
    for route in ['/relative','/absolute','/network','/query','/cookie']+[f'/status/{s}' for s in [301,302,303,307,308]]:call(route)
    target=call('/relative',stop=1,target=run/'resume.bin');call('/relative',target=target)
    call('/close-body');call('/close-large');call('/empty-body','empty body')
    call('/chunked');call('/chunked-short','WinHTTP error');call('/chunked-large')
    for route,error in [('/loop','redirect loop'),('/cycle-a','redirect loop'),('/chain/0','redirect limit'),('/ftp','only HTTP/HTTPS'),('/credentials','URL credentials'),('/missing','exactly one Location'),('/duplicate','exactly one Location'),('/tls','WinHTTP error'),('/html-redirect','HTML page'),('/denied','HTTP 403')]:call(route,error)
    assert all(not r['cookie'] and not r['authorization'] for r in requests)
    (run/'results.json').write_text(json.dumps(dict(passed=True,tests=records,requests=requests),indent=2))
    print('ALL PASS: '+str(run))
finally:
    server.shutdown();tls.shutdown()
