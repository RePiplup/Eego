// Isolated Edge profile + real downloads API; native host is stubbed so no user
// registry, browser profile, native task, or external network is touched.
const fs=require('fs'),path=require('path'),http=require('http'),assert=require('assert/strict');
const {chromium}=require('C:/Users/12534/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const root=path.resolve(__dirname,'..'),run=path.join(root,'artifacts/test-runs','history-browser-'+Date.now());
fs.mkdirSync(run,{recursive:true});
const ext=path.join(run,'extension');fs.cpSync(path.join(root,'browser/extension'),ext,{recursive:true});
fs.appendFileSync(path.join(ext,'worker.js'),`\nglobalThis.nativeAdds=[];chrome.runtime.sendNativeMessage=async(host,msg)=>{if(msg.url)nativeAdds.push(msg.url);return {accepted:true,task_id:1,status:3};};`);
const server=http.createServer((req,res)=>{res.writeHead(200,{'Content-Type':'application/octet-stream','Content-Disposition':'attachment; filename="fixture.bin"','Content-Length':65536});let sent=0;const t=setInterval(()=>{res.write(Buffer.alloc(4096,7));sent+=4096;if(sent===65536){clearInterval(t);res.end();}},40);res.on('close',()=>clearInterval(t));});
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function until(fn){for(let n=0;n<100;n++){if(await fn())return;await sleep(100);}throw Error('timeout');}
let context;
async function launch(){context=await chromium.launchPersistentContext(path.join(run,'profile'),{channel:'msedge',headless:true,chromiumSandbox:true,ignoreDefaultArgs:['--disable-extensions'],args:['--disable-extensions-except='+ext,'--load-extension='+ext]});const p=context.pages()[0];const cdp=await context.newCDPSession(p);await cdp.send('Browser.setDownloadBehavior',{behavior:'allow',downloadPath:path.join(run,'downloads')});return context.serviceWorkers()[0]||await context.waitForEvent('serviceworker');}
(async()=>{await new Promise(r=>server.listen(0,'127.0.0.1',r));const url='http://127.0.0.1:'+server.address().port+'/fixture.bin';try{
 let worker=await launch();await worker.evaluate(()=>chrome.storage.local.set({capture:true,captureSince:Date.now()}));
 const id=await worker.evaluate(url=>chrome.downloads.download({url}),url);
 await until(()=>worker.evaluate(()=>nativeAdds.length===1));
 await until(()=>worker.evaluate(async id=>(await chrome.downloads.search({id}))[0]?.state==='complete',id));
 await worker.evaluate(async id=>{const [entry]=await chrome.downloads.search({id});await Promise.all([captureDownload(entry),captureDownload(entry)]);},id);
 assert.equal(await worker.evaluate(()=>nativeAdds.length),1);
 await context.close();context=null;worker=await launch();
 await worker.evaluate(async()=>{for(const entry of await chrome.downloads.search({}))await captureDownload(entry);});
 assert.equal(await worker.evaluate(()=>nativeAdds.length),0);
 await worker.evaluate(url=>chrome.downloads.download({url}),url);
 await until(()=>worker.evaluate(()=>nativeAdds.length===1));
 const result={passed:true,checks:['fresh real download captured','completed history replay ignored','browser restart history ignored','new same-URL download after restart captured'],nativeHost:'stubbed; no registration changes'};
 fs.writeFileSync(path.join(run,'results.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result));
 }finally{if(context)await context.close();server.closeAllConnections();server.close();}})().catch(e=>{console.error(e);process.exitCode=1;});
