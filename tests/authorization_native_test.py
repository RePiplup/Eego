"""Real HTTP + GUI + native framing, isolated directory, no registry changes."""
import pathlib,shutil,subprocess,struct,json,http.server,threading,time,uuid
root=pathlib.Path(__file__).resolve().parents[1]
run=root/'artifacts/test-runs'/('authorization-native-'+uuid.uuid4().hex[:8]);(run/'bin').mkdir(parents=True)
for name in ['handmadebt.exe','handmadebt_host.exe','libc++.dll','libunwind.dll']:
    shutil.copy2(root/'dist/HandmadeBT'/name,run/'bin'/name)
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self,*args): pass
    def do_GET(self):
        route=self.path
        if route in ('/401','/403','/404'):
            self.send_response(int(route[1:]));self.send_header('Content-Length','0');self.end_headers();return
        if route=='/redirect-login':
            self.send_response(302);self.send_header('Location','/login');self.send_header('Content-Length','0');self.end_headers();return
        if route=='/cross-denied':
            self.send_response(302);self.send_header('Location',f'http://localhost:{self.server.server_port}/403');self.send_header('Content-Length','0');self.end_headers();return
        if route in ('/html','/login'):
            body=b'<html>fixture</html>';self.send_response(200);self.send_header('Content-Type','text/html')
        else:
            body=b'fixture download';self.send_response(200);self.send_header('Content-Type','application/octet-stream')
        self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
def native(message):
    data=json.dumps(message).encode()
    result=subprocess.run([str(run/'bin/handmadebt_host.exe'),'chrome-extension://ghkahndjnmpphcogefijjefdlgnilken/'],input=struct.pack('<I',len(data))+data,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=25,creationflags=subprocess.CREATE_NO_WINDOW)
    assert result.returncode==0,result.stderr
    size=struct.unpack('<I',result.stdout[:4])[0]
    return json.loads(result.stdout[4:4+size])
gui=subprocess.Popen([str(run/'bin/handmadebt.exe')],creationflags=subprocess.CREATE_NO_WINDOW)
tests=[]
try:
    for route,expected in [('/401',5),('/403',5),('/404',3),('/html',3),('/redirect-login',5),('/cross-denied',3),('/ok',2)]:
        task=native({'url':f'http://127.0.0.1:{server.server_port}{route}'})['task_id']
        deadline=time.monotonic()+20
        while True:
            state=native({'task_id':task})['status']
            if state!=1: break
            assert time.monotonic()<deadline
            time.sleep(.1)
        assert state==expected,(route,state,expected)
        tests.append({'route':route,'status':state})
    (run/'results.json').write_text(json.dumps({'passed':True,'tests':tests},indent=2))
    print(json.dumps({'passed':True,'tests':tests,'run':str(run)}))
finally:
    gui.terminate();gui.wait(timeout=10);server.shutdown()

    for name in ['handmadebt.exe','handmadebt_host.exe','libc++.dll','libunwind.dll']:
        (run/'bin'/name).unlink(missing_ok=True)
