"""Drive the packaged Win32 window on a private, never-activated Windows desktop."""
import ctypes
import hashlib
import http.server
import json
import pathlib
import subprocess
import sys
import threading
import time

import win32gui as gui
import win32process
import win32service
import win32event
import win32ui
from PIL import Image

root = pathlib.Path(__file__).resolve().parents[1]
exe = root / "dist/HandmadeBT/HandmadeBT.exe"
user = ctypes.windll.user32
if len(sys.argv) == 1:
    run = root / "artifacts/test-runs/ui-098" / str(time.time_ns())
    run.mkdir(parents=True)
    name = "HandmadeBT-Test-" + run.name
    desk = win32service.CreateDesktop(name, 0, 0x1ff, None)
    startup = win32process.STARTUPINFO()
    startup.lpDesktop = name
    cmd = subprocess.list2cmdline([sys.executable, __file__, str(run)])
    proc, thread, pid, tid = win32process.CreateProcess(None, cmd, None, None, False,
                                                        0x08000000, None, str(root), startup)
    win32event.WaitForSingleObject(proc, -1)
    print(run)
    sys.exit(win32process.GetExitCodeProcess(proc))

run = pathlib.Path(sys.argv[1])
sys.stderr = (run / "stderr.txt").open("w", encoding="utf8")
user.SetProcessDPIAware()
block = bytes(range(256)) * 256
count = 128
expected = hashlib.sha256(block * count).hexdigest()
filename = "下载界面验收样本二〇二六年十一月长文件名称完整性检查.bin"

class Source(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.endswith("missing.bin"):
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()
        try:
            for _ in range(count):
                self.wfile.write(b"10000\r\n" + block + b"\r\n")
                self.wfile.flush()
                time.sleep(.035)
            self.wfile.write(b"0\r\n\r\n")
        except OSError:
            pass

    def log_message(self, *args):
        pass

def wait(check):
    until = time.monotonic() + 25
    while time.monotonic() < until:
        value = check()
        if value:
            return value
        time.sleep(.06)
    raise TimeoutError(check)

def screenshot(name):
    time.sleep(.35)
    gui.RedrawWindow(h,None,None,0x185)
    l,t,r,b = gui.GetWindowRect(h)
    dc = gui.GetWindowDC(h)
    src = win32ui.CreateDCFromHandle(dc)
    mem = src.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap()
    bmp.CreateCompatibleBitmap(src,r-l,b-t)
    prev = mem.SelectObject(bmp)
    assert user.PrintWindow(h,mem.GetSafeHdc(),2)
    Image.frombytes("RGB",(r-l,b-t),bmp.GetBitmapBits(True),"raw","BGRX").save(run / (name+".png"))
    mem.SelectObject(prev)
    gui.DeleteObject(bmp.GetHandle())
    mem.DeleteDC()
    gui.ReleaseDC(h,dc)

def window():
    windows = []
    try:
        gui.EnumWindows(lambda w,_: windows.append(w) if win32process.GetWindowThreadProcessId(w)[1]==app.pid and user.GetDlgItem(w,107) else None,None)
    except gui.error as error:
        if error.winerror:
            raise
    return next(iter(windows),0)

def control(i):
    return gui.GetDlgItem(h,i)

def enabled(i):
    return bool(user.IsWindowEnabled(control(i)))

def value(i):
    buf=ctypes.create_unicode_buffer(65536)
    user.SendMessageW(control(i),13,len(buf),buf)
    return buf.value

def click(i):
    assert enabled(i), i
    gui.SendMessage(control(i),0xf5,0,0)

def select(row):
    table=control(106)
    # LVM_GETITEMRECT takes a process-local pointer. Use keyboard navigation instead.
    gui.SendMessage(table,0x100,0x24,0)
    for _ in range(row):
        gui.SendMessage(table,0x100,0x28,0)

def verify_width():
    assert not gui.GetWindowLong(control(106),-16) & 0x100000
    return [gui.SendMessage(control(106),0x101d,i,0) for i in range(4)]

server=http.server.ThreadingHTTPServer(("127.0.0.1",0),Source)
threading.Thread(target=server.serve_forever,daemon=True).start()
base=f"http://127.0.0.1:{server.server_port}"
lib=run/"library"
app=subprocess.Popen([exe,"--data-dir",lib])
try:
    h=wait(window)
    wait(lambda: "选择任务" in value(107))
    assert not any(enabled(i) for i in (103,104,105,109,110,114,115,116))
    assert "还没有" in value(122)
    default_cols=verify_width()
    screenshot("empty")
    gui.SendMessage(control(101),12,0,base+"/"+filename+" | "+expected+"\r\n"+base+"/missing.bin")
    wait(lambda: enabled(103))
    click(103)
    part=lib/"Downloads"/("1-"+filename+".part")
    wait(lambda: part.exists() and part.stat().st_size>3*1024*1024)
    select(0)
    wait(lambda: enabled(104) and "重下" in value(107))
    assert not enabled(105) and "未知大小" in value(107) and "%" not in value(107)
    screenshot("streaming")
    click(104)
    wait(lambda: enabled(105))
    assert "重下" in value(107)
    partial=part.stat().st_size
    assert 0<partial<len(block)*count and not enabled(104)
    click(105)
    wait(lambda: enabled(116))
    actual=hashlib.sha256(part.with_suffix("").read_bytes()).hexdigest()
    assert actual==expected and expected in value(107) and not enabled(105)
    assert not enabled(104) and not enabled(115)
    screenshot("completed")
    select(1)
    wait(lambda: enabled(115) and "404" in value(107))
    dpi=user.GetDpiForWindow(h)
    gui.SetWindowPos(h,0,0,0,740*dpi//96,600*dpi//96,0x16)
    small_cols=verify_width()
    screenshot("error-small")
    result=dict(exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),partial_bytes=partial,
                expected_sha256=expected,actual_sha256=actual,completed_bytes=part.with_suffix("").stat().st_size,
                system_dpi=dpi,default_columns=default_cols,small_columns=small_cols,error=value(107))
    gui.PostMessage(h,0x10,0,0)
    assert app.wait(15)==0
    (run/"result.json").write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding="utf8")
finally:
    server.shutdown()
    if app.poll() is None:
        app.terminate()
        app.wait()

