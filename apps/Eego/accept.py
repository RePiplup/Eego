"""Exercise a deployed Qt Quick executable on a private Windows desktop."""
import ctypes
import hashlib
import http.server
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time

import psutil
import win32event
import win32gui as gui
import win32process
import win32service
import win32ui
from PIL import Image, ImageChops

root = Path(__file__).resolve().parents[2]
exe = Path(sys.argv[1]).resolve()
if len(sys.argv) == 2:
    run = root / "artifacts/test-runs/qt-quick" / str(time.time_ns())
    run.mkdir(parents=True)
    name = "HandmadeBT-Quick-" + run.name
    desktop = win32service.CreateDesktop(name, 0, 0x1ff, None)
    startup = win32process.STARTUPINFO()
    startup.lpDesktop = name
    args = subprocess.list2cmdline([sys.executable, __file__, str(exe), str(run)])
    proc, _, _, _ = win32process.CreateProcess(None, args, None, None, False, 0x08000000, None, str(root), startup)
    print(run, flush=True)
    win32event.WaitForSingleObject(proc, -1)
    sys.exit(win32process.GetExitCodeProcess(proc))

run = Path(sys.argv[2])
sys.stderr = (run / "test.log").open("w", encoding="utf8", buffering=1)
user = ctypes.windll.user32
user.SetProcessDPIAware()
powershell = shutil.which("pwsh") or shutil.which("powershell")
inspector = subprocess.Popen([powershell, "-NoProfile", "-File", str(root/"apps/Eego/ui-tree.ps1")],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=sys.stderr, encoding="utf8")
block = bytes(range(256)) * 256
count = 160
digest = hashlib.sha256(block*count).hexdigest()
lib = run / "library"
app = None
handle = 0
ui = []
evidence = {}
def wait(f, timeout=35):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        val=f()
        if val: return val
        time.sleep(.1)
    raise TimeoutError(f)

class Source(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.endswith("missing.bin"):
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Transfer-Encoding","chunked")
        self.end_headers()
        try:
            for _ in range(count):
                self.wfile.write(b"10000\r\n"+block+b"\r\n")
                self.wfile.flush()
                time.sleep(.09)
            self.wfile.write(b"0\r\n\r\n")
        except OSError:
            pass
    def log_message(self,*args): pass

def tree(op="read", value=""):
    global ui
    inspector.stdin.write(json.dumps(dict(window=handle,operation=op,value=value))+"\n")
    inspector.stdin.flush()
    result=json.loads(inspector.stdout.readline())
    if isinstance(result,dict): raise RuntimeError(result)
    ui=result
    return result

def find(label, prefix=False):
    return next((n for n in tree() if n["name"]==label or (prefix and n["name"].startswith(label))),None)

def click(label, repeat=1):
    n=wait(lambda: find(label))
    assert n["enabled"],label
    x,y=gui.ScreenToClient(handle,(round(n["x"]+n["width"]/2), round(n["y"]+n["height"]/2)))
    pt=(y<<16)|(x&65535)
    gui.SendMessage(handle,0x200,0,pt)
    for _ in range(repeat):
        gui.SendMessage(handle,0x201,1,pt)
        gui.SendMessage(handle,0x202,0,pt)

def window():
    result=[]
    try:
        gui.EnumWindows(lambda w,_:result.append(w) if win32process.GetWindowThreadProcessId(w)[1]==app.pid and "下载中心" in gui.GetWindowText(w) else None,None)
    except gui.error as err:
        if err.winerror: raise
    if app.poll() is not None: raise RuntimeError("frontend exited "+str(app.returncode))
    return next(iter(result),0)

def launch(zoom=1, binary=exe):
    global app,handle
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(("QT_","QML","PYTHON","CONDA"))}
    env["PATH"]=os.environ["SystemRoot"]+"\\System32;"+os.environ["SystemRoot"]
    env["QT_SCALE_FACTOR"]=str(zoom)
    app=subprocess.Popen([binary,"--data-dir",lib],env=env,cwd=run,stderr=sys.stderr,stdout=sys.stderr)
    handle=wait(window)
    wait(lambda:find("＋ 新建任务"))
    return app

def child():
    return next((p for p in psutil.Process(app.pid).children() if "task_service_host" in p.name()),None)

def stop():
    p=child()
    gui.PostMessage(handle,0x10,0,0)
    assert app.wait(30)==0
    assert p is None or not p.is_running(), "orphan service"

def capture(tag):
    l,t,r,b=gui.GetWindowRect(handle)
    dc=gui.GetWindowDC(handle); src=win32ui.CreateDCFromHandle(dc); mem=src.CreateCompatibleDC()
    bmp=win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(src,r-l,b-t)
    old=mem.SelectObject(bmp)
    assert user.PrintWindow(handle,mem.GetSafeHdc(),2)
    im=Image.frombytes("RGB",(r-l,b-t),bmp.GetBitmapBits(True),"raw","BGRX")
    mem.SelectObject(old); gui.DeleteObject(bmp.GetHandle());mem.DeleteDC();gui.ReleaseDC(handle,dc)
    im.save(run/(tag+".png"))
    return im

