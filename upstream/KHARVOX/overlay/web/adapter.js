'use strict';

const {root,q,update,quality}=window.overlayUI;

const token=document.querySelector('meta[name=overlay-token]').content;

q('close').onclick=async()=>{try{const r=await fetch('/xr/close',{method:'POST',headers:{'X-Overlay-Token':token}});const result=await r.json();if(!r.ok||result.error)throw Error(result.error||'Close unavailable');}catch(error){q('native-status').textContent=error.message;}};

const labelStatus=document.createElement('div');labelStatus.className='status';labelStatus.hidden=true;q('red').parentElement.after(labelStatus);

const groups={render:['mode','internal','neural-scale','before','style','intensity','structure','tone','auto-skin','skin','mask','detail','color','sharpness','preset','sharpen','neural-model']};

let state=null,online=false,busy=false;

const pending={render:null},notice={render:''};

const dirty={render:false},base={render:null};

// Keep uncertain submissions until a fresh publication identifies their result.

// Read-only confirmation never repeats a mutation or discards a newer draft.

const attempts={render:null};

const field={render:'Controls'};

function val(id,value){const el=q(id);if(value===undefined)return el.type==='checkbox'?+el.checked:el.tagName==='SELECT'?el.selectedIndex:Number(el.value);if(el.type==='checkbox')el.checked=!!value;else if(el.tagName==='SELECT')el.selectedIndex=value;else {el.value=value;el.nextElementSibling.value=value;}}

function parseRender(text){const v=text.split(';').map(Number);if(v[0]<2||v[0]>6||v.length!==({2:6,3:7,4:8,5:18,6:19})[v[0]])throw Error('Unsupported renderer protocol');return [v[1],v[5]/100,v[6]??85,v[7]??0,...(v[0]>=5?v.slice(8,18):[2,100,100,100,1,100,0,100,100,0]),[0,10,11,12,13].indexOf(v[3]),v[4],v[18]??0];}

function load(group,text){const values=parseRender(text);groups[group].forEach((id,i)=>val(id,values[i]));base[group]=text;dirty[group]=false;}

function encode(group){const scale=Math.round(val('internal')*100);return [6,val('mode'),scale<5400?2:scale<6234?1:0,[0,10,11,12,13][val('preset')],val('sharpen'),scale,val('neural-scale'),val('before'),...['style','intensity','structure','tone','auto-skin','skin','mask','detail','color','sharpness'].map(id=>val(id)),val('neural-model')].join(';');}

function retryable(snapshot=state){return snapshot?.fields.Status?.value.startsWith('ERROR: Processing stopped. Apply retries with the selected settings. ')&&snapshot.fields.Status.ageMs<=2000;}
function hardFault(snapshot=state){return snapshot?.fields.Status?.value.startsWith('ERROR')&&!retryable(snapshot);}
function availability(){

 for(const el of root.querySelectorAll('input,select,button'))
  if(!el.closest('#pv-foveated-dlss')&&el.dataset.overlayPage!=='foveated_dlss')el.disabled=false;

 update();const fault=hardFault(state);
 const modes=state?.capabilities?.modes??[0,1,2,3];
 for(const [index,option] of [...q('mode').options].entries()){
  option.disabled=!modes.includes(index);
  option.title=index===4&&option.disabled?(state?.capabilities?.neuralReason??'Neural temporarily unavailable'):'';
 }
 const unavailable=!modes.includes(Number(val('mode')));

 for(const [group,ids] of Object.entries(groups))if(!online||busy||pending[group]||(group==='render'&&fault)){

  for(const id of ids){q(id).disabled=true;if(q(id).type==='range')q(id).nextElementSibling.disabled=true;}

  if(group==='render')for(const button of quality.children)button.disabled=true;

 }

   const incompatible=window.overlayFoveated?.generalConflict(Number(val('mode')));
   const blocked=!online||busy||!!pending.render||fault||incompatible||unavailable;
   if(unavailable&&online)q('action-status').textContent=state?.capabilities?.neuralReason??'Selected renderer unavailable';
   if(incompatible)q('action-status').textContent=Number(val('mode'))===4?'DLSS5 requires Fixed or Disabled on FOVEATED_DLSS.':'Apply Disabled on FOVEATED_DLSS before selecting this renderer. DLAA does not support foveation.';

 root.querySelector('[data-action="all-apply"]').disabled=blocked||(!dirty.render&&!window.overlayResolution?.dirty&&!retryable());

 root.querySelector('[data-action="all-save"]').disabled=blocked;

 q('discard').hidden=!dirty.render&&!window.overlayResolution?.dirty;q('discard').disabled=blocked;

 q('red').disabled=!online||busy;window.overlayResolution?.availability(blocked);

}

