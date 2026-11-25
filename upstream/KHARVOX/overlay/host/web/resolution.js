'use strict';
// Shared output-resolution UI. The host transport supplies the game's adapter;
// this module has no UEVR, game, INI, scale or memory-layout knowledge.
(()=>{
 const target=document.getElementById('pv-output-resolution');if(!target)return;
 const token=document.querySelector('meta[name=overlay-token]').content;
 target.innerHTML='<div class="heading">OpenXR output resolution</div><div class="range"><button type="button" id="output-minus" aria-label="Decrease output width by 50 pixels">−</button><input id="output-width" type="range" step="50" aria-label="Output width per eye"><button type="button" id="output-plus" aria-label="Increase output width by 50 pixels">+</button></div><output id="output-preview"></output><div id="output-effective" class="status"></div><div id="output-notice" class="status" aria-live="polite"></div><hr>';
 const q=id=>document.getElementById('output-'+id),slider=q('width');
 let current=null,base=null,dirty=false,busy=false,online=false,pending=null,outerBusy=false;
 const changed=()=>document.dispatchEvent(new Event('outputdraftchange'));
 async function rpc(path,data){const r=await fetch('/output/'+path,{method:'POST',headers:{'Content-Type':'application/json','X-Overlay-Token':token},body:data?JSON.stringify(data):''});const result=await r.json();if(!r.ok||result.error)throw Error(result.error||'Output adapter unavailable');return result;}
 function valid(s){return s?.protocol===1&&s.available&&Number.isInteger(s.width)&&s.width>0&&Number.isInteger(s.height)&&s.height>0&&typeof s.session==='string';}
 function load(s){base={...s};dirty=false;const step=50;
  const min=s.width+Math.ceil(((s.minWidth||s.width)-s.width)/step)*step;
  const max=s.width+Math.floor(((s.maxWidth||s.width)-s.width)/step)*step;
  slider.min=Math.min(min,s.width);slider.max=Math.max(max,s.width);slider.value=s.width;
 }
 function render(){
  const usable=online&&valid(current)&&current.canWrite&&!busy&&!outerBusy;
  slider.disabled=!usable;q('minus').disabled=!usable||Number(slider.value)<=Number(slider.min);q('plus').disabled=!usable||Number(slider.value)>=Number(slider.max);
  if(base){const width=Number(slider.value),height=Math.round(width*base.height/base.width);q('preview').textContent=`${width} × ${height} px per eye${dirty?' · draft':''}`;}else q('preview').textContent='Waiting for output dimensions';
  if(valid(current)){
   q('effective').textContent=`${current.authority||'OpenXR'} selected output: ${current.width} × ${current.height} px per eye`;
   const reconstruction=current.reconstruction;
   if(reconstruction?.available)q('effective').textContent+=`\n${reconstruction.mode} input: ${reconstruction.inputWidth} × ${reconstruction.inputHeight} → output: ${reconstruction.outputWidth} × ${reconstruction.outputHeight} px per eye`;
   else if(reconstruction)q('effective').textContent+='\nRenderer: '+reconstruction.reason;
   const submitted=current.submittedViews;if(submitted?.available)q('effective').textContent+='\nSubmitted output: '+submitted.views.map((v,i)=>`${i+1}: ${v.width} × ${v.height}`).join(' · ');
   else if(submitted)q('effective').textContent+='\nRendered views: '+(submitted.reason||'Waiting for an OpenXR frame');
   if(!dirty&&!pending)q('notice').textContent=current.reason||(current.saved?'Startup value saved.':current.saveRequested?'Startup save requested; awaiting confirmation.':'Current value is not confirmed saved.');
  }else q('effective').textContent=current?.reason||'Output adapter unavailable';
 }
 function edit(value){if(!base||slider.disabled)return;slider.value=Math.max(Number(slider.min),Math.min(Number(slider.max),value));dirty=Number(slider.value)!==base.width;render();changed();}
 slider.addEventListener('input',()=>edit(Number(slider.value)));q('minus').onclick=()=>edit(Number(slider.value)-50);q('plus').onclick=()=>edit(Number(slider.value)+50);
 const control={
  get dirty(){return dirty||!!pending;},get pending(){return !!pending;},
  availability(value){outerBusy=!!value;render();},
  discard(){pending=null;if(valid(current))load(current);q('notice').textContent='';render();changed();},
  capture(save){if(pending)throw Error('Previous output result is uncertain; read current state or Discard before another request');if(!dirty&&!(save&&valid(current)&&current.canWrite&&!current.saved))return null;
   if(!online||!valid(current)||!current.canWrite||(save&&!current.canSave))throw Error('Output resolution cannot be changed or saved in this session');
   return {session:base.session,expected:base.expected,width:Number(slider.value),save:!!save};},
  async preflight(request){if(!request)return;const s=await rpc('state');if(!valid(s)||!s.canWrite||s.session!==request.session||s.expected!==request.expected)throw Error('Output resolution changed externally; discard or review draft');},
  async apply(request){if(!request)return;busy=true;pending={...request};render();
   try{const result=await rpc('apply',request);if(!result.submitted)throw Error('Output request was not submitted');
    if(valid(result.selection)&&result.selection.width===request.width){current=result.selection;if(Number(slider.value)===request.width)load(current);else base={...current};pending=null;}
    q('notice').textContent='Output selection acknowledged; awaiting rendered views.'+(request.save?' Startup save requested.':'');
   }catch(error){q('notice').textContent=error.message+' Read current state before retrying; no automatic replay.';throw error;}
   finally{busy=false;render();changed();}
  }
 };
 window.overlayResolution=control;
 async function poll(){try{
  const s=await rpc('state');online=true;
  if(valid(s)){
   if(base&&base.session!==s.session){pending=null;load(s);q('notice').textContent='New output session';}
   if(pending&&pending.session===s.session&&s.width===pending.width&&s.expected!==pending.expected){
    if(Number(slider.value)===pending.width)load(s);else base={...s};pending=null;q('notice').textContent='Previous output selection confirmed; no request repeated.';
   }
   current=s;if(!base||(!dirty&&!busy&&!pending))load(s);
  }else {current=s;if(!dirty){base=null;pending=null;}}
 }catch(error){online=false;current={available:false,reason:error.message};}
 render();changed();setTimeout(poll,500);}
 render();poll();
})();
