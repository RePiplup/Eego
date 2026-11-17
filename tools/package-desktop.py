"""Self-contained native Windows portable package, with matching source archive."""
import hashlib,json,pathlib,shutil,subprocess,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
from extension_build import stamp_extension
NAME='HandmadeBT'
import tempfile
import argparse
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=pathlib.Path, default=ROOT/'dist/HandmadeBT.zip')
archive = parser.parse_args().output.resolve()
if archive.suffix.lower() != '.zip':
    parser.error('--output must be a ZIP file')
staging=tempfile.TemporaryDirectory()
OUT=pathlib.Path(staging.name)/NAME
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
if OUT.exists():raise SystemExit('Refusing to overwrite existing desktop package')
import os
build = pathlib.Path(staging.name) / 'build'
cmake = ROOT / 'tools/python-tools/cmake/data/bin/cmake.exe'
compiler = ROOT / 'tools/llvm-mingw-20261020-ucrt-x86_64/bin'
ninja = ROOT / 'tools/python-tools/ninja-1.11.1.3.data/scripts'
env = os.environ.copy()
env['PATH'] = os.pathsep.join(map(str, [compiler, ninja, env['PATH']]))
subprocess.run([cmake, '-S', ROOT, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++'], env=env, check=True)
subprocess.run([cmake, '--build', build, '--target', 'handmadebt_desktop', 'desktop_bridge',
                '-j', '8'], env=env, check=True)
VERSION=subprocess.check_output(['powershell.exe','-NoProfile','-Command',
    "(Get-Item -LiteralPath 'handmadebt_desktop.exe').VersionInfo.FileVersion"],cwd=build,text=True).strip()
OUT.mkdir()
shutil.copy2(build/'handmadebt_desktop.exe',OUT/'HandmadeBT.exe')
shutil.copy2(build/'desktop_bridge.exe',OUT/'handmadebt_host.exe')
shutil.copytree(ROOT/'browser',OUT/'browser')
extension=OUT/'browser'/'extension'
stamp_extension(extension)

shutil.copy2(ROOT/'Update-Edge.cmd',OUT/'Update-Edge.cmd')
for name in ['libc++.dll' ,'libunwind.dll']:shutil.copy2(compiler/name,OUT/name)
shutil.copy2(ROOT/'COPYING',OUT/'COPYING')
shutil.copytree(ROOT/'LICENSES',OUT/'LICENSES')
(OUT/'使用说明.txt').write_text(f'''HandmadeBT {VERSION} 便携桌面版（Windows 10/11，64位）

把整个压缩包解压到可写目录，双击 HandmadeBT.exe 即可使用。
不需要安装 Python、Node、浏览器插件或启动任何服务器。
请保留 EXE 旁的两个 DLL；不要只复制 EXE。

1. 在上方粘贴下载链接（每行一个），选择保存目录，点击“添加下载”。
2. 选中任务，点击“暂停选中”或“恢复选中”。
3. 关闭软件后再次双击 EXE，未完成任务会保留并暂停，点击恢复即可继续。
4. 磁力链接取得文件清单后自动打开选择窗口。勾选文件并确认后开始下载；稍后可从“选择 / 查看文件”重新打开。
5. 完成后可双击任务打开文件，或点击“打开保存目录”。

以后升级请先退出软件，再将新版解压覆盖同一 HandmadeBT 文件夹。个人任务库位于程序旁的 userdata。默认保存到当前库的 Downloads。
任务库菜单可打开其他旧库。接回旧库后请保留其 userdata 和已有下载路径。
便携版无需管理员权限。请先解压，不要在压缩包预览里直接运行。
下载记录使用绝对路径；有未完成任务时请不要移动便携目录。

支持 HTTP、HTTPS 和 BT v1 Magnet。无人做种的资源不保证下载。
不支持 Range 的 HTTP 源恢复可能从头开始；已开始的磁力任务不能改变文件选择。
此开发版未证明独立 DHT-only 可用，尚未覆盖断电或所有磁盘故障。
当前按队列逐个运行任务。

Edge 下载助手：双击 Update-Edge.cmd 同步本机通信程序和扩展文件。以后覆盖升级时，已启用的助手会自动重新加载。首次使用按脚本显示的位置加载解压缩扩展。

COPYING / LICENSES 为许可证；source.zip 为对应源码，普通使用无需解压。
''',encoding='utf-8-sig')
from desktop_sources import write_sources
write_sources(ROOT, OUT/'source.zip')
manifest={'version':VERSION+'-desktop','entrypoint':'HandmadeBT.exe','architecture':'native Win32; shared download_core linked in-process','runtime':'bundled libc++ and libunwind; Windows 10/11 system libraries','files':{}}
for p in sorted(OUT.rglob('*')):
    if p.is_file():manifest['files'][p.relative_to(OUT).as_posix()]=digest(p)
(OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
draft=OUT.parent/'release.zip'
with zipfile.ZipFile(draft,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in sorted(OUT.rglob('*')):
        if p.is_file():z.write(p,NAME+'/'+p.relative_to(OUT).as_posix())
shutil.copyfile(draft,archive.with_suffix('.zip.new'))
archive.with_suffix('.zip.new').replace(archive)
archive.with_suffix('.zip.sha256').write_text(digest(archive)+'  '+archive.name+'\n',encoding='utf-8')
print(json.dumps(dict(archive=str(archive),size=archive.stat().st_size,sha256=digest(archive)),ensure_ascii=False))
