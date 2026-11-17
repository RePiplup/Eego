from fixture_helpers import *

try:
 scenario('mixed',['http','bt','http'],0)
 scenario('dual-bt',['bt','bt','bt'],10)
 scenario('dual-http',['http','http','http'],20)
 h=Host(RUN/'invalid-restart')
 for value in [0,-1,9,16,17,True,None,2.0,'2',{},[]]:h.cmd('settings',ok=False,maxActiveTasks=value)
 h.cmd('settings',ok=False,maxActiveTasks='2',extra={'maxActiveTasks':2})
 assert h.cmd('list')['maxActiveTasks']==1
 h.cmd('settings',maxActiveTasks=8);h.p.kill();h.p.wait(10);h.err.close();h=Host(RUN/'invalid-restart');assert h.cmd('list')['maxActiveTasks']==8;h.stop()
 note('invalid scalars and nested bypass rejected; abrupt host restart retained ACKed limit')
 note('ALL PASS')
 (RUN/'results.json').write_text(json.dumps(dict(status='PASS',checks=records),indent=2),encoding='utf8')
except Exception:
 (RUN/'failure.txt').write_text(traceback.format_exc(),encoding='utf8');(RUN/'results.json').write_text(json.dumps(dict(status='FAIL',checks=records),indent=2),encoding='utf8');raise
finally:
 (RUN/'http-requests.json').write_text(json.dumps(request_log,indent=2),encoding='utf8')
 for p in children:
  if p.poll() is None:p.terminate();p.wait(20)
 server.shutdown();print('EVIDENCE',RUN,flush=True)
