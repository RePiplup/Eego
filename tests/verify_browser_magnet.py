"""Independent content verification after real browser/GUI acceptance.
Never operates the UI, resumes a task, or modifies downloaded files.
"""
import argparse,hashlib,json,pathlib,shlex

def sealed(path):
    raw=path.read_bytes();prefix=b'HBT-CHECKPOINT-1\n'
    assert raw.startswith(prefix) and raw[-73:-65]==b'\nSHA256 '
    payload=raw[len(prefix):-73]
    assert hashlib.sha256(payload).hexdigest()==raw[-65:-1].decode()
    return payload

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1048576),b''):h.update(chunk)
    return h.hexdigest()

def main():
    p=argparse.ArgumentParser();p.add_argument('fixture',type=pathlib.Path)
    p.add_argument('--bundle',type=pathlib.Path,required=True)
    p.add_argument('--task-id',type=int,required=True);a=p.parse_args()
    fixture=json.loads(a.fixture.read_text(encoding='utf-8'))
    record=a.bundle/'userdata/tasks.record'
    tasks=[shlex.split(s) for s in sealed(record).decode().splitlines()[1:]]
    task=next(t for t in tasks if int(t[0])==a.task_id)
    assert task[1]=='3','GUI task is not complete'
    assert task[4]==fixture['magnet'],'Wrong Magnet task'
    folder=pathlib.Path(task[5]);manifest=sealed(folder/'files.record')
    assert hashlib.sha256(manifest).hexdigest()==task[7],'Task manifest digest mismatch'
    expected=fixture['expected'];actual={}
    for name,wanted in expected.items():
        file=folder/'files'/name;h=digest(file);assert h==wanted,name
        actual[name]={'bytes':file.stat().st_size,'sha256':h}
    found={f.relative_to(folder/'files').as_posix() for f in (folder/'files').rglob('*') if f.is_file()}
    assert found==set(expected),'Unexpected or missing payload files'
    assert sum(x['bytes'] for x in actual.values())==int(task[3])
    assert task[2]==task[3]
    report={'passed':True,'task_id':a.task_id,'task_status':'complete',
            'folder':str(folder),'magnet':task[4],'files':actual,
            'manifest_sha256':task[7],
            'gui_executable_sha256':digest(a.bundle/'bin/handmadebt.exe')}
    output=a.fixture.parent/('verified-task-'+str(a.task_id)+'.json')
    output.write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
    print('HASH PASS: '+str(output))

if __name__=='__main__':main()
