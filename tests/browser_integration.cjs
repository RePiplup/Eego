// Real Edge extension/native-messaging/download pipeline in a fresh profile.
// No clicks are simulated; interactive UI acceptance is recorded separately.
const fs=require('fs'),path=require('path'),crypto=require('crypto'),http=require('http'),cp=require('child_process');
const {chromium}=require(process.env.HBT_PLAYWRIGHT);
const root=path.resolve(__dirname,'..'), run=path.join(root,'artifacts/test-runs','浏览器 验收 '+Date.now());
fs.mkdirSync(path.join(run,'bin'),{recursive:true});
for(const n of ['handmadebt.exe','handmadebt_host.exe','core_cli.exe','libc++.dll','libunwind.dll'])fs.copyFileSync(path.join(root,n==='core_cli.exe'?'build':'dist/HandmadeBT',n),path.join(run,'bin',n));
fs.cpSync(path.join(root,'browser'),path.join(run,'browser'),{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'), data=Buffer.alloc(2*1024*1024+37);for(let i=0;i<data.length;i++)data[i]=(i*31+i%251)%256;
const tests=[], sleep=ms=>new Promise(r=>setTimeout(r,ms));let context,seed,registered=false;
function ps(args){return cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass',...args],{encoding:'utf8',windowsHide:true});}
function setup(remove=false){ps(['-File',path.join(run,'browser','Register-Browser.ps1'),...(remove?['-Unregister']:[])]);registered=!remove;}
function record(name,extra={}){tests.push({name,...extra});fs.writeFileSync(path.join(run,'results.json'),JSON.stringify({all_passed:false,run,expected_sha256:sha(data),tests},null,2));console.log(name+': PASS');}
async function until(fn,seconds=90){const end=Date.now()+seconds*1000;while(Date.now()<end){const v=await fn();if(v)return v;await sleep(100);}throw Error('Timed out');}
function taskComplete(id){try{return fs.readFileSync(path.join(run,'userdata','tasks.record'),'utf8').split('\n').some(s=>s.startsWith(id+' 3 '));}catch{return false;}}
async function output(id,name){await until(()=>taskComplete(id));const p=path.join(run,'userdata','Downloads',id+'-'+name);if(sha(fs.readFileSync(p))!==sha(data))throw Error('content mismatch');return p;}
const server=http.createServer((req,res)=>{
 if(req.url==='/'){res.setHeader('Content-Type','text/html; charset=utf-8');res.end('<a href="/manual.bin">下载测试文件</a>');return;}
 let start=0,end=data.length-1;const range=/bytes=(\d+)-(\d*)/.exec(req.headers.range||'');if(range){start=+range[1];if(range[2])end=+range[2];}
 res.writeHead(range?206:200,{'Content-Length':end-start+1,'ETag':'"browser-fixture"','Accept-Ranges':'bytes','Content-Disposition':'attachment; filename="fixture.bin"',...(range?{'Content-Range':`bytes ${start}-${end}/${data.length}`}:{})});
 let at=start;const t=setInterval(()=>{if(at>end){clearInterval(t);res.end();return;}res.write(data.subarray(at,Math.min(at+16384,end+1)));at+=16384;},10);res.on('close',()=>clearInterval(t));
});
(async()=>{await new Promise(r=>server.listen(0,'127.0.0.1',r));const base='http://127.0.0.1:'+server.address().port;
 try{
 setup();
 const ext=path.join(run,'browser','extension');
 context=await chromium.launchPersistentContext(path.join(run,'profile'),{channel:'msedge',headless:true,chromiumSandbox:true,ignoreDefaultArgs:['--disable-extensions'],acceptDownloads:true,downloadsPath:path.join(run,'browser-downloads'),args:['--disable-extensions-except='+ext,'--load-extension='+ext]});
 const worker=context.serviceWorkers()[0]||await context.waitForEvent('serviceworker',{timeout:15000});
 const id=fs.readFileSync(path.join(run,'browser','extension-id.txt'),'utf8').trim();if(!worker.url().includes(id))throw Error('extension identity mismatch');record('real-edge-extension-loaded',{browser_version:context.browser()?.version(),extension_id:id});
 await worker.evaluate(url=>send(url),base+'/manual.bin');await output(1,'manual.bin');record('native-cold-wakeup-and-http-hash');
 await worker.evaluate(()=>chrome.storage.local.set({capture:true}));
 const download=await worker.evaluate(url=>chrome.downloads.download({url,saveAs:false}),base+'/auto.bin');
 await output(2,'auto.bin');await until(async()=>{const a=await worker.evaluate(id=>chrome.downloads.search({id}),download);return a[0]?.state==='interrupted';});record('browser-auto-capture-ack-before-cancel-and-hash');
 // New GUI contract: real metadata via the task queue, then wait for explicit selection.
 const source=path.join(run,'seed','浏览器资源');fs.mkdirSync(source,{recursive:true});fs.writeFileSync(path.join(source,'one.bin'),data);fs.writeFileSync(path.join(source,'two.bin'),data.subarray(0,33333));
 const cli=path.join(run,'bin','core_cli.exe'),torrent=path.join(run,'fixture.torrent');cp.execFileSync(cli,['make-torrent-dir',source,torrent],{windowsHide:true});
 const port=server.address().port+1;seed=cp.spawn(cli,['bt-seed',torrent,path.dirname(source),String(port)],{windowsHide:true});let ready='';seed.stdout.on('data',b=>ready+=b);await until(()=>ready.includes('SEED_READY'),15);
 const uri=cp.execFileSync(cli,['magnet-uri',torrent,String(port)],{encoding:'utf8',windowsHide:true}).trim();fs.writeFileSync(path.join(run,'magnet.txt'),uri);
 await worker.evaluate(url=>send(url),uri);await until(()=>fs.readFileSync(path.join(run,'userdata','tasks.record'),'utf8').split('\n').some(s=>s.startsWith('3 5 ')),120);
 const bt=path.join(run,'userdata','Downloads','3-magnet');if(!fs.existsSync(path.join(bt,'metadata.record'))||fs.existsSync(path.join(bt,'files')))throw Error('Payload started before selection');record('browser-magnet-handoff-real-metadata-awaits-selection');
 ps(['-Command',"Start-Process -FilePath 'handmadebt:"+encodeURIComponent(base+'/protocol.bin')+"'"]);
 await output(4,'protocol.bin');record('windows-custom-protocol-dispatch-and-hash');
 const origin='chrome-extension://'+id+'/';for(const [name,url,o] of [['bad-scheme','file:///C:/Windows/win.ini',origin],['bad-origin',base+'/bad.bin','chrome-extension://'+'a'.repeat(32)+'/'],['injection',base+'/x\nhttps://bad.test/',origin]]){
  const body=Buffer.from(JSON.stringify({url})),len=Buffer.alloc(4);len.writeUInt32LE(body.length);const r=cp.spawnSync(path.join(run,'bin','handmadebt_host.exe'),[o],{input:Buffer.concat([len,body]),windowsHide:true});if(!r.stdout?.length||JSON.parse(r.stdout.subarray(4).toString()).accepted)throw Error('unsafe input accepted');record(name+'-rejected');
 }
 // Edge may cache native-host discovery. Remove only this isolated test helper
 // executable to deterministically exercise a launch failure.
 fs.renameSync(path.join(run,'bin','handmadebt_host.exe'),path.join(run,'bin','handmadebt_host.disabled'));
 const page=context.pages()[0]||await context.newPage();const cdp=await context.newCDPSession(page);await cdp.send('Browser.setDownloadBehavior',{behavior:'allow',downloadPath:path.join(run,'browser-downloads')});
 const failed=await worker.evaluate(url=>chrome.downloads.download({url,saveAs:false}),base+'/fallback.bin');
 let entry;try{entry=await until(async()=>{const a=await worker.evaluate(id=>chrome.downloads.search({id}),failed);return a[0]?.state==='complete'?a[0]:false;},30);}catch(e){console.log(JSON.stringify(await worker.evaluate(async id=>({downloads:await chrome.downloads.search({id}),storage:await chrome.storage.local.get(null)}),failed),null,2));throw e;}
 if(!path.resolve(entry.filename).startsWith(path.resolve(run)+path.sep)||sha(fs.readFileSync(entry.filename))!==sha(data))throw Error('fallback content/path mismatch');
 if(!fs.readFileSync(path.join(run,'userdata','tasks.record'),'utf8').includes('GUI2 5 4\n'))throw Error('unexpected fallback GUI task');record('handoff-failure-resumes-browser-and-hash');
 fs.writeFileSync(path.join(run,'results.json'),JSON.stringify({all_passed:true,run,expected_sha256:sha(data),tests},null,2));console.log('BROWSER PASS: '+run);
 }finally{
 if(context)await context.close();if(seed)seed.kill();if(registered)setup(true);
 const exe=path.join(run,'bin','handmadebt.exe').replaceAll("'","''");ps(['-Command',`Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -eq '${exe}' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }`]);
 for(const n of ['handmadebt.exe','handmadebt_host.exe','handmadebt_host.disabled','core_cli.exe','libc++.dll','libunwind.dll'])fs.rmSync(path.join(run,'bin',n),{force:true});
  server.closeAllConnections();await new Promise(r=>server.close(r));
 }
})().catch(e=>{console.error(e);process.exitCode=1;});
