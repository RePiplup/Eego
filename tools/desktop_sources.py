"""Collect the source inputs used by the desktop build without reading a release."""
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile
import os


def write_sources(source: Path, target: Path):
    trees = ("src", "apps", "browser", "tests", "LICENSES", "provenance",
             "audit/upstream/aria2", "audit/upstream/libtorrent", "dependencies/boost_1_82_0")
    with ZipFile(target, "x", ZIP_DEFLATED, compresslevel=2) as archive:
        for name in ("CMakeLists.txt", "README.md", "COPYING", "Update-Edge.cmd"):
            archive.write(source / name, name)
        for name in trees:
            for directory, children, files in os.walk(source / name):
                children[:] = [child for child in children if child not in {".git", "__pycache__"}]
                for filename in files:
                    path = Path(directory) / filename
                    if filename != ".git" and path.suffix.lower() not in {".exe", ".dll", ".pyc"}:
                        archive.write(path, path.relative_to(source))
        for folder in ("packaging", "tools"):
            for path in (source / folder).iterdir():
                if path.is_file() and path.suffix in {".ps1", ".py", ".cmd", ".txt", ".iss"}:
                    archive.write(path, path.relative_to(source))
