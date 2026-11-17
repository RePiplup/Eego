"""Open both installed desktops using isolated libraries and close their main windows."""
from pathlib import Path
import subprocess
import tempfile
import time

import psutil
import win32con
import win32gui
import win32process


ROOT = Path(__file__).resolve().parents[1]


def check(entry, ui, library):
    library.mkdir()
    proc = subprocess.Popen([entry, "--data-dir", library], cwd=ROOT)
    owner = psutil.Process(proc.pid)
    processes = []
    window = None
    try:
        deadline = time.monotonic() + 35
        while time.monotonic() < deadline and proc.poll() is None:
            processes = [owner, *owner.children(recursive=True)]
            pids = {p.pid for p in processes if Path(p.exe()).resolve() == ui}
            windows = []

            def collect(hwnd, unused):
                if win32process.GetWindowThreadProcessId(hwnd)[1] not in pids:
                    return
                title = win32gui.GetWindowText(hwnd)
                if win32gui.IsWindowVisible(hwnd) and win32gui.GetClassName(hwnd) != "#32770" and title:
                    windows.append((hwnd, title))

            win32gui.EnumWindows(collect, None)
            if windows:
                window, title = windows[0]
                time.sleep(1)
                win32gui.SendMessageTimeout(window, win32con.WM_NULL, 0, 0, win32con.SMTO_ABORTIFHUNG, 5000)
                print(f"{entry.name}: {title} (responsive)", flush=True)
                win32gui.PostMessage(window, win32con.WM_CLOSE, 0, 0)
                if proc.wait(timeout=25) != 0:
                    raise RuntimeError(f"{entry.name} failed to exit normally")
                return
            time.sleep(.2)
        raise RuntimeError(f"{entry.name} did not open a main window; exit={proc.poll()}")
    finally:
        for p in reversed(processes):
            if p.is_running():
                try:
                    p.terminate()
                    p.wait(5)
                except psutil.NoSuchProcess:
                    pass
        if proc.poll() is None:
            proc.terminate()
            proc.wait()


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as directory:
        base = Path(directory)
        legacy = ROOT / "dist/HandmadeBT/HandmadeBT.exe"
        check(legacy, legacy, base/"legacy")
        check(ROOT/"Eego.exe", ROOT/"dist/Eego/HandmadeBTQuick.exe", base/"eego")
