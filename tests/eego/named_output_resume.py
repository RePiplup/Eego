"""Resume a partially downloaded single file across an old-layout library upgrade."""
import hashlib
import json
from pathlib import Path

from fixture_helpers import Host, RUN, byid, children, fixture, server


def save_inventory(path, body):
    data = body.encode()
    path.write_bytes(b"HBT-CHECKPOINT-1\n" + data + b"\nSHA256 " +
                     hashlib.sha256(data).hexdigest().encode() + b"\n")


library = RUN / "resume"
host = Host(library)
try:
    uri, expected = fixture("bt", 1)
    task_id = host.cmd("create", url=uri)["id"]
    host.until(lambda tasks: byid(tasks, task_id)["status"] == "choosing")
    host.cmd("select", id=task_id, indexes=[0])
    host.until(lambda tasks: 0 < byid(tasks, task_id)["bytes"] < 4 * 1024 * 1024)
    host.cmd("pause", id=task_id)
    paused = byid(host.until(lambda tasks: byid(tasks, task_id)["status"] == "paused"), task_id)
    host.stop()
    public = Path(paused["path"])
    assert public.is_file() and public.resolve().is_relative_to(RUN.resolve())
    identity = public.stat().st_ino
    legacy = public.parent / f"{task_id}-magnet" / "files" / public.name
    legacy.parent.mkdir(parents=True)
    public.rename(legacy)
    inventory = library / f"ownership-{task_id}.record"
    body = inventory.read_bytes().partition(b"\n")[2].rpartition(b"\nSHA256 ")[0].decode()
    old = json.dumps(str(public), ensure_ascii=False)
    assert old in body
    save_inventory(inventory, body.replace(old, json.dumps(str(legacy), ensure_ascii=False)))
    (library / "magnet-state" / str(task_id) / "output.record").unlink()
    host = Host(library)
    migrated = byid(host.list(), task_id)
    assert migrated["status"] == "paused" and migrated["bytes"] == paused["bytes"] > 0
    assert Path(migrated["path"]) == public and public.stat().st_ino == identity
    assert not legacy.parent.parent.exists()
    host.cmd("resume", id=task_id)
    done = byid(host.until(lambda tasks: byid(tasks, task_id)["status"] == "completed"), task_id)
    assert Path(done["files"][0]["diskPath"]) == public
    assert hashlib.sha256(public.read_bytes()).hexdigest() == expected
    print("PASS: legacy partial renamed, progress and file ID preserved, resumed to matching SHA-256")
finally:
    if host.p.poll() is None:
        host.stop()
    for process in children:
        if process.poll() is None:
            process.terminate()
            process.wait(20)
    server.shutdown()
