"""A Windows ANSI diagnostic in one saved task must not hide the full list."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


host = Path(__file__).resolve().parents[2] / "dist/Eego/core/task_service_host_r3.exe"
with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    lines = [b"GUI2 3 2"]
    for index, error in [(1, b"native error: \xd5\xd2"), (2, b"")]:
        name = f"file-{index}.bin"
        path = (root / name).as_posix().encode()
        lines.append(f'{index} 4 0 0 "https://example.org/{name}" "'.encode() +
                     path + b'" "" "" "' + error + b'"')
    payload = b"\n".join(lines) + b"\n"
    (root / "tasks.record").write_bytes(b"HBT-CHECKPOINT-1\n" + payload +
        b"\nSHA256 " + hashlib.sha256(payload).hexdigest().encode() + b"\n")
    request = b'{"op":"list","requestId":"list"}\n{"op":"shutdown","requestId":"stop"}\n'
    result = subprocess.run([host, "--data-dir", root], input=request, capture_output=True,
                            check=True, timeout=30)
    response = json.loads(result.stdout.splitlines()[0].decode("utf8"))
    assert response["ok"] and [t["id"] for t in response["tasks"]] == [1, 2]
    assert response["tasks"][0]["error"]["message"].startswith("native error: ")
    print("PASS: two tasks visible with a native Windows diagnostic")
