import sys,subprocess,json,time,pathlib
sys.stdin.reconfigure(encoding='utf8');sys.stdout.reconfigure(encoding='utf8')
log=(pathlib.Path(sys.argv[2])/'proxy.log').open('w',encoding='utf8')
p=subprocess.Popen([sys.argv[1],'--data-dir',sys.argv[2]],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=sys.stderr,text=True,encoding='utf8')
delayed=False
for line in sys.stdin:
 log.write(str(time.monotonic())+' IN '+line);log.flush()
 p.stdin.write(line);p.stdin.flush();response=p.stdout.readline();request=json.loads(line)
 if request.get('op')=='create' and str(request.get('requestId','')).startswith('external-') and not delayed:
  delayed=True;time.sleep(6)
 log.write(str(time.monotonic())+' OUT '+response);log.flush();sys.stdout.write(response);sys.stdout.flush()
 if request['op']=='shutdown':break
p.wait(timeout=10)
