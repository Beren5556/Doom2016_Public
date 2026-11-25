'use strict';
((root)=>{
 const v2=typeof module!=='undefined'&&module.exports?require('./foveated_v2.js'):root.OverlayFoveatedV2;
 const clone=v=>structuredClone(v),same=(a,b)=>JSON.stringify(a)===JSON.stringify(b),fail=s=>{throw Error(s);};
 function profile(p,mode){
  if(!p||typeof p.config!=='string')fail('Invalid mode profile');const c=v2.parse(p.config).map(Number),s=p.settings;
  if(!s||s.version!==1||s.mode!==mode||typeof s.centralDlaa!=='boolean'||!Number.isInteger(s.targetMicros)||s.targetMicros<5000||s.targetMicros>50000)fail('Invalid experimental settings');
  if(!Number.isInteger(s.sustainedOverloadMs)||s.sustainedOverloadMs<100||s.sustainedOverloadMs>10000)fail('Sustained overload must be between 100 and 10000 ms.');
  if(!Number.isInteger(s.recoveryDelayMs)||s.recoveryDelayMs<100||s.recoveryDelayMs>10000)fail('Recovery delay must be between 100 and 10000 ms.');
  for(const key of ['inner','outer'])if(!Array.isArray(s[key])||s[key].length!==4||s[key].some(x=>!Number.isInteger(x)||x<0||x>4500||x%100))fail('Invalid bounds');
  if(s.inner.some((x,i)=>x<s.outer[i])||c[1]!==+(mode!==0)||s.centralDlaa&&(c[8]!==1||c[9]>=10000))fail('Incompatible mode profile');
  return {config:c.map(String).join(';'),settings:clone(s)};
 }
 class Model {
  constructor(){this.snapshot=null;this.descriptor=null;this.drafts=[];this.bases=[];this.selected=0;this.selectionEdited=false;this.online=false;this.pending=null;this.error='Waiting for native host';this.result=null;}
  get current(){return this.drafts[this.selected];}
  policy(){for(const p of this.drafts)if(p)p.settings.centralDlaa=false;}
  get dirty(){return !!this.current&&(this.selected!==this.snapshot?.experimental.selectedMode||!same(this.current,this.snapshot?.experimental.profiles[this.selected]));}
  get blocked(){return this.descriptor?.generalMode!==3||this.descriptor?.neuralActive===true;}
  get fixedBlocked(){return ![3,4].includes(this.descriptor?.generalMode);}
  get conflict(){const b=this.bases[this.selected],s=this.snapshot;return !!b&&(!s||b.sessionId!==s.sessionId||b.revision!==s.revision||b.generalRevision!==s.generalRevision);}
  base(mode=this.selected){return {sessionId:this.snapshot.sessionId,revision:this.snapshot.revision,generalRevision:this.snapshot.generalRevision,profile:clone(this.snapshot.experimental.profiles[mode])};}
  ingest(e,queriedRequestId=null){
   const d=e?.descriptor,s=e?.snapshot;
   if(v2.compatibility(d)||d.experimentalVersion!==1||d.dynamicWithNeural!==false||!s||s.sessionId!==d.sessionId)fail('Experimental host contract unavailable');
   v2.revision(s.revision,true);v2.revision(s.generalRevision);const exp=s.experimental;
   if(!exp||exp.version!==1||!Number.isInteger(exp.selectedMode)||exp.selectedMode<0||exp.selectedMode>2||!Array.isArray(exp.profiles)||exp.profiles.length!==3||!Array.isArray(exp.presets)||exp.presets.length!==3)fail('Invalid experimental snapshot');
   exp.profiles.forEach((p,i)=>profile(p,i));exp.presets.forEach((p,i)=>{if(p)profile(p,i);});
   const previous=this.snapshot,first=!previous,sessionChanged=previous&&previous.sessionId!==s.sessionId;
   const clean=this.drafts.map((p,i)=>same(p,previous?.experimental.profiles[i]));
   this.descriptor=clone(d);this.snapshot=clone(s);this.online=true;this.error='';
   if(first||sessionChanged||!this.selectionEdited)this.selected=exp.selectedMode;
   const result=Object.hasOwn(e,'result')?e.result:s.lastResult;
   if(this.pending&&sessionChanged){this.pending=null;this.error='Session changed. Review or discard your draft.';}
   // A fresh, serialized result query is authoritative: the host has no queued
   // or completed command with this ID. Never replay an uncertain mutation,
   // but do not keep every control locked after a request was never admitted.
   if(this.pending&&queriedRequestId===this.pending.requestId&&e.result===null){
    this.pending=null;this.error='The request did not reach the renderer. Your draft is retained; Apply to try again.';
   }
   if(this.pending&&result?.requestId===this.pending.requestId&&result.sessionId===s.sessionId){
    if(result.controlVersion!==2||!['queued','accepted','rejected'].includes(result.status)||!['notRequested','saved','failed'].includes(result.persistence))fail('Invalid native result');
    this.result=clone(result);
    if(result.status!=='queued'){
     const mode=this.pending.experimental.mode;
     if(result.status==='accepted'){
      if(v2.revision(result.revision)<=v2.revision(this.pending.packet.split('|')[0])||v2.revision(s.revision)<v2.revision(result.revision))fail('Invalid acknowledgement revision');
      this.drafts[mode]=clone(exp.profiles[mode]);this.bases[mode]=this.base(mode);this.selectionEdited=false;this.selected=exp.selectedMode;
     }else this.error=result.code||'Request rejected';
     this.pending=null;
    }
   }
   for(let i=0;i<3;i++){
    if(!this.drafts[i]||clean[i]&&!this.pending){this.drafts[i]=clone(exp.profiles[i]);this.bases[i]=this.base(i);}
    else if(!this.pending&&!sessionChanged&&this.bases[i]?.sessionId===s.sessionId&&same(this.bases[i].profile,exp.profiles[i])){
     // Switching another mode or a readiness/size update is not a conflicting
     // edit of this profile. Retain its draft and use the current authority.
     this.bases[i]=this.base(i);
    }
   }
   this.policy();
  }
  edit(fn){if(!this.online||!this.current||this.pending)fail('Native host unavailable or request pending');fn(this.current);this.policy();}
  select(i){if(!Number.isInteger(i)||i<0||i>2||this.pending)fail('Invalid mode selection');this.selected=i;this.selectionEdited=i!==this.snapshot?.experimental.selectedMode;this.result=null;this.policy();}
  discard(){if(this.pending)fail('Request pending');this.drafts[this.selected]=clone(this.snapshot.experimental.profiles[this.selected]);this.bases[this.selected]=this.base();this.error='';}
  review(){if(this.pending||!this.online)fail('Host unavailable');profile(this.current,this.selected);this.bases[this.selected]=this.base();this.error='';}
  restore(){const p=this.snapshot?.experimental.presets[this.selected];if(p)this.edit(d=>Object.assign(d,clone(p)));}
  capture(save,id){
   if(this.selected&&this.fixedBlocked)fail('Foveation requires DLSS 4.5 or DLSS5. DLAA is not supported.');
   const recovery=this.selected===0;
   if(!this.online||!this.current||!(this.descriptor?.available||recovery&&this.descriptor?.recoveryAvailable))fail('Host not ready');
   if(this.pending)fail('Request pending');if(this.conflict)fail('Settings changed. Review or discard the draft.');
   this.policy();profile(this.current,this.selected);const c=v2.parse(this.current.config);
   if(this.selected===2&&this.blocked)fail('Dynamic requires DLSS 4.5. Neural / DLSS5 supports Fixed only.');
   v2.capabilities(this.descriptor,c,v2.parse(this.snapshot.experimental.profiles[this.selected].config));
   if(this.current.settings.centralDlaa&&!this.descriptor.experimentalCapabilities?.includes('centralDlaa'))fail('Central DLAA unavailable');
   const b=this.bases[this.selected];v2.revision(b.generalRevision,true);
   return {controlVersion:2,sessionId:b.sessionId,requestId:id,event:save?'DLSS_FOVEADO.ApplySave':'DLSS_FOVEADO.Apply',packet:b.revision+'|'+b.generalRevision+'|'+this.current.config,experimental:clone(this.current.settings)};
  }
 }
 function mount(target,options={}){
  if(typeof target==='string')target=document.querySelector(target);
  if(!target||!root.OverlayExperimentalView)fail('Experimental view unavailable');
  target.innerHTML=root.OverlayExperimentalView.replace('Central DLAA / peripheral DLSS · Experimental','Central DLAA / peripheral DLSS · From NVIDIA_DLSS');target.classList.add('overlay-panel');
  const $=id=>target.querySelector('#exp-'+id),model=new Model(),controls=[];
  let stopped=false,inflight=false,timer=null,lastProjection=null,projectionSession=null,refreshTask=null,applyRequested=false;
  const values=()=>model.current?model.current.config.split(';').map(Number):null;
  function edit(fn){try{model.edit(p=>{const c=p.config.split(';').map(Number);fn(c,p.settings);p.config=c.join(';');});model.error='';}catch(e){model.error=e.message;}render();}
  function control(parent,id,label,kind,index,{get,set,min=0,max=45,choices=[],enabled=()=>true}={}){
   const row=document.createElement('div');row.className='fov-row';const text=document.createElement('label');text.htmlFor='exp-'+id;text.textContent=label;row.append(text);
   const input=document.createElement(kind==='select'?'select':'input');input.id='exp-'+id;input.setAttribute('aria-label',label);let buttons=[],out;
   if(kind==='select')for(const [value,label]of choices){const o=document.createElement('option');o.value=value;o.textContent=label;input.append(o);}else input.type=kind;
   const read=()=>get?get():values()?.[index],write=v=>edit((c,s)=>set?set(c,s,v):c[index]=v);
   const low=()=>typeof min==='function'?min():min,high=()=>typeof max==='function'?max():max;
   if(kind==='range'){
    input.step='1';const box=document.createElement('div');box.className='fov-range';
    for(const [name,step]of [['Decrease',-1],['Increase',1]]){const b=document.createElement('button');b.type='button';b.textContent=step<0?'−':'+';b.setAttribute('aria-label',name+' '+label+' by 1 percentage point');b.onclick=()=>{if(input.disabled)return;write(Math.max(low(),Math.min(high(),Math.round(read()/100)+step))*100);};buttons.push(b);}
    box.append(buttons[0],input,buttons[1]);row.append(box);out=document.createElement('output');out.htmlFor=input.id;row.append(out);
   }else row.append(input);
   input.addEventListener(kind==='range'?'input':'change',()=>write(kind==='checkbox'?+input.checked:kind==='range'?Math.max(low(),Math.min(high(),Math.round(+input.value)))*100:+input.value));
   $(parent).append(row);controls.push(()=>{
    const v=read()??0;input.disabled=!model.online||!!model.pending||!model.selected||!enabled();
    if(kind==='checkbox')input.checked=!!v;else{if(kind==='range'){input.min=low();input.max=high();input.value=Math.round(v/100);out.textContent=Math.round(v/100)+'%';}else input.value=v;}
    buttons.forEach((b,i)=>b.disabled=input.disabled||(i?v/100>=high():v/100<=low()));
   });
  }
  const shapes=[[0,'Rectangle'],[1,'Ellipse']],presets=[[0,'Auto'],[10,'J'],[11,'K'],[12,'L'],[13,'M']];
  control('central-controls','shape','Shape','select',2,{choices:shapes});
  for(const [i,name]of [[0,'Top'],[1,'Bottom'],[3,'Left'],[2,'Right']]){
   control('fixed-controls','fixed-'+i,name,'range',3+i);
   control('inner-controls','min-'+i,name,'range',null,{get:()=>model.current?.settings.inner[i],set:(c,s,v)=>s.inner[i]=v,min:()=>model.current?.settings.outer[i]/100||0});
   control('outer-controls','max-'+i,name,'range',null,{get:()=>model.current?.settings.outer[i],set:(c,s,v)=>s.outer[i]=v,max:()=>model.current?model.current.settings.inner[i]/100:45});
  }
  control('transition-control','transition','Transition','range',7,{max:50});
  control('processing-controls','central-preset','Central preset','select',11,{choices:[[0,'Inherit host'],...presets.slice(1)]});
  control('processing-controls','supersample','Central supersampling','range',12,{min:100,max:200});
  control('processing-controls','periphery','Periphery','select',8,{choices:[[0,'Spatial'],[1,'Temporal']],enabled:()=>!model.current?.settings.centralDlaa});
  control('processing-controls','peripheral-scale','Peripheral scale','range',9,{min:25,max:()=>model.current?.settings.centralDlaa?99:100});
  control('processing-controls','peripheral-preset','Peripheral preset','select',10,{choices:presets,enabled:()=>values()?.[8]===1});
  control('neural-controls','linked','Link Neural to SR','checkbox',14);
  control('neural-controls','neural-shape','Shape','select',15,{choices:shapes,get:()=>values()?.[values()?.[14]?2:15],enabled:()=>!values()?.[14]});
  for(const [i,name]of [[0,'Top'],[1,'Bottom'],[3,'Left'],[2,'Right']])control('neural-controls','neural-'+i,name,'range',16+i,{get:()=>values()?.[(values()?.[14]?3:16)+i],enabled:()=>!values()?.[14]});
  control('neural-controls','neural-transition','Transition','range',20,{max:50,get:()=>values()?.[values()?.[14]?7:20],enabled:()=>!values()?.[14]});
  control('indicator-controls','indicator','Indicator regions','select',21,{choices:[[0,'SR'],[1,'Neural'],[2,'Both']]});
  const review=document.createElement('button');review.type='button';review.textContent='Keep my changes';review.onclick=()=>{try{model.review();}catch(e){model.error=e.message;}render();};$('discard').after(review);
  const svgNS='http://www.w3.org/2000/svg';
  function node(tag,attrs){const e=document.createElementNS(svgNS,tag);for(const [k,v]of Object.entries(attrs))e.setAttribute(k,v);return e;}
  function draw(svg,eye){
   const liveProjection=model.snapshot?.experimental.telemetry?.projection;
   if(projectionSession!==model.snapshot?.sessionId){lastProjection=null;projectionSession=model.snapshot?.sessionId;}
   if(liveProjection?.width>0&&liveProjection?.height>0&&liveProjection.maps?.length===2&&liveProjection.maps.every(m=>Array.isArray(m)&&m.length===9&&m.every(Number.isFinite)))lastProjection=liveProjection;
   const projection=lastProjection;
   const measured=projection?.width>0&&projection?.height>0&&projection.maps?.length===2;
   // Stable dimensions also while the page is hidden. Measuring hidden SVGs
   // produced tiny viewBoxes, and eye-dependent heights shrank the fusion tile.
   const w=240,h=192,p=9;let W=w-2*p,H=h-2*p,x0=p,y0=p;
   svg.setAttribute('viewBox',`0 0 ${w} ${h}`);svg.setAttribute('preserveAspectRatio','xMidYMid meet');svg.style.aspectRatio='5 / 4';svg.removeAttribute('height');svg.replaceChildren();
   svg.append(node('rect',{x:p,y:p,width:W,height:H,rx:3,fill:'var(--ue-control)',stroke:'var(--ue-line)'}));
   const c=values(),s=model.current?.settings;if(!c||!model.selected)return;
   // Both eyes use the same output-to-common transform as the native shader.
   // No artificial half-oval, stretch, nasal anchor or guessed headset frustum.
   let inverse=null;
   if(eye<2&&measured){
    const [a,b,c,d,e,f,g,h1,i]=projection.maps[eye];
    inverse=[e*i-f*h1,c*h1-b*i,b*f-c*e,f*g-d*i,a*i-c*g,c*d-a*f,d*h1-e*g,b*g-a*h1,a*e-b*d];
    const determinant=a*inverse[0]+b*inverse[3]+c*inverse[6];
    if(!Number.isFinite(determinant)||Math.abs(determinant)<1e-10)inverse=null;
    else{const scale=Math.min(W/projection.width,H/projection.height);W=projection.width*scale;H=projection.height*scale;x0=(w-W)/2;y0=(h-H)/2;}
   }
   const project=([x,y])=>{if(!inverse)return [x0+x*W,y0+y*H];const m=inverse,z=m[6]*x+m[7]*y+m[8];return [x0+(m[0]*x+m[1]*y+m[2])/z*W,y0+(m[3]*x+m[4]*y+m[5])/z*H];};
   const clipId=svg.id+'-clip',clip=node('clipPath',{id:clipId});clip.append(node('rect',{x:x0,y:y0,width:W,height:H}));svg.append(clip);
   const layer=node('g',{'clip-path':`url(#${clipId})`});svg.append(layer);
   function contour(cuts,shape,color,role,dashed=false){
    const [t,b,r,l]=cuts.map(x=>.5-x/10000);let points=[];
    if(!shape)points=[[.5-l,.5-t],[.5+r,.5-t],[.5+r,.5+b],[.5-l,.5+b]];
    else{const start=0,span=2*Math.PI;for(let i=0;i<=96;i++){const a=start+i/96*span,x=Math.cos(a),y=Math.sin(a);points.push([.5+x*(x>=0?r:l),.5+y*(y>=0?b:t)]);}}
    layer.append(node('path',{d:points.map(project).map(([x,y],i)=>(i?'L':'M')+x.toFixed(2)+' '+y.toFixed(2)).join(' ')+' Z',fill:'none',stroke:color,'stroke-width':role==='active'?2:3.5,'stroke-dasharray':dashed?'5 4':'none','data-contour':role}));
   }
   const rt=model.snapshot?.runtime,tel=model.snapshot?.experimental.telemetry;
   if(model.selected===2){contour(s.inner,c[2],'#ff6577','minimum',true);contour(s.outer,c[2],'#ff6577','maximum');}
   else contour(c.slice(3,7),c[2],'#5de994','draft');
   if(model.online&&rt?.active&&tel?.active&&model.snapshot.experimental.selectedMode===model.selected&&Array.isArray(tel.cuts))contour(tel.cuts,Number(model.snapshot.experimental.profiles[model.selected].config.split(';')[2]),'#5de994','active');
   if(model.descriptor?.neuralActive&&!c[14])contour(c.slice(16,20),c[15],'#82bfff','neural',true);
  }
  function render(){
   const tab=document.querySelector('[data-overlay-page="foveated_dlss"]');if(tab)tab.disabled=!model.snapshot;
   const c=values(),s=model.current?.settings,off=model.selected===0,dyn=model.selected===2;
   $('mode').value=['off','fixed','dynamic'][model.selected];$('mode').disabled=!model.online||!!model.pending;$('mode').querySelector('[value="dynamic"]').disabled=model.blocked;$('mode').querySelector('[value="fixed"]').disabled=model.fixedBlocked;
   $('fixed').hidden=dyn;$('dynamic').hidden=!dyn;$('legend').hidden=!dyn;$('neural-legend').hidden=!model.descriptor?.neuralActive||!c||!!c[14];
   $('dlaa').checked=false;$('dlaa').disabled=true;$('dlaa').closest('label').hidden=true;$('show').checked=!!c?.[13];$('show').disabled=!model.online||!!model.pending;
   const period=model.snapshot?.experimental.telemetry?.framePeriodMs; $('target').value=period>0?(1000/period).toFixed(1):'';$('target').placeholder='Waiting for runtime';
   $('compatibility').hidden=!model.blocked;$('compatibility').textContent=model.fixedBlocked?'Foveation requires DLSS 4.5 or DLSS5. DLAA is not supported. Saved presets are retained.':'Dynamic requires DLSS 4.5. Neural / DLSS5 supports Fixed only; Dynamic settings are retained.';
   let reason='';try{model.capture(false,'preflight');}catch(e){reason=e.message;}
   $('apply').disabled=$('save').disabled=applyRequested||!!reason;$('restore').disabled=!model.online||!!model.pending||!model.snapshot?.experimental.presets[model.selected];
   $('discard').disabled=!model.online||!!model.pending;$('reset').disabled=off||!model.online||!!model.pending;review.hidden=!model.conflict;review.disabled=!!model.pending;
   $('error').textContent=reason;$('state').textContent=model.error||(model.pending?'Waiting for native acknowledgement':model.result?.status==='rejected'?model.result.code:model.dirty?'Unapplied draft':model.snapshot?.runtime.message||'Waiting for native host');
   if(model.result?.status==='accepted')$('state').textContent+=' · '+(model.result.persistence==='saved'?'Preset saved':model.result.persistence==='failed'?'Save failed; applied selection retained':'Requested settings accepted');
   const tel=model.snapshot?.experimental.telemetry,active=model.online&&model.snapshot?.runtime.active&&tel?.active;
   $('live').textContent=active?`${tel.status} · Active crop T / B / R / L: ${tel.cuts.map(x=>(x/100).toFixed(0)+'%').join(' / ')}`:off?'Foveation disabled':'Settings preview · Waiting for active stereo confirmation';
   $('extent').textContent=dyn&&tel?.gpuMs>0?'GPU stereo interval: '+tel.gpuMs.toFixed(2)+' ms · Effective target: '+(tel.targetMs??s.targetMicros/1000).toFixed(2)+' ms · Includes waits':s?.centralDlaa?'Native scene input required · Supersampling increases central processing size':'';
   if(dyn&&tel?.changeCpuMs>0)$('extent').textContent+=' · Last region change CPU: '+tel.changeCpuMs.toFixed(2)+' ms';
   $('neural-extent').textContent=c?.[14]?'Follows the central SR region':'Independent Neural region';
   controls.forEach(fn=>fn());for(const [id,eye]of [['left-eye',0],['right-eye',1],['fusion',2]])draw($(id),eye);
   document.dispatchEvent(new CustomEvent('foveatedcompatibilitychange'));
  }
  $('mode').onchange=e=>{try{model.select(['off','fixed','dynamic'].indexOf(e.target.value));}catch(error){model.error=error.message;}render();};
  $('show').onchange=e=>edit(c=>c[13]=+e.target.checked);

  $('restore').onclick=()=>{try{model.restore();}catch(e){model.error=e.message;}render();};$('discard').onclick=()=>{model.discard();render();};
  $('reset').onclick=()=>edit((c,s)=>{c[2]=c[15]=1;c[7]=c[20]=1000;c[14]=1;for(let i=0;i<4;i++)c[16+i]=2000;if(model.selected===2){s.inner=[3000,3000,3000,3000];s.outer=[1000,1000,1000,1000];}else for(let i=0;i<4;i++)c[3+i]=2000;});
  async function rpc(request){if(options.rpc)return options.rpc(request);const response=await fetch('/api',{method:'POST',headers:{'Content-Type':'application/json','X-Overlay-Token':document.querySelector('meta[name=overlay-token]').content},body:JSON.stringify(request)});const data=await response.json();if(!response.ok||data.error)fail(data.error||'Host unavailable');return data;}
  function refresh(){if(refreshTask)return refreshTask;if(inflight||applyRequested||stopped)return Promise.resolve();inflight=true;const id=model.pending?.requestId;
   refreshTask=(async()=>{try{model.ingest(await rpc({op:'foveated_state',requestId:id}),id);}catch(e){model.online=false;model.error=e.message;}finally{inflight=false;refreshTask=null;render();}})();return refreshTask;
  }
  async function apply(save){if(applyRequested||model.pending||stopped)return;applyRequested=true;render();
   // A click during a slow state read is retained, never silently discarded.
   if(refreshTask)await refreshTask;if(stopped){applyRequested=false;return;}inflight=true;try{
   model.ingest(await rpc({op:'foveated_state'}));const request=model.capture(save,crypto.randomUUID());model.pending=clone(request);render();
   model.ingest(await rpc({op:'foveated_command',...request}));
  }catch(e){model.error=e.message;if(model.pending)model.error+='; querying result without replay';}finally{inflight=false;applyRequested=false;render();}}
  $('apply').onclick=()=>apply(false);$('save').onclick=()=>apply(true);
  async function poll(){try{await refresh();}catch(e){model.error=e.message;console.error('Foveation view:',e);}finally{if(!stopped)timer=setTimeout(poll,options.pollMs||250);}}
  const pageChanged=e=>{if(e.detail==='foveated_dlss'&&model.snapshot&&!model.pending){model.selected=model.snapshot.experimental.selectedMode;model.selectionEdited=false;render();refresh();}};document.addEventListener('overlaypagechange',pageChanged);
  render();poll();return {model,refresh,apply,generalConflict(mode){return [model.snapshot?.experimental.selectedMode,model.selected].some(selected=>selected>0&&(mode!==3&&(mode!==4||selected===2)));},destroy(){document.removeEventListener('overlaypagechange',pageChanged);stopped=true;clearTimeout(timer);}};
 }
 const api={Model,profile,mount};if(typeof module!=='undefined'&&module.exports)module.exports=api;else root.OverlayFoveatedExperimental=api;
})(globalThis);
