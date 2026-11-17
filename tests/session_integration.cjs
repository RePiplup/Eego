const fs=require('fs'),path=require('path'),cp=require('child_process'),http=require('http'),crypto=require('crypto');
const {chromium}=require('C:/Users/12534/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const root='D:/CXworkspace/HandmadeBT',run=path.join(root,'artifacts/test-runs','session-safe-'+Date.now());
fs.mkdirSync(path.join(run,'bin'),{recursive:true});
for(const n of ['handmadebt.exe','handmadebt_host.exe','libc++.dll','libunwind.dll'])fs.copyFileSync(path.join(root,n==='core_cli.exe'?'build':'dist/HandmadeBT',n),path.join(run,'bin',n));
fs.cpSync(path.join(root,'browser'),path.join(run,'browser'),{recursive:true});
// Do not inherit another installation's rollback record.
fs.rmSync(path.join(run,'browser','registration-backup.json'),{force:true});
const data=Buffer.alloc(256*1024,75),sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const requests=[];
const tests=[],sleep=ms=>new Promise(r=>setTimeout(r,ms));let context,registered=false;
function ps(args){return cp.execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass',...args],{encoding:'utf8',windowsHide:true});}
function setup(remove=false){ps(['-File',path.join(run,'browser/Register-Browser.ps1'),...(remove?['-Unregister']:[])]);registered=!remove;}
async function until(fn,seconds=35){const end=Date.now()+seconds*1000;while(Date.now()<end){const x=await fn();if(x)return x;await sleep(100);}throw Error('Timed out');}
function record(name){tests.push(name);console.log(name+': PASS');}
const server=http.createServer((req,res)=>{
 const browser=!req.headers.range;requests.push({url:req.url,cookie:req.headers.cookie||'',referer:req.headers.referer||''});
 if(req.url==='/cross'){res.writeHead(302,{Location:'http://localhost:'+server.address().port+'/cross-target','Content-Length':0});res.end();return;}
 if(req.url==='/scoped/start'){res.writeHead(302,{Location:'/outside','Content-Length':0});res.end();return;}
 if(req.url==='/session.bin'&&!req.headers.cookie?.includes('fixture=logged-in')){res.writeHead(200,{'Content-Length':0});res.end();return;}
 const range=/bytes=(\d+)-(\d*)/.exec(req.headers.range||'');let start=range?+range[1]:0,end=range&&range[2]?+range[2]:data.length-1;
 res.writeHead(range?206:200,{'Content-Length':end-start+1,'ETag':'"capture-safe"','Content-Type':'application/octet-stream','Content-Disposition':'attachment; filename="fixture.bin"',...(range?{'Content-Range':`bytes ${start}-${end}/${data.length}`}:{})});
 let at=start;const timer=setInterval(()=>{if(at>end){clearInterval(timer);res.end();return;}res.write(data.subarray(at,Math.min(at+8192,end+1)));at+=8192;},browser&&['/fast-native.bin','/session.bin'].includes(req.url)?100:5);res.on('close',()=>clearInterval(timer));
});
(async()=>{
 await new Promise(r=>server.listen(0,r));const base='http://127.0.0.1:'+server.address().port;
 try{
  setup();const ext=path.join(run,'browser/extension');
  const mf=path.join(ext,'manifest.json'),manifest=JSON.parse(fs.readFileSync(mf));manifest.permissions.push('cookies');manifest.host_permissions=['http://127.0.0.1/*'];fs.writeFileSync(mf,JSON.stringify(manifest));
  context=await chromium.launchPersistentContext(path.join(run,'profile'),{channel:'msedge',headless:true,chromiumSandbox:true,ignoreDefaultArgs:['--disable-extensions'],acceptDownloads:true,downloadsPath:path.join(run,'browser-downloads'),args:['--disable-extensions-except='+ext,'--load-extension='+ext]});
  await context.addCookies([{name:'fixture',value:'logged-in',url:base},{name:'path-only',value:'private-path-value',domain:'127.0.0.1',path:'/scoped/'}]);
  const page=context.pages()[0]||await context.newPage();const cdp=await context.newCDPSession(page);await cdp.send('Browser.setDownloadBehavior',{behavior:'allow',downloadPath:path.join(run,'browser-downloads')});
  const worker=context.serviceWorkers()[0]||await context.waitForEvent('serviceworker');
  await worker.evaluate(()=>chrome.storage.local.set({capture:true}));
  // Instrument actual API calls to prove cancellation sees an already committed file.
  await worker.evaluate(()=>{globalThis.cancelEvidence=[];const original=chrome.downloads.cancel.bind(chrome.downloads);chrome.downloads.cancel=async id=>{cancelEvidence.push({id,time:Date.now()});return original(id);};});
  const id=await worker.evaluate(url=>chrome.downloads.download({url,saveAs:false}),base+'/fast-native.bin');
  await until(async()=>{const a=await worker.evaluate(id=>chrome.downloads.search({id}),id);return a[0]?.state==='interrupted';});
  const output=id=>path.join(run,'userdata/Downloads',fs.readdirSync(path.join(run,'userdata/Downloads')).find(n=>n.startsWith(id+'-')&&!n.endsWith('.hbt-lock')&&!n.endsWith('.resume')&&!n.endsWith('.part')));
  const file=output(1);if(!fs.existsSync(file)||sha(fs.readFileSync(file))!==sha(data))throw Error('cancelled without valid native output');
  const state=await worker.evaluate(()=>chrome.runtime.sendNativeMessage(HOST,{task_id:1}));if(state.status!==2||state.task_id!==1)throw Error('completion status wrong');record('native-completion-and-hash-before-browser-cancel');
  const auth=await worker.evaluate(url=>chrome.downloads.download({url,saveAs:false}),base+'/session.bin');
  await until(async()=>{const state=await worker.evaluate(()=>chrome.runtime.sendNativeMessage(HOST,{task_id:2}));return state.status===2;});
  if(sha(fs.readFileSync(output(2)))!==sha(data))throw Error('native authenticated content corrupt');
  record('authenticated-cookie-download-completes-in-native-engine');
  if(!requests.some(q=>q.url==='/session.bin'&&q.cookie.includes('fixture=logged-in')))throw Error('cookie absent');
  const hand=async route=>worker.evaluate(async url=>chrome.runtime.sendNativeMessage(HOST,await browserRequest(url)),base+route);
  const cross=await hand('/cross');await until(async()=>{const state=await worker.evaluate(id=>chrome.runtime.sendNativeMessage(HOST,{task_id:id}),cross.task_id);return state.status===2;});
  if(requests.filter(q=>q.url==='/cross-target').some(q=>q.cookie||q.referer))throw Error('cross origin credential leak');record('cross-origin-redirect-never-forwards-credentials');
  const scoped=await hand('/scoped/start');await until(async()=>{const state=await worker.evaluate(id=>chrome.runtime.sendNativeMessage(HOST,{task_id:id}),scoped.task_id);return state.status===2;});
  if(!requests.some(q=>q.url==='/scoped/start'&&q.cookie.includes('path-only='))||requests.filter(q=>q.url==='/outside').some(q=>q.cookie.includes('path-only=')))throw Error('cookie path isolation failed');record('redirect-respects-cookie-path');
  const bad=await worker.evaluate(async url=>{const m=await browserRequest(url);m.session.cookies=[{name:'bad',value:'x\r\nInjected: bad',path:'/',secure:false}];return chrome.runtime.sendNativeMessage(HOST,m);},base+'/bad');if(bad.accepted)throw Error('header injection accepted');record('cookie-header-injection-rejected');
  for(const n of fs.readdirSync(path.join(run,'userdata'))){const f=path.join(run,'userdata',n);if(n.endsWith('.record')&&fs.readFileSync(f).includes(Buffer.from('logged-in')))throw Error('session cookie persisted');}record('credentials-absent-from-task-and-ownership-records');
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
