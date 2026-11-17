"""Read only the browser download tables; report matches to current native tasks."""
import sqlite3, pathlib, re, json, datetime
root = pathlib.Path(__file__).resolve().parents[1]
history = pathlib.Path('C:/Users/12534/AppData/Local/Microsoft/Edge/User Data/Default/History')
db = sqlite3.connect(history.as_uri() + '?mode=ro&immutable=1', uri=True)
rows = db.execute('SELECT d.id, d.start_time, d.state, u.url FROM downloads d JOIN downloads_url_chains u ON d.id=u.id').fetchall()
urls = {}
for ident, stamp, state, url in rows:
    when = (datetime.datetime(1601,1,1,tzinfo=datetime.timezone.utc) + datetime.timedelta(microseconds=stamp)).isoformat()
    urls.setdefault(url, []).append(dict(browser_id=ident, started=when, state=state))
record = root/'dist/HandmadeBT-0.8.2-lock-cleanup-win64/userdata/tasks.record'
result = []
for line in record.read_text(encoding='utf8').splitlines():
    m = re.match(r'^(\d+) (\d+) (\d+) (\d+) "([^"\n]*)"', line)
    if m:
        ident, status, received, total, url = m.groups()
        result.append(dict(task_id=int(ident), status=int(status), received=int(received), matches=urls.get(url, [])))
out=root/'backups/capture-history-20261029/download-match-audit.json'
out.write_text(json.dumps(result,indent=2),encoding='utf8')
print(json.dumps(result))
