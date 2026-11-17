"""Replay a lost HTTP completion update using real TaskService records and payloads."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True, type=pathlib.Path)
    args = parser.parse_args()
    host_exe = args.host.resolve()
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        fixture = subprocess.Popen(
            [sys.executable, str(pathlib.Path(__file__).with_name("gui_fixture.py")),
             "--exe", str(host_exe), "--output-root", str(root)],
            stdout=subprocess.PIPE, encoding="utf-8")
        host = None
        try:
            info = json.loads(fixture.stdout.readline())
            pathlib.Path(info["root"], "speed.json").write_text('{"delay":0.003}')
            library = root / "tasks"
            record = library / "tasks.record"

            def start():
                return subprocess.Popen([host_exe, "--data-dir", library],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL, encoding="utf-8")

            def request(op, **fields):
                host.stdin.write(json.dumps(dict(op=op, **fields)) + "\n")
                host.stdin.flush()
                result = json.loads(host.stdout.readline())
                assert result["ok"], result
                return result

            def stop():
                request("shutdown")
                assert host.wait(15) == 0

            host = start()
            request("create", url=info["http"] + "/slow.bin")
            running = record.read_bytes()
            assert b"\n1 1 " in running
            deadline = time.monotonic() + 45
            while True:
                task = request("list")["tasks"][0]
                if task["status"] == "completed":
                    break
                assert time.monotonic() < deadline and task["status"] != "failed", task
                time.sleep(.1)
            final = pathlib.Path(task["path"])
            stop()
            record.write_bytes(running)
            for _ in range(2):
                host = start()
                task = request("list")["tasks"][0]
                assert task["status"] == "completed" and task["digest"] == info["sha256"], task
                stop()
            # Identical content in a different physical file is still unowned.
            moved = final.with_suffix(".original")
            final.rename(moved)
            final.write_bytes(moved.read_bytes())
            record.write_bytes(running)
            host = start()
            assert request("list")["tasks"][0]["status"] == "failed"
            assert hashlib.sha256(final.read_bytes()).hexdigest() == info["sha256"]
            stop()
        finally:
            for process in (host, fixture):
                if process and process.poll() is None:
                    process.terminate()
                    process.wait(10)
    print("Published HTTP recovery and replacement protection passed")


if __name__ == "__main__":
    main()
