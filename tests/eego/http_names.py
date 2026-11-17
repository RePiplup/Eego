from pathlib import Path
from urllib.parse import quote

from fixture_helpers import Host, RUN, byid, server, sources


host = Host(RUN / "library")
destination = RUN / "downloads"
destination.mkdir()
payload = b"test archive"
base = f"http://127.0.0.1:{server.server_port}/"


def download(leaf, expected, filename=""):
    sources[leaf] = payload
    task_id = host.cmd("create", url=base + leaf + "?token=123", filename=filename,
                       destination=str(destination))["id"]
    done = byid(host.until(lambda rows: byid(rows, task_id)["status"] == "completed"), task_id)
    path = Path(done["path"])
    assert path == destination / expected and path.read_bytes() == payload, path
    assert not Path(str(path) + ".hbt-lock").exists()


try:
    filename = "附件-5.zip"
    download(quote(filename), filename)
    download("attachment", "附件-5 (2).zip", filename)
    download("100%2525+data.zip", "100%25+data.zip")
    download("endpoint", "literal%20name.zip", "literal%20name.zip")
    download("bad%2Fname.zip", "bad_name.zip")
    download("CON.txt", "_CON.txt")
    sentinel = destination / "other.zip.part"
    sentinel.write_bytes(b"existing")
    download("other.zip", "other (2).zip")
    assert sentinel.read_bytes() == b"existing"
    print("PASS Unicode names, collision handling, literal escapes, lock cleanup")
finally:
    host.stop()
    server.shutdown()