def statuses():
    try:
        lines=(lib/"tasks.record").read_text(encoding="utf8").splitlines()
        return {int(a[0]):int(a[1]) for line in lines if (a:=line.split()) and a[0].isdigit()}
    except OSError:return {}

def create(text,repeat=1):
    click("＋ 新建任务")
    wait(lambda:find("下载链接"))
    tree("type",text)
    if repeat > 1:
        capture("new-task")
    wait(lambda:(n:=find("开始下载")) and n["enabled"])
    click("开始下载",repeat)

server=http.server.ThreadingHTTPServer(("127.0.0.1",0),Source)
threading.Thread(target=server.serve_forever,daemon=True).start()
try:
    launch()
    physical_dpi=user.GetDpiForWindow(handle)
    wait(child)
    capture("empty")
    fname="十一月下载器验收资料与连续刷新长中文文件名.bin"
    uri=f"http://127.0.0.1:{server.server_port}"
    create(uri+"/"+fname+" | "+digest+"\n"+uri+"/missing.bin",2)
    part=lib/"Downloads"/("1-"+fname+".part")
    wait(lambda:part.exists() and part.stat().st_size>2**20)
    row=wait(lambda:find("1-"+fname,True))
    click(row["name"])
    wait(lambda:find("下载中"))
    frames=[]
    counter_areas=[]
    for i in range(4):
        frames.append(capture("streaming" if i==3 else "refresh-"+str(i)))
        # With counts unchanged, the counter glyphs should be pixel-identical across speed updates.
        r=gui.GetWindowRect(handle)
        names=[n for n in tree() if n["type"]=="ControlType.Text" and n["name"].isdigit()]
        assert len(names)==5
        counter_areas.append([frames[-1].crop((round(n["x"]-r[0])-2,round(n["y"]-r[1])-2,
                            round(n["x"]+n["width"]-r[0])+2,round(n["y"]+n["height"]-r[1])+2)).tobytes() for n in names])
        time.sleep(.43)
    assert all(areas==counter_areas[0] for areas in counter_areas)
    assert any(ImageChops.difference(frames[0],f).getbbox() for f in frames[1:])
    assert any("大小未知" in n["name"] for n in tree())
    click("暂停",2)
    wait(lambda:statuses().get(1)==2)
    partial=part.stat().st_size
    assert 0<partial<len(block)*count
    wait(lambda:(n:=find("恢复")) and n["enabled"])
    click("恢复",2)
    wait(lambda:statuses().get(1)==3,50)
    wait(lambda:statuses().get(2)==4)
    assert len(statuses())==2
    final=part.with_suffix("")
    assert hashlib.sha256(final.read_bytes()).hexdigest()==digest
    wait(lambda:find("1-"+fname+"，已完成"))
    capture("completed")
    click("2-missing.bin，失败");click("详情")
    wait(lambda:any("404" in (n["value"] or n["name"]) for n in tree()))
    capture("error")
    stop()
    for percent in (100,125,150):
        launch(percent/physical_dpi*96/100)
        wait(lambda:find("1-"+fname+"，已完成"))
        capture("default-"+str(percent))
        w=round(690*percent/100); ht=round(480*percent/100)
        gui.SetWindowPos(handle,0,0,0,w,ht,0x16)
        time.sleep(.5)
        tree()
        capture("small-"+str(percent))
        assert find("＋ 新建任务") and find("详情")
        stop()
    # Close during a new active transfer, then check restart state and child lifetime.
    launch()
    create(uri+"/close.bin")
    wait(lambda:statuses().get(3)==1)
    stop()
    launch()
    wait(lambda:find("3-close.bin，已暂停"))
    stop()
    runtime = tempfile.TemporaryDirectory()
    bad=Path(runtime.name)/"no-core"
    shutil.copytree(exe.parent,bad,ignore=shutil.ignore_patterns("core","sources","source.zip","userdata","LICENSES","rollback","library-path.txt","library-continuity.json"))
    launch(binary=bad/exe.name)
    wait(lambda:find("下载服务无法运行",True))
    assert not find("＋ 新建任务")["enabled"]
    capture("startup-error")
    stop()
    evidence.update(partial_bytes=partial,completed_sha256=digest,completed_bytes=final.stat().st_size,
                    host_dpi=physical_dpi,qt_effective_scales=[100,125,150],exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                    restart=statuses(),stable_counter_pixel_regions=5,refresh_frames=4,no_orphan=True,duplicate_create_pause_resume=True)
    (run/"result.json").write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding="utf8")
except Exception:
    if app and app.poll() is None and handle:
        capture("failure")
    (run/"last-tree.json").write_text(json.dumps(ui,ensure_ascii=False,indent=2),encoding="utf8")
    raise
finally:
    if app and app.poll() is None:
        gui.PostMessage(handle,0x10,0,0)
        try: app.wait(10)
        except subprocess.TimeoutExpired: app.terminate(); app.wait()
    inspector.stdin.close();inspector.wait(10);server.shutdown()

    if "runtime" in globals(): runtime.cleanup()
