async function browserRequest(url,item={}) {
  const message={url};
  if(!/^https?:/.test(url))return message;
  const u=new URL(url);const origin=u.protocol+'//'+u.hostname+'/*';
  if(item.incognito)return message;
  if(item.filename)message.filename=item.filename.split(/[\\/]/).pop();
  if (chrome.runtime.getManifest && !chrome.runtime.getManifest().optional_permissions?.includes('cookies')) return message;
  if(await chrome.permissions.contains({permissions:['cookies'],origins:[origin]})) {
    const stores=await chrome.cookies.getAllCookieStores();
    const store=stores.find(s=>s.tabIds.includes(item.tabId));
    const cookies=await chrome.cookies.getAll({url,...(store?{storeId:store.id}:{})});
    // Partitioned cookies need the top-level partition key; do not guess it.
    const selected=cookies.filter(c=>!c.partitionKey).sort((a,b)=>b.path.length-a.path.length);
    let referer='';try{const ref=new URL(item.referrer);if(ref.origin===u.origin){ref.hash='';referer=ref.href;}}catch(_){}
    message.session={cookies:selected.map(c=>({name:c.name,value:c.value,path:c.path,secure:c.secure})),referer,userAgent:navigator.userAgent};
  }
  return message;
}
const HOST = 'org.handmadebt.bridge';
const AUTH_HINT_TTL = 120000;
let authSequence = 0;
let activeAuthContext = null;
function permissionFor(origin) {
  const u = new URL(origin);
  return {permissions:['cookies'],origins:[u.protocol+'//'+u.hostname+'/*']};
}
async function beginAuthorizationContext(url, incognito=false) {
  const context = {token:`${Date.now()}:${++authSequence}`,origin:/^https?:/.test(url)?new URL(url).origin:'',expiresAt:Date.now()+AUTH_HINT_TTL,incognito};
  activeAuthContext = context;
  await chrome.storage.session.set({authorizationContext:context.token,authorizationHint:null});
  return context;
}
async function maybeOfferAuthorization(context) {
  if (context !== activeAuthContext || context.incognito || !context.origin || Date.now() >= context.expiresAt) return;
  if (await chrome.permissions.contains(permissionFor(context.origin))) return;
  if (context !== activeAuthContext) return;
  await chrome.storage.session.set({authorizationHint:{token:context.token,origin:context.origin,expiresAt:context.expiresAt}});
}
async function monitorManualTask(taskId, context, library) {
  for(let i=0;i<60;i++) {
    if(context!==activeAuthContext)return;
    const state=await chrome.runtime.sendNativeMessage(HOST,{task_id:taskId,...(library?{library}:{})});
    if(!state?.accepted||state.task_id!==taskId)return;
    if(state.status===5){await maybeOfferAuthorization(context);return;}
    if(state.status!==1)return;
    await new Promise(resolve=>setTimeout(resolve,1000));
  }
}
async function report(message, ok) {
  await chrome.storage.local.set({lastResult: message});
  await chrome.action.setBadgeText({text: ok ? 'OK' : '!'});
  await chrome.action.setBadgeBackgroundColor({color: ok ? '#267344' : '#ad3030'});
}
async function send(url) {
  if (!/^(https?:\/\/|magnet:\?)/.test(url || '')) throw new Error('仅支持 HTTP、HTTPS 和 Magnet 链接');
  const context=await beginAuthorizationContext(url);
  const result = await chrome.runtime.sendNativeMessage(HOST, await browserRequest(url));
  if (!result?.accepted) throw new Error('HandmadeBT 未确认保存任务');
  await report('已交给 HandmadeBT，下载结果请查看任务窗口。', true);
  if(Number.isSafeInteger(result.task_id)&&context.origin)monitorManualTask(result.task_id,context,result.library).catch(()=>{});
  return true;
}
chrome.runtime.onInstalled.addListener(() => {
  chrome.contextMenus.create({id:'send', title:'使用 HandmadeBT 下载链接', contexts:['link']});
});
chrome.contextMenus.onClicked.addListener(info => {
  if (info.menuItemId === 'send') send(info.linkUrl).catch(e => report(e.message, false));
});
chrome.runtime.onMessage.addListener((message, sender, reply) => {
  if (message?.action !== 'send') return;
  send(message.url).then(() => reply({ok:true}), e => {report(e.message, false); reply({ok:false,error:e.message});});
  return true;
});
// Pause the browser transfer during handoff; cancel only after the native task
// is accepted and its identity/status has been checked.
const CAPTURE_MAX_AGE_MS = 15000;
let captureAdmission = Promise.resolve();
function freshDownload(item, enabledSince = 0) {
  const started = Date.parse(item?.startTime);
  const now = Date.now();
  return Number.isSafeInteger(item?.id) && item.id >= 0 &&
    item.state === 'in_progress' && !item.paused &&
    Number.isFinite(started) && started >= enabledSince &&
    started <= now && now - started <= CAPTURE_MAX_AGE_MS &&
    /^https?:\/\//.test(item.finalUrl || item.url) &&
    (!item.danger || item.danger === 'safe');
}
// Serialize only admission, not the completion polling. Persist the claim BEFORE
// contacting the host so duplicate events and worker restarts cannot add twice.
function claimDownload(item) {
  const pending = captureAdmission.then(async () => {
    if (!freshDownload(item)) return null;
    const settings = await chrome.storage.local.get(['capture', 'captureSince', 'capturedDownloadsV1']);
    if (!settings.capture || !freshDownload(item, settings.captureSince || 0)) return null;
    const [current] = await chrome.downloads.search({id:item.id});
    if (!current || current.startTime !== item.startTime ||
        !freshDownload(current, settings.captureSince || 0)) return null;
    const key = `${item.id}:${Date.parse(item.startTime)}`;
    const seen = settings.capturedDownloadsV1 || {};
    if (Object.prototype.hasOwnProperty.call(seen, key)) return null;
    const now = Date.now();
    const recent = Object.entries(seen).filter(([,time]) => now - time < 86400000)
      .sort((a,b) => b[1]-a[1]).slice(0,2047);
    const claims = Object.fromEntries(recent);
    claims[key] = now;
    await chrome.storage.local.set({capturedDownloadsV1:claims});
    return current;
  });
  captureAdmission = pending.catch(() => {});
  return pending;
}
async function captureDownload(item) {
  let pausedItem = null;
  let handedOff = false;
  try {
    if ((item.finalUrl || item.url || '').startsWith('blob:')) {
      const settings = await chrome.storage.local.get(['capture', 'captureSince']);
      const started = Date.parse(item.startTime);
      if (settings.capture && started >= settings.captureSince && Date.now() - started <= CAPTURE_MAX_AGE_MS)
        await report('这个文件由网页在浏览器内生成，已交给浏览器保存。请在浏览器下载记录中查看。', true);
      return;
    }
    item = await claimDownload(item);
    if (!item) return;
    await chrome.downloads.pause(item.id);
    pausedItem = item;
    const settings = await chrome.storage.local.get(['capture','captureSince']);
    const [latest] = await chrome.downloads.search({id:item.id});
    if (!settings.capture || !latest || latest.startTime !== item.startTime ||
        !latest.paused || !freshDownload({...latest, paused:false}, settings.captureSince || 0))
      throw new Error('下载状态或接管设置已改变。');
    const url = latest.finalUrl || latest.url;
    const context = await beginAuthorizationContext(url, latest.incognito);
    const request = await browserRequest(url, latest);
    const result = await chrome.runtime.sendNativeMessage(HOST, request);
    if (!result?.accepted || !Number.isSafeInteger(result.task_id) || result.task_id <= 0)
      throw new Error('下载器未提供可核实的任务编号');
    const state = await chrome.runtime.sendNativeMessage(HOST, {task_id:result.task_id,...(result.library?{library:result.library}:{})});
    if (!state?.accepted || state.task_id !== result.task_id) throw new Error('无法核实下载状态');
    if (state.status === 5) {
      await maybeOfferAuthorization(context);
      throw new Error('网站拒绝下载，请查看任务窗口或授权网站登录状态。');
    }
    if (state.status !== 1 && state.status !== 2) throw new Error('HandmadeBT 未能开始下载，请查看任务窗口。');
    const [current] = await chrome.downloads.search({id:item.id});
    if (!current || current.startTime !== item.startTime || current.state !== 'in_progress' || !current.paused)
      throw new Error('浏览器下载状态已改变，未自动取消。');
    await chrome.downloads.cancel(item.id);
    handedOff = true;
    await report('已由 HandmadeBT 接管，浏览器下载已停止；进度和后续错误请查看任务窗口。', true);
    if(state.status===1)monitorManualTask(result.task_id,context,result.library).catch(()=>{});
  } catch (e) {
    let fallback = '已保留浏览器下载。';
    if (pausedItem && !handedOff) {
      try {
        const [current] = await chrome.downloads.search({id:pausedItem.id});
        if (current?.startTime === pausedItem.startTime && current.state === 'in_progress' && current.paused) {
          await chrome.downloads.resume(pausedItem.id);
          fallback = '接管失败，已恢复浏览器下载。';
        }
      } catch (_) { fallback = '接管失败，请在浏览器中手动恢复下载。'; }
    }
    await report(fallback + e.message, false);
  }
}
let handoffs = 0;
chrome.downloads.onCreated.addListener(async item => {
  handoffs++;
  try { await captureDownload(item); }
  finally { handoffs--; }
});

