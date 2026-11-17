const el = id => document.getElementById(id);
chrome.storage.local.get(['capture','lastResult']).then(s => {el('capture').checked=!!s.capture;el('result').textContent=s.lastResult||'';});
el('capture').onchange=()=>chrome.storage.local.set({capture:el('capture').checked,captureSince:Date.now()});
el('send').onclick=async()=>{el('send').disabled=true;try{const r=await chrome.runtime.sendMessage({action:'send',url:el('url').value.trim()});el('result').textContent=r.ok?'已添加，请查看 HandmadeBT 窗口':r.error;}catch(e){el('result').textContent=e.message;}finally{el('send').disabled=false;}};

// Discard the obsolete last-site label; existing site permissions are retained.
chrome.storage.local.remove('sessionSite');
let pendingHint=null, renderSequence=0, expiryTimer;
function permissionFor(origin) {
  const u=new URL(origin);
  return {permissions:['cookies'],origins:[u.protocol+'//'+u.hostname+'/*']};
}
async function renderAuthorization() {
  const sequence=++renderSequence;
  pendingHint=null;el('authorization').hidden=true;clearTimeout(expiryTimer);
  const state=await chrome.storage.session.get(['authorizationHint','authorizationContext']);
  const hint=state.authorizationHint;
  if(!hint || hint.token!==state.authorizationContext || !Number.isFinite(hint.expiresAt) || hint.expiresAt<=Date.now())return;
  let url;try{url=new URL(hint.origin);}catch(_){return;}
  if(!['http:','https:'].includes(url.protocol)||url.origin!==hint.origin)return;
  const input=el('url').value.trim();
  if(input){try{if(new URL(input).origin!==hint.origin)return;}catch(_){return;}}
  const authorized=await chrome.permissions.contains(permissionFor(hint.origin));
  if(sequence!==renderSequence || authorized || hint.expiresAt<=Date.now())return;
  pendingHint=hint;
  el('sessionSite').textContent='此次下载的网站：'+hint.origin;
  el('authorize').disabled=false;el('authorization').hidden=false;
  expiryTimer=setTimeout(renderAuthorization,hint.expiresAt-Date.now());
}
el('authorize').onclick=()=>{
  const hint=pendingHint;
  if(!hint || hint.expiresAt<=Date.now())return renderAuthorization();
  el('authorize').disabled=true;
  // Request directly in the user's gesture; do not grant permissions silently.
  chrome.permissions.request(permissionFor(hint.origin)).then(async ok=>{
    el('result').textContent=ok?'已授权 '+hint.origin+'。请回原网站重新点击下载。':'未授权，浏览器下载仍会保留。';
    const state=await chrome.storage.session.get('authorizationHint');
    if(state.authorizationHint?.token===hint.token)await chrome.storage.session.set({authorizationHint:null});
    await renderAuthorization();
  }).catch(e=>{el('result').textContent=e.message;renderAuthorization();});
};
el('dismissAuth').onclick=async()=>{
  const hint=pendingHint;
  const state=await chrome.storage.session.get('authorizationHint');
  if(hint && state.authorizationHint?.token===hint.token)await chrome.storage.session.set({authorizationHint:null});
  await renderAuthorization();
};
el('url').addEventListener('input',renderAuthorization);
chrome.storage.onChanged.addListener((changes,area)=>{if(area==='session'&&(changes.authorizationHint||changes.authorizationContext))renderAuthorization();});
chrome.permissions.onAdded.addListener(renderAuthorization);
chrome.permissions.onRemoved.addListener(renderAuthorization);
renderAuthorization();
