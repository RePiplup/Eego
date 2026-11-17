const fs=require('fs'),path=require('path'),cp=require('child_process'),http=require('http'),crypto=require('crypto');
const {chromium}=require('C:/Users/12534/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const root='D:/CXworkspace/HandmadeBT',run=path.join(root,'artifacts/test-runs','capture-safe-'+Date.now());
fs.mkdirSync(path.join(run,'bin'),{recursive:true});
for(const n of ['handmadebt.exe','handmadebt_host.exe','libc++.dll','libunwind.dll'])fs.copyFileSync(path.join(root,n==='core_cli.exe'?'build':'dist/HandmadeBT',n),path.join(run,'bin',n));
fs.cpSync(path.join(root,'browser'),path.join(run,'browser'),{recursive:true});
// Do not inherit another installation's rollback record.
fs.rmSync(path.join(run,'browser','registration-backup.json'),{force:true});
const data=Buffer.alloc(256*1024,75),sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const tests=[],sleep=ms=>new Promise(r=>setTimeout(r,ms));let context,registered=false;
function ps(args){return cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass',...args],{encoding:'utf8',windowsHide:true});}
function setup(remove=false){ps(['-File',path.join(run,'browser/Register-Browser.ps1'),...(remove?['-Unregister']:[])]);registered=!remove;}
async function until(fn,seconds=35){const end=Date.now()+seconds*1000;while(Date.now()<end){const x=await fn();if(x)return x;await sleep(100);}throw Error('Timed out');}
function record(name){tests.push(name);console.log(name+': PASS');}
const server=http.createServer((req,res)=>{
 const browser=!(req.headers['user-agent']||'').startsWith('HandmadeBT/');
 if(req.url==='/session.bin'&&!req.headers.cookie?.includes('fixture=logged-in')){res.writeHead(200,{'Content-Length':0});res.end();return;}
 const range=/bytes=(\d+)-(\d*)/.exec(req.headers.range||'');let start=range?+range[1]:0,end=range&&range[2]?+range[2]:data.length-1;
 res.writeHead(range?206:200,{'Content-Length':end-start+1,'ETag':'"capture-safe"','Content-Type':'application/octet-stream','Content-Disposition':'attachment; filename="fixture.bin"',...(range?{'Content-Range':`bytes ${start}-${end}/${data.length}`}:{})});
 let at=start;const timer=setInterval(()=>{if(at>end){clearInterval(timer);res.end();return;}res.write(data.subarray(at,Math.min(at+8192,end+1)));at+=8192;},browser&&req.url==='/fast-native.bin'?100:5);res.on('close',()=>clearInterval(timer));
});
(async()=>{
 await new Promise(r=>server.listen(0,'127.0.0.1',r));const base='http://127.0.0.1:'+server.address().port;
 try{
  setup();const ext=path.join(run,'browser/extension');
  context=await chromium.launchPersistentContext(path.join(run,'profile'),{channel:'msedge',headless:true,chromiumSandbox:true,ignoreDefaultArgs:['--disable-extensions'],acceptDownloads:true,downloadsPath:path.join(run,'browser-downloads'),args:['--disable-extensions-except='+ext,'--load-extension='+ext]});
  await context.addCookies([{name:'fixture',value:'logged-in',url:base}]);
  const page=context.pages()[0]||await context.newPage();const cdp=await context.newCDPSession(page);await cdp.send('Browser.setDownloadBehavior',{behavior:'allow',downloadPath:path.join(run,'browser-downloads')});
  const worker=context.serviceWorkers()[0]||await context.waitForEvent('serviceworker');
  await worker.evaluate(()=>chrome.storage.local.set({capture:true}));
  // Observe browser cancellation, then independently verify native completion.
  await worker.evaluate(()=>{globalThis.cancelEvidence=[];const original=chrome.downloads.cancel.bind(chrome.downloads);chrome.downloads.cancel=async id=>{cancelEvidence.push({id,time:Date.now()});return original(id);};});
  const id=await worker.evaluate(url=>chrome.downloads.download({url,saveAs:false}),base+'/fast-native.bin');
  await until(async()=>{const a=await worker.evaluate(id=>chrome.downloads.search({id}),id);return a[0]?.state==='interrupted';});
  await until(async()=>{const state=await worker.evaluate(()=>chrome.runtime.sendNativeMessage(HOST,{task_id:1}));return state.status===2;});
  const file=path.join(run,'userdata/Downloads/1-fast-native.bin');if(!fs.existsSync(file)||sha(fs.readFileSync(file))!==sha(data))throw Error('cancelled without valid native output');
  const state=await worker.evaluate(()=>chrome.runtime.sendNativeMessage(HOST,{task_id:1}));if(state.status!==2||state.task_id!==1)throw Error('completion status wrong');record('browser-stopped-after-handoff-native-eventually-completes');
  const auth=await worker.evaluate(url=>chrome.downloads.download({url,saveAs:false}),base+'/session.bin');
  const entry=await until(async()=>{const a=await worker.evaluate(id=>chrome.downloads.search({id}),auth);return a[0]?.state==='complete'?a[0]:false;});
  if(sha(fs.readFileSync(entry.filename))!==sha(data))throw Error('authenticated fallback corrupt');
  await until(()=>fs.readFileSync(path.join(run,'userdata/tasks.record'),'utf8').includes('2 4 0 0'));
  const failed=await worker.evaluate(()=>chrome.runtime.sendNativeMessage(HOST,{task_id:2}));if(failed.status!==3)throw Error('failed status wrong');
  if((await worker.evaluate(()=>cancelEvidence)).some(x=>x.id===auth))throw Error('authenticated browser cancelled');record('session-required-native-failure-browser-completes-with-cookie');
  fs.renameSync(path.join(run,'bin/handmadebt_host.exe'),path.join(run,'bin/handmadebt_host.disabled'));
  const off=await worker.evaluate(url=>chrome.downloads.download({url,saveAs:false}),base+'/offline.bin');
  const offentry=await until(async()=>{const a=await worker.evaluate(id=>chrome.downloads.search({id}),off);return a[0]?.state==='complete'?a[0]:false;});
  if(sha(fs.readFileSync(offentry.filename))!==sha(data))throw Error('offline fallback corrupt');
  if((await worker.evaluate(()=>cancelEvidence)).some(x=>x.id===off))throw Error('offline browser cancelled');record('native-host-unavailable-browser-completes');
  fs.writeFileSync(path.join(run,'results.json'),JSON.stringify({passed:true,tests},null,2));console.log('ALL PASS '+run);
 }finally{
  if(context)await context.close();if(registered)setup(true);
  const exe=path.join(run,'bin/handmadebt.exe').replaceAll("'","''");ps(['-Command',`Get-Process handmadebt -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq '${exe}' } | Stop-Process -Force`]);
  for(const n of ['handmadebt.exe','handmadebt_host.exe','handmadebt_host.disabled','core_cli.exe','libc++.dll','libunwind.dll'])fs.rmSync(path.join(run,'bin',n),{force:true});
  server.closeAllConnections();server.close();
 }
})().catch(e=>{console.error(e);process.exitCode=1;});
