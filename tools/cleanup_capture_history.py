"""One-time, audited task-list cleanup. Never removes or writes download files."""
import ctypes, hashlib, json, os, pathlib, re
root=pathlib.Path(__file__).resolve().parents[1]
data=root/'dist/HandmadeBT-0.8.2-lock-cleanup-win64/userdata'
backup=root/'backups/capture-history-20261029'
k=ctypes.WinDLL('kernel32',use_last_error=True)
k.CreateFileW.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p]
k.CreateFileW.restype=ctypes.c_void_p
k.CloseHandle.argtypes=[ctypes.c_void_p]
handle=k.CreateFileW(str(data/'ui.lock'),0xC0000000,0,None,3,0x80,None)
if handle==ctypes.c_void_p(-1).value: raise RuntimeError('GUI is running or lock unavailable; no changes made')
def sha(value): return hashlib.sha256(value).hexdigest()
def inventory():
    result={}
    for p in sorted(data.rglob('*')):
        if not p.is_file() or p.name in ('tasks.record','ui.lock'): continue
        with p.open('rb') as f: digest=hashlib.file_digest(f,'sha256').hexdigest()
        stat=p.stat()
        result[str(p.relative_to(data))]={'size':stat.st_size,'mtime_ns':stat.st_mtime_ns,'sha256':digest}
    return result
try:
    original=(data/'tasks.record').read_bytes()
    prefix=b'HBT-CHECKPOINT-1\n'
    assert original.startswith(prefix) and original[-73:-65]==b'\nSHA256 '
    payload=original[len(prefix):-73]
    assert sha(payload)==original[-65:-1].decode()
    lines=payload.decode('utf8').splitlines()
    magic,next_id,count=lines[0].split()
    tasks=lines[1:]
    assert magic=='GUI2' and len(tasks)==int(count)
    audit=json.loads((backup/'download-match-audit.json').read_text())
    remove={x['task_id'] for x in audit if x['matches'] and all(m['started']<'2026-10-29' for m in x['matches'])}
    assert remove==set(range(33,250)), 'Unexpected audit; inspect manually'
    ids={int(x.split(' ',1)[0]) for x in tasks}
    assert remove<=ids
    saved=backup/'tasks.record.before-cleanup'
    assert not saved.exists(), 'Refuse to overwrite original backup'
    saved.write_bytes(original)
    before=inventory()
    (backup/'files-before.json').write_text(json.dumps(before,ensure_ascii=False,indent=2),encoding='utf8')
    kept=[x for x in tasks if int(x.split(' ',1)[0]) not in remove]
    new_payload=(f'GUI2 {next_id} {len(kept)}\n'+''.join(x+'\n' for x in kept)).encode('utf8')
    new_record=prefix+new_payload+b'\nSHA256 '+sha(new_payload).encode()+b'\n'
    temporary=data/'tasks.record.cleanup-tmp'
    with temporary.open('xb') as f: f.write(new_record);f.flush();os.fsync(f.fileno())
    assert (data/'tasks.record').read_bytes()==original
    os.replace(temporary,data/'tasks.record')
    after=inventory()
    assert before==after, 'Unexpected file change; inspect inventory'
    result={'removed_task_count':len(remove),'removed_ids':sorted(remove),'remaining_tasks':len(kept),'next_id':int(next_id),'files_preserved_and_hash_verified':len(before),'bytes_preserved':sum(x['size'] for x in before.values()),'record_backup':str(saved),'downloads_deleted':0}
    (backup/'cleanup-result.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    print(json.dumps({k:v for k,v in result.items() if k!='removed_ids'}))
finally: k.CloseHandle(handle)
