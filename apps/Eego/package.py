"""Assemble a clean Eego portable package from this build and its SDK."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from desktop_sources import write_sources
from extension_build import stamp_extension

SOURCE = ROOT


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def install_browser_files(stage, version):
    extension = stage / "browser/extension"
    shutil.copytree(SOURCE / "browser/extension", extension, dirs_exist_ok=True)
    manifest = extension / "manifest.json"
    config = json.loads(manifest.read_text(encoding="utf8"))
    config.update(version=version, name="Eego 下载助手", description="将浏览器中的文件下载交给 Eego。")
    config.pop("optional_permissions", None)
    config.pop("optional_host_permissions", None)
    manifest.write_text(json.dumps(config, ensure_ascii=False, indent=2), encoding="utf8")
    for name in ("popup.html", "popup.js", "worker.js"):
        path = extension / name
        path.write_text(path.read_text(encoding="utf8").replace("HandmadeBT", "Eego"), encoding="utf8")
    stamp_extension(extension)
    shutil.copy2(SOURCE / "apps/Eego/Browser-Setup.cmd", stage)


def package(build, qt, mingw, output):
    sdk = qt.parents[2]
    with tempfile.TemporaryDirectory(dir=output.parent) as temp:
        stage = Path(temp) / "Eego"
        core = stage / "core"
        core.mkdir(parents=True)
        shutil.copy2(build / "ui/HandmadeBTQuick.exe", stage)
        shutil.copy2(SOURCE / "apps/Eego/ui-text.json", stage)
        for file in ("task_service_host_r3.exe", "core_cli.exe"):
            shutil.copy2(build / "core" / file, core)
        shutil.copy2(build / "core/eego_browser_host.exe", stage)
        for file in ("libc++.dll", "libunwind.dll"):
            runtime = ROOT / "tools/llvm-mingw-20261020-ucrt-x86_64/bin" / file
            shutil.copy2(runtime, core)
            shutil.copy2(runtime, stage)
        env = os.environ.copy()
        env["PATH"] = os.pathsep.join(map(str, [qt/"bin", mingw, env["PATH"]]))
        # Anaconda adds its DLL directory to child processes; Qt must load its SDK DLLs.
        if not ctypes.windll.kernel32.SetDllDirectoryW(None):
            raise ctypes.WinError()
        subprocess.run([qt/"bin/windeployqt.exe", "--release", "--qtpaths", qt/"bin/qtpaths.exe", "--qmldir", SOURCE/"apps/Eego",
                        "--compiler-runtime", "--skip-plugin-types", "qmltooling", "--no-opengl-sw",
                        "--no-system-d3d-compiler", "--no-translations", stage/"HandmadeBTQuick.exe"],
                       env=env, cwd=qt/"bin", check=True)
        version = (build / "ui/release-version.txt").read_text().strip()
        install_browser_files(stage, version)
        origin = (SOURCE / "browser/extension-id.txt").read_text().strip()
        host = dict(name="org.handmadebt.bridge", description="Eego task handoff", type="stdio",
                    path=r"..\eego_browser_host.exe", allowed_origins=[f"chrome-extension://{origin}/"])
        (stage/"browser/native-host.json").write_text(json.dumps(host, indent=2), encoding="utf8")
        licenses = stage / "LICENSES"
        shutil.copytree(SOURCE / "LICENSES", licenses / "core")
        shutil.copy2(SOURCE / "COPYING", stage)
        shutil.copy2(SOURCE / "apps/Eego/assets/lucide-LICENSE.txt", licenses)
        for component in ("gcc", "mingw-w64", "winpthreads"):
            shutil.copytree(mingw.parent/"licenses"/component, licenses/"MinGW"/component)
        sources = stage / "sources"
        sources.mkdir()
        (licenses / "Qt-SBOM").mkdir()
        for module in ("qtbase", "qtdeclarative", "qtsvg"):
            archive = sdk / "sources" / f"{module}-everywhere-src-6.8.3.tar.xz"
            if digest(archive) != Path(str(archive)+".sha256").read_text().split()[0]:
                raise ValueError(f"Source checksum mismatch: {archive}")
            shutil.copy2(archive, sources)
            for sbom in (qt/"sbom").glob(module+"-*"):
                shutil.copy2(sbom, licenses/"Qt-SBOM")
            with tarfile.open(archive, "r|xz") as tar:
                for member in tar:
                    parts = Path(member.name).parts
                    if member.isfile() and len(parts) == 3 and parts[1] == "LICENSES":
                        dest = licenses / module / parts[-1]
                        dest.parent.mkdir(exist_ok=True)
                        dest.write_bytes(tar.extractfile(member).read())
        write_sources(SOURCE, sources / "HandmadeBT-source.zip")
        files = sorted(p for p in stage.rglob("*") if p.is_file())
        hashes = {p.relative_to(stage).as_posix(): digest(p) for p in files}
        manifest = stage/"manifest.json"
        manifest.write_text(json.dumps(dict(version=version, entry="HandmadeBTQuick.exe", files=hashes), indent=2), encoding="utf8")
        draft = Path(temp)/"package.zip"
        with zipfile.ZipFile(draft, "x", zipfile.ZIP_DEFLATED, compresslevel=1) as bundle:
            for path in [*files, manifest]:
                bundle.write(path, path.relative_to(stage.parent))
        with zipfile.ZipFile(draft) as bundle:
            if bundle.testzip() is not None:
                raise ValueError("Package verification failed")
        draft.replace(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ("build", "qt", "mingw", "output"):
        parser.add_argument("--"+arg, required=True, type=Path)
    args = parser.parse_args()
    if args.output.suffix.lower() != ".zip":
        parser.error("--output must be a ZIP file")
    package(args.build.resolve(), args.qt.resolve(), args.mingw.resolve(), args.output.resolve())
