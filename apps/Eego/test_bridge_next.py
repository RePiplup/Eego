from pathlib import Path
import subprocess,json,struct,http.server,threading,time,hashlib
R=Path(__file__).resolve().parents[2];E=R/'artifacts/test-runs/eego-next'
import shutil
import tempfile
import atexit
# Give the native host its own library selector. The installed selector points to production.
sandbox = tempfile.TemporaryDirectory()
atexit.register(sandbox.cleanup)
P = Path(sandbox.name)
library = P/'userdata'
library.mkdir()
(P/'library-path.txt').write_text(str(library), encoding='utf8')
installed = R/'dist/Eego'
shutil.copytree(installed, P, dirs_exist_ok=True, ignore=shutil.ignore_patterns(
    'userdata','rollback','evidence','sources','LICENSES','library-path.txt','library-continuity.json'))
ui = subprocess.Popen([P/'HandmadeBTQuick.exe','--data-dir',library])
def close_ui():
    import win32gui, win32process
    def close(hwnd, unused):
        if win32process.GetWindowThreadProcessId(hwnd)[1] == ui.pid:
            win32gui.PostMessage(hwnd, 0x10, 0, 0)
    win32gui.EnumWindows(close, None)
    try: ui.wait(timeout=10)
    except subprocess.TimeoutExpired: ui.terminate(); ui.wait()
atexit.register(close_ui)
import win32gui, win32process
def ready():
    windows = []
    win32gui.EnumWindows(lambda w, _: windows.append(w) if win32gui.IsWindowVisible(w) and win32process.GetWindowThreadProcessId(w)[1] == ui.pid else None, None)
    return bool(windows)
deadline = time.monotonic() + 20
while not ready():
    if ui.poll() is not None or time.monotonic() >= deadline:
        raise RuntimeError("Isolated Eego window failed to start")
    time.sleep(.1)
E.mkdir(parents=True, exist_ok=True)
class H(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  if self.path=='/fail':self.send_error(503);return
  self.send_response(200);self.send_header('Content-Length','4');self.end_headers();self.wfile.write(b'test')
 def log_message(self,*a):pass
s=http.server.ThreadingHTTPServer(('127.0.0.1',0),H);threading.Thread(target=s.serve_forever,daemon=True).start()
records=[]
def call(v):
 b=json.dumps(v).encode();r=subprocess.run([P/'eego_browser_host.exe','chrome-extension://ghkahndjnmpphcogefijjefdlgnilken/'],input=struct.pack('<I',len(b))+b,capture_output=True,timeout=20)
 assert len(r.stdout)>=4,(r.returncode,r.stderr);n=struct.unpack('<I',r.stdout[:4])[0];a=json.loads(r.stdout[4:4+n]);records.append(a);return a
for path,wanted in [('/fast',2),('/fail',3)]:
 url=f'http://127.0.0.1:{s.server_port}{path}';a=call({'url':url});assert a['accepted'];i=a['task_id']
 end=time.time()+15
 while time.time()<end:
  status=call({'task_id':i,'library':a['library']})['status']
  if status==wanted:break
  time.sleep(.25)
 assert status==wanted,status
 assert call({'url':url})['task_id']==i
assert (P/'Downloads/fast').read_bytes()==b'test'
(E/'bridge-result.json').write_text(json.dumps({'passed':True,'records':records,'sha256':hashlib.sha256(files[0].read_bytes()).hexdigest()},indent=2))
s.shutdown();print('PASS real Native Messaging fast completion=2 / immediate failure=3 / duplicate ID')