async function rpc(request){const response=await fetch('/api',{method:'POST',headers:{'Content-Type':'application/json','X-Overlay-Token':token},body:JSON.stringify(request)});const result=await response.json();if(!response.ok||result.error){const error=Error(result.error||response.statusText);error.rejected=response.ok;throw error;}return result;}

document.addEventListener('outputdraftchange',availability);
document.addEventListener('foveatedcompatibilitychange',availability);
q('discard').onclick=()=>{window.overlayResolution?.discard();for(const group of ['render'])load(group,state.fields[field[group]].value);q('action-status').textContent='Draft discarded; native selections restored.';availability();};

function changed(event){for(const [group,ids] of Object.entries(groups))if(ids.some(id=>q(id)===event.target||q(id).nextElementSibling===event.target)){dirty[group]=true;}availability();}

root.addEventListener('change',changed);root.addEventListener('input',changed);for(const button of quality.children)button.addEventListener('click',()=>{dirty.render=true;availability();});

async function send(group,event,data){if(!online||busy)return;busy=true;availability();const display='render';try{await rpc({op:'command',session:state.session,event,data,expected:group==='label'?state.fields.Label.value:base[group]});notice[display]='Request submitted; awaiting native status. This is not confirmation of active output.';if(group!=='label'&&event!=='Save')pending[group]={data,time:Date.now()};}catch(error){notice[display]=error.message;}finally{q(display+'-status').textContent=notice[display];busy=false;availability();}}

q('red').addEventListener('change',()=>send('label','SetLabel',String(val('red'))));

const same=(group,a,b)=>group==='render'?JSON.stringify(parseRender(a))===JSON.stringify(parseRender(b)):a===b;

function fresh(snapshot){if(snapshot.session!==state.session){const e=Error('Game session changed; review settings');e.terminal=true;throw e;}for(const key of ['Controls','Status'])if(!snapshot.fields[key]||snapshot.fields[key].ageMs>2000)throw Error('Renderer state unavailable or stale');if(hardFault(snapshot)){const e=Error(snapshot.fields.Status.value);e.terminal=true;throw e;}}

function reconcile(group,snapshot){
 const a=attempts[group],current=snapshot.fields[field[group]]?.value;
 if(!snapshot.fields.Status||snapshot.fields.Status.ageMs>2000||hardFault(snapshot))return false;
 if(!a||a.session!==snapshot.session||current===undefined||snapshot.fields[field[group]].ageMs>2000)return false;

 // An unchanged value cannot prove that a command with a lost reply ran.

 if(!same(group,current,a.data)||(!a.submitted&&same(group,a.before,a.data)))return false;

 console.info('Overlay action: native selection confirmed',group);a.confirmed=true;a.actual=current;attempts[group]=null;base[group]=current;

 if(same(group,encode(group),a.data))load(group,current);

 notice[group]='Native requested settings acknowledged; effective output is shown in native status.';

 if(!busy)q('action-status').textContent='Previous request confirmed by native settings. '+(dirty[group]?'Newer draft preserved.':'');

 return true;

}

async function confirm(group,a){

 const deadline=Date.now()+12000;let reason='No matching native acknowledgement for '+group;

 while(Date.now()<deadline){

  if(state.session!==a.session)throw Error('Game session changed; remaining steps stopped');

  if(a.confirmed)return a.actual;

  try{const ack=await rpc({op:'state'});fresh(ack);state=ack;reconcile(group,ack);if(a.confirmed)return a.actual;}

  catch(error){if(error.terminal)throw error;reason=error.message;}

  await new Promise(r=>setTimeout(r,150));

 }

 throw Error(reason+'; confirmation timed out (no automatic retry)');

}

