"""Independent disk verification of GUI task records; does not drive the UI."""
import hashlib,json,pathlib,shlex,sys
sys.stdout.reconfigure(encoding='utf-8')
fixture=json.loads(pathlib.Path(sys.argv[1]).read_text(encoding='utf-8'))
record=pathlib.Path(fixture['data_dir'])/'tasks.record'
raw=record.read_bytes();prefix=b'HBT-CHECKPOINT-1\n'
assert raw.startswith(prefix) and raw[-73:-65]==b'\nSHA256 '
payload=raw[len(prefix):-73]
assert hashlib.sha256(payload).hexdigest()==raw[-65:-1].decode()
lines=payload.decode('utf-8').splitlines();head=lines[0].split()
assert head[0] in ('GUI1','GUI2') and len(lines)-1==int(head[2])
results=[]
for line in lines[1:]:
    ident,status,received,total,url,path,expected,digest,error=shlex.split(line)
    t=dict(id=int(ident),status=int(status),path=path,error=error)
    if status=='3':
        p=pathlib.Path(path);h=hashlib.sha256(p.read_bytes()).hexdigest()
        assert p.stat().st_size==int(total) and h==digest
        if expected:assert h==expected
        if url.startswith(fixture['http']):assert h==fixture['sha256']
        t['verified_sha256']=h
    elif status=='4':assert not pathlib.Path(path).exists(), 'failed task has a committed output'
    results.append(t)
assert any(t['status']==3 for t in results),'No completed files yet; nothing verified'
out=pathlib.Path(fixture['root'])/'independent-verification.json'
out.write_text(json.dumps(dict(all_passed=True,tasks=results),indent=2),encoding='utf-8')
print('PASS: '+str(sum(t['status']==3 for t in results))+' 个完成文件逐一哈希核验；记录：'+str(out))
