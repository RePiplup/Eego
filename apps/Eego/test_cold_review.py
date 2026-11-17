from pathlib import Path
import subprocess,time,json,psutil
R=Path(__file__).resolve().parents[2];E=R/'artifacts/test-runs/eego-next'/('cold-'+str(time.time_ns()));E.mkdir(parents=True,exist_ok=True)
exe=R/'dist/Eego/HandmadeBTQuick.exe';cli=R/'dist/Eego/core/core_cli.exe'
torrents=[]
for i in range(3):
 source=E/f'source{i}';source.mkdir();(source/'sample.bin').write_bytes(bytes([i])*123)
 t=E/f'{i}.torrent';subprocess.run([cli,'make-torrent-dir',source,t],check=True,capture_output=True);torrents.append(t)
library=E/'library';children=[subprocess.Popen([exe,'--data-dir',library,'--open',torrents[i%3]]) for i in range(9)]
deadline=time.time()+30
while time.time()<deadline:
 alive=[p for p in children if p.poll() is None]
 if len(alive)==1 and (library/'tasks.record').exists():break
 time.sleep(.3)
time.sleep(1)
codes=[p.poll() for p in children];assert codes.count(None)==1 and all(c in (None,0) for c in codes),codes
lines=(library/'tasks.record').read_text(encoding='utf8').splitlines();assert lines[1].split()[-1]=='3',lines
rows=[l for l in lines if l and l[0].isdigit()];assert len(rows)==3 and len(set(l.split('magnet:?xt=urn:btih:')[1][:40] for l in rows))==3
result={'passed':True,'codes':codes,'survivor':alive[0].pid,'library':str(library),'task_count':3,'process':psutil.Process(alive[0].pid).exe()};(E/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