// The packager embeds the deployed fingerprint here and publishes its file list.
const installedBuild = 'DEVELOPMENT';
if (chrome.alarms && installedBuild !== 'DEVELOPMENT') {
  const alarm = 'check-installed-files';
  chrome.alarms.get(alarm).then(existing => {
    if (!existing) return chrome.alarms.create(alarm, {periodInMinutes: 1});
  });
  chrome.alarms.onAlarm.addListener(async event => {
    if (event.name !== alarm || handoffs) return;
    try {
      const read = async name => {
        const response = await fetch(chrome.runtime.getURL(name), {cache:'no-store'});
        if (!response.ok) throw Error('Incomplete update');
        return response.arrayBuffer();
      };
      const hex = async bytes => Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)),
        value => value.toString(16).padStart(2,'0')).join('');
      const update = JSON.parse(new TextDecoder().decode(await read('build.json')));
      if (update.build === installedBuild || !/^[a-f0-9]{64}$/.test(update.build)) return;
      for (const [name, digest] of Object.entries(update.files)) {
        if (!/^[a-zA-Z0-9_.-]+$/.test(name) || await hex(await read(name)) !== digest) return;
      }
      if (!handoffs) chrome.runtime.reload();
    } catch (_) { /* Retry on the next alarm if extraction is still in progress. */ }
  });
}