async function unified(save){
   if(window.overlayFoveated?.generalConflict(Number(val('mode')))){availability();return;}

 if(!online||busy)return;

 const session=state.session,values={render:encode('render')};

 let outputRequest=null;
 const selected=['render'].filter(group=>save||dirty[group]||(group==='render'&&retryable()));

 const completed=[];let attempted=false;busy=true;availability();q('action-status').textContent=save?'Applying and requesting startup saveâ€¦':'Applyingâ€¦';

 try{

  outputRequest=window.overlayResolution?.capture(save);
  await window.overlayResolution?.preflight(outputRequest);
  // Reconcile our own late confirmation before checking for real UEVR edits.

  const snapshot=await rpc({op:'state'});fresh(snapshot);

  for(const group of ['render'])reconcile(group,snapshot);

  const expected={...base};

  for(const group of selected)if(!same(group,snapshot.fields[field[group]].value,expected[group]))throw Error('Settings changed in UEVR; discard or review draft');

  for(const group of selected){

   const event='Apply';

   const a={session,data:values[group],before:expected[group],submitted:false,confirmed:false};attempts[group]=a;

   attempted=true;console.info('Overlay action: submitting',event,values[group]);

   try{const result=await rpc({op:'command',session,event,data:values[group],expected:expected[group]});

    a.submitted=result.submitted===true;if(!a.submitted){const e=Error('Native command was not submitted');e.rejected=true;throw e;}

    completed.push(event+' submitted');

   }catch(error){if(error.rejected){attempts[group]=null;throw error;}

    console.info('Overlay action: reply uncertain; confirming without replay',event);

    q('action-status').textContent='Waiting for native confirmation; the request will not be repeatedâ€¦';

   }

   const actual=await confirm(group,a);

   if(!a.submitted){completed.push(event+' selection confirmed');}

   if(save&&group==='render'){

    await rpc({op:'command',session,event:'Save',data:values.render,expected:actual});

    completed.push('Save submitted');

   }

  }

  if(outputRequest){attempted=true;await window.overlayResolution.apply(outputRequest);completed.push('Output resolution submitted');}
  q('action-status').textContent=(save?'Applied requested settings; startup save submitted. Check native save status below.':'Applied requested settings.')+(outputRequest?' Output resolution submitted; check rendered dimensions below.':'');

 }catch(error){q('action-status').textContent=error.message+(completed.length?' Completed requests: '+completed.join(', ')+'. Remaining steps stopped; review native status.':attempted?' Request outcome may be incomplete; review native status before retrying.':' No requests sent.');}

 finally{busy=false;availability();}

}

root.querySelector('[data-action="all-apply"]').onclick=()=>unified(false);

root.querySelector('[data-action="all-save"]').onclick=()=>unified(true);

// Never expose preview samples as live values before a valid publication arrives.

for(const el of root.querySelectorAll('input,select')){if(el.type==='checkbox')el.checked=false;else if(el.tagName==='SELECT')el.selectedIndex=-1;else if(el.type==='number')el.value='';}

async function poll(){try{const next=await rpc({op:'state'});const required=['Controls','Status','Label'];online=required.every(key=>next.fields[key]&&next.fields[key].ageMs<=2000);if(!online)throw Error('Waiting for fresh native renderer publications');if(state&&state.session!==next.session){dirty.render=false;pending.render=null;attempts.render=null;notice.render='New game session';}state=next;for(const group of ['render']){reconcile(group,state);const current=state.fields[field[group]].value;const p=pending[group];if(p){const matches=group==='render'?JSON.stringify(parseRender(current))===JSON.stringify(parseRender(p.data)):current===p.data;if(matches){pending[group]=null;dirty[group]=false;notice[group]='Native requested settings acknowledged; effective output is shown in native status.';}else if(Date.now()-p.time>3000){pending[group]=null;notice[group]='No matching native acknowledgement; review status before retrying.';}}if(!dirty[group]&&!busy&&!pending[group])load(group,current);}if(!busy)val('red',Number(state.fields.Label.value));labelStatus.textContent=state.fields.LabelStatus?.value??'';labelStatus.hidden=!labelStatus.textContent;if(q('connection-status'))q('connection-status').textContent=state.fields.Status.value.startsWith('ERROR')?state.fields.Status.value:'';q('native-status').textContent='Rendering: '+state.fields.Status.value+'\n'+(state.fields.Details?.value??'');if(!busy)q('render-status').textContent=(state.fields.Saved?.value??'')+'\n'+notice.render;}catch(error){online=false;if(q('connection-status'))q('connection-status').textContent='Disconnected: '+error.message;q('native-status').textContent='Disconnected: '+error.message;}availability();setTimeout(poll,500);}

// Native browser select popups are not part of offscreen screenshots. Use a

// DOM popup with the same selected control, labels and order for VR pointers.

let popup=null;

document.addEventListener('pointerdown',e=>{if(popup&&!popup.contains(e.target)){popup.remove();popup=null;}const select=e.target.closest('select');if(!select||select.disabled)return;e.preventDefault();popup=document.createElement('div');popup.setAttribute('role','listbox');const r=select.getBoundingClientRect(),css=getComputedStyle(select);Object.assign(popup.style,{position:'fixed',left:r.left+'px',top:Math.min(r.bottom,innerHeight-select.options.length*30)+'px',width:r.width+'px',zIndex:1000,background:css.backgroundColor,color:css.color,font:css.font,border:css.border,maxHeight:'70vh',overflow:'auto'});[...select.options].forEach((option,index)=>{const item=document.createElement('div');item.setAttribute('role','option');item.textContent=option.textContent;item.style.padding='5px 6px';item.onpointerenter=()=>item.style.background='var(--ue-selected)';item.onpointerleave=()=>item.style.background='';item.setAttribute('aria-disabled',String(option.disabled));if(option.disabled){item.style.opacity='.5';return popup.append(item);}item.onclick=()=>{select.selectedIndex=index;select.dispatchEvent(new Event('change',{bubbles:true}));popup.remove();popup=null;};popup.append(item);});root.append(popup);});

availability();poll();

