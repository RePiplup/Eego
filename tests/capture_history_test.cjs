const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync(require('node:path').join(__dirname,'../browser/extension/worker.js'),'utf8');
let now=1800000000000;
const item=(extra={})=>({id:1,startTime:new Date(now-1000).toISOString(),state:'in_progress',paused:false,url:'https://example.test/file',danger:'safe',...extra});
function env(store={capture:true}, current=item(), options={}) {
  let sends=0, cancels=0, searches=0, pauses=0, resumes=0;
  const noop={addListener(){}};
  const chrome={storage:{session:{async set(){}},local:{async get(keys){return Object.fromEntries((Array.isArray(keys)?keys:[keys]).map(k=>[k,store[k]]));},async set(value){Object.assign(store,value);}}},
    downloads:{onCreated:noop,async search(){searches++;return current?[{...current}]:[];},async pause(){pauses++;current.paused=true;},async resume(){resumes++;current.paused=false;},async cancel(){if(options.failCancel)throw Error("cancel failed");cancels++;current.state="interrupted";}},
    runtime:{onInstalled:noop,onMessage:noop,async sendNativeMessage(host,msg){sends++;if(options.failHost)throw Error('offline');return {accepted:true,task_id:1,status:options.status??2};}},
    contextMenus:{onClicked:noop,create(){}},action:{async setBadgeText(){},async setBadgeBackgroundColor(){}},
    permissions:{async contains(){if(options.completeDuringCookie && !current.paused)current.state='complete';return false;}}};
  const sandbox=vm.createContext({chrome,URL,Date:class extends Date{static now(){return now;}},setTimeout,navigator:{userAgent:'fixture'}});
  vm.runInContext(source,sandbox);
  return {run:x=>sandbox.captureDownload(x),counts:()=>({sends,cancels,searches,pauses,resumes}),store};
}
(async()=>{
 let passed=0;
 async function rejected(name,event,current=event,store={capture:true}){const e=env(store,current);await e.run(event);assert.equal(e.counts().sends,0,name);passed++;}
 for(const state of ['complete','interrupted'])await rejected(state,item({state}));
 await rejected('old completed history',item({state:'complete',startTime:new Date(now-86400000).toISOString()}));
 await rejected('old in-progress recovery',item({startTime:new Date(now-86400000).toISOString()}));
 await rejected('missing time',item({startTime:undefined}));
 await rejected('invalid time',item({startTime:'invalid'}));
 await rejected('future time',item({startTime:new Date(now+1000).toISOString()}));
 await rejected('paused',item({paused:true}));
 await rejected('disabled',item(),item(),{capture:false});
 await rejected('predates enable',item(),item(),{capture:true,captureSince:now});
 await rejected('finished before lookup',item(),item({state:'complete'}));
 await rejected('erased before lookup',item(),null);
 await rejected('unsafe',item({danger:'dangerous'}));
 const race=env({capture:true},item(),{completeDuringCookie:true});await race.run(item());assert.equal(race.counts().cancels,1);passed++;
 const e=env();await Promise.all([e.run(item()),e.run(item()),e.run(item())]);assert.equal(e.counts().sends,2);assert.equal(e.counts().cancels,1);assert.equal(e.counts().pauses,1);passed++;
 const restarted=env(e.store);await restarted.run(item());assert.equal(restarted.counts().sends,0);passed++;
 const second=env(e.store,item({id:2}));await second.run(item({id:2}));assert.equal(second.counts().sends,2);passed++;
 const failed=env({capture:true},item(),{failHost:true});await failed.run(item());await failed.run(item());assert.equal(failed.counts().sends,1);assert.equal(failed.counts().cancels,0);assert.equal(failed.counts().resumes,1);passed++;
 for(const status of [3,5]){const f=env({capture:true},item(),{status});await f.run(item());assert.equal(f.counts().cancels,0);assert.equal(f.counts().resumes,1);passed++;}
 const cancelFailure=env({capture:true},item(),{failCancel:true});await cancelFailure.run(item());assert.equal(cancelFailure.counts().resumes,1);passed++;
 console.log(`${passed} capture history/race/dedup/fallback checks passed`);
})().catch(e=>{console.error(e);process.exitCode=1;});
