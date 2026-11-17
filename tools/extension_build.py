import hashlib
import json


def stamp_extension(extension):
    def hashes():
        return {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(extension.iterdir()) if p.is_file() and p.name != "build.json"}

    build = hashlib.sha256(json.dumps(hashes(), sort_keys=True).encode()).hexdigest()
    worker = extension / "worker.js"
    source = worker.read_text(encoding="utf8")
    marker = "const installedBuild = 'DEVELOPMENT'"
    if source.count(marker) != 1:
        raise ValueError("Expected a fresh extension worker")
    worker.write_text(source.replace(marker, f"const installedBuild = '{build}'"), encoding="utf8")
    (extension / "build.json").write_text(json.dumps(dict(build=build, files=hashes())), encoding="utf8")
