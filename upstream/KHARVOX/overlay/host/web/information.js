'use strict';
// Independent read-only information: never updates settings or Apply's draft.
(()=>{
 const output=document.getElementById('pv-headset-info');
 const token=document.querySelector('meta[name=overlay-token]').content;
 const profiles={touch_controller:'Touch',index_controller:'Valve Index',motion_controller:'Microsoft Motion',vive_controller:'Vive / compatible trackpad',pico4_controller:'PICO 4',touch_controller_plus:'Touch Plus'};
 const profile=value=>value?(profiles[value.split('/').pop()]||value):'Not reported';
 async function refresh(){
  try{
   const response=await fetch('/xr/info',{method:'POST',headers:{'X-Overlay-Token':token}});
   if(!response.ok)throw Error('OpenXR information unavailable');
   const info=await response.json();
   if(!info.available){output.textContent=info.reason||'Waiting for an active OpenXR session';return;}
   output.textContent=[`Headset / OpenXR system: ${info.system||'Model not identified by runtime'}`,
    'System name reported by OpenXR; exact headset model may not be identified.',
    `Runtime: ${info.runtime||'Not reported'} ${info.runtimeVersion||''}`,
    `Graphics: ${info.graphics||'Unknown'}`,
    `Controller profiles (may be emulated):\nLeft: ${profile(info.leftProfile)} · Right: ${profile(info.rightProfile)}`].join('\n');
  }catch(error){output.textContent='OpenXR information unavailable';}
  finally{setTimeout(refresh,1000);}
 }
 refresh();
})();
