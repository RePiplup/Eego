// Exercise the installed Eego runtime through a separate Edge profile and native host name.
const assert = require('node:assert/strict');
const cp = require('node:child_process');
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const {chromium} = require('C:/Users/12534/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const root = path.resolve(__dirname, '../..');
const installed = path.join(root, 'dist/Eego');
const run = fs.mkdtempSync(path.join(root, 'artifacts/test-runs/eego-capture-'));
const ext = path.join(run, 'browser/extension');
const name = 'org.handmadebt.test_' + path.basename(run).slice(-6).toLowerCase();
const key = 'HKCU\\Software\\Microsoft\\Edge\\NativeMessagingHosts\\' + name;
const delay = ms => new Promise(r => setTimeout(r, ms));
async function until(check) {
  for (let i = 0; i < 200; i++) { if (await check()) return; await delay(100); }
  throw Error('Timed out waiting for ' + check);
}
async function main() {
  for (const file of fs.readdirSync(installed)) {
    if (/\.(exe|dll)$/i.test(file) || ['browser','core','platforms','qml','ui-text.json'].includes(file))
      fs.cpSync(path.join(installed,file),path.join(run,file),{recursive:true});
  }
  const host = path.join(run,'browser/native-host.json');
  const manifest = JSON.parse(fs.readFileSync(host));
  manifest.name = name;
  manifest.path = path.join(run,'eego_browser_host.exe');
  fs.writeFileSync(host, JSON.stringify(manifest));
  const worker = path.join(ext,'worker.js');
  fs.writeFileSync(worker,fs.readFileSync(worker,'utf8').replace("'org.handmadebt.bridge'",JSON.stringify(name)));
  const bytes = Buffer.alloc(512*1024, 71);
  const filename = '附件-5.zip';
  const archivePath = '/' + encodeURIComponent(filename);
  const server = http.createServer((req,res) => {
    if (req.url === '/') {
      res.writeHead(200,{'Content-Type':'text/html'});
      res.end('<a href="'+archivePath+'">Download</a>');
      return;
    }
    res.writeHead(200,{'Content-Type':'application/octet-stream','Content-Length':bytes.length,
      'Content-Disposition':'attachment'});
    let at = 0;
    const timer = setInterval(() => {
      res.write(bytes.subarray(at,at+8192)); at += 8192;
      if (at >= bytes.length) res.end();
    },25);
    res.on('close',() => clearInterval(timer));
  });
  let context, registered = false;
  try {
    cp.execFileSync('reg',['add',key,'/ve','/t','REG_SZ','/d',host,'/f']); registered = true;
    await new Promise(resolve => server.listen(0,'127.0.0.1',resolve));
    context = await chromium.launchPersistentContext(path.join(run,'profile'),{channel:'msedge',headless:true,
      chromiumSandbox:true,ignoreDefaultArgs:['--disable-extensions'],args:['--load-extension='+ext,'--disable-extensions-except='+ext]});
    const sw = context.serviceWorkers()[0] || await context.waitForEvent('serviceworker',{timeout:15000});
    const cdp = await context.newCDPSession(context.pages()[0]);
    await cdp.send('Browser.setDownloadBehavior',{behavior:'allow',downloadPath:path.join(run,'browser-downloads')});
    await sw.evaluate(() => chrome.storage.local.set({capture:true,captureSince:Date.now()}));
    const base = 'http://127.0.0.1:'+server.address().port;
    const page = context.pages()[0];
    await page.goto(base);
    await page.getByRole('link',{name:'Download'}).click();
    await until(async () => {
      const s = await sw.evaluate(() => chrome.storage.local.get('lastResult'));
      if (!s.lastResult) return false;
      assert.match(s.lastResult,/已由 Eego 接管/); return true;
    });
    const [item] = await sw.evaluate(url => chrome.downloads.search({url}),base+archivePath);
    assert.equal(item.state,'interrupted');
    const output = path.join(run,'Downloads',filename);
    await until(() => fs.existsSync(output) && fs.readFileSync(output).equals(bytes));
    await until(() => !fs.existsSync(output+'.hbt-lock'));
    console.log('PASS Edge download handed off to Eego, browser stopped, native bytes verified.');
    cp.execFileSync('reg',['delete',key,'/f']); registered = false;
    const fallbackId = await sw.evaluate(url => chrome.downloads.download({url}),base+'/offline.bin');
    await until(async () => {
      const [entry] = await sw.evaluate(id => chrome.downloads.search({id}),fallbackId);
      return entry?.state === 'complete' && fs.readFileSync(entry.filename).equals(bytes);
    });
    const fallback = await sw.evaluate(() => chrome.storage.local.get('lastResult'));
    assert.match(fallback.lastResult,/已恢复浏览器下载/);
    console.log('PASS native host unavailable, Edge resumed and saved identical bytes.');
  } finally {
    if (context) await context.close();
    server.closeAllConnections(); server.close();
    if (registered) cp.execFileSync('reg',['delete',key,'/f']);
    cp.execFileSync('powershell.exe',['-NoProfile','-Command',
      "Get-Process HandmadeBTQuick -ErrorAction SilentlyContinue | Where-Object Path -eq '"+path.join(run,'HandmadeBTQuick.exe').replaceAll("'","''")+"' | Stop-Process; exit 0"]);
  }
}
main().catch(e => {console.error(e);process.exitCode=1;});
