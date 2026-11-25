'use strict';
let busy=false;
const apply=document.getElementById('apply'),discard=document.getElementById('discard'),result=document.getElementById('result');
function availability(){apply.disabled=busy||!window.overlayResolution?.dirty;discard.disabled=busy||!window.overlayResolution?.dirty;window.overlayResolution?.availability(busy);}
document.addEventListener('outputdraftchange',availability);
discard.onclick=()=>{window.overlayResolution.discard();result.textContent='Draft discarded';};
apply.onclick=async()=>{busy=true;availability();try{const request=window.overlayResolution.capture(false);await window.overlayResolution.preflight(request);await window.overlayResolution.apply(request);result.textContent='Demo selection acknowledged; no game was changed.';}catch(error){result.textContent=error.message;}finally{busy=false;availability();}};
availability();
