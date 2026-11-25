'use strict';
// Optional common widget. The renderer and native surface know nothing about it.
((root)=>{
 const max64=18446744073709551615n;
 const ranges=[[2,2],[0,1],[0,1],[1000,10000],[1000,10000],[0,5000],[0,1],[0,1],[2500,10000],[0,13],[0,13],[10000,20000],...[12,13,14,15].map(()=>[0,10000]),[0,1],[0,1],[0,1],[1000,10000],[1000,10000],[0,5000],[0,1],[0,2],[0,1],[0,10000],[0,10000],[0,1],[0,10000],[0,10000]];
 const fail=code=>{throw Error(code);};
 function revision(s,positive=false){if(typeof s!=='string'||s.length>1024||!/^\d+$/.test(s))fail('invalidPacket');const n=BigInt(s);if(n>max64||(positive&&n===0n))fail('invalidPacket');return n;}
 function id(s){return typeof s==='string'&&s.length>0&&new TextEncoder().encode(s).length<=128;}
 function parse(text){
  if(typeof text!=='string'||text.length>1024||!/^\d+(;\d+)*$/.test(text))fail('invalidConfig');
  let p=text.split(';');if(p.length===24&&BigInt(p[0])===1n)p=['2',...p.slice(1),'1','10000','5000','1','10000','5000'];
  if(p.length!==30||p.some((s,i)=>BigInt(s)<BigInt(ranges[i][0])||BigInt(s)>BigInt(ranges[i][1]))||[9,10].some(i=>![0,10,11,12,13].includes(Number(p[i]))))fail('invalidConfig');
  return p; // retain every untouched lexeme; never round-trip via floating point
 }
 function compatibility(d){
  if(!d)return 'noDescriptor';
  if(d.controlVersion!==1||d.sourceContract!==3||d.configWriteVersion!==2||!Array.isArray(d.configReadVersions)||!d.configReadVersions.includes(2)||d.backend!=='D3D11'||d.placement!=='fixed'||!['nasalOval','symmetric'].includes(d.geometryMode))return 'incompatibleVersion';
  if(!id(d.sessionId)||!Array.isArray(d.capabilities)||!Array.isArray(d.operations)||typeof d.available!=='boolean'||typeof d.hostId!=='string'||typeof d.hostVersion!=='string')return 'invalidDescriptor';
  return '';
 }
 const has=(d,c)=>d?.capabilities?.includes(c);
 function capabilities(d,v,old){
  v=v.map(Number);old=old.map(Number);
  const require=(condition,cap)=>{if(condition&&!has(d,cap))fail('unavailableCapability');};
  const changed=indices=>indices.some(i=>v[i]!==old[i]);
  require(true,'fixedFoveation');require(changed([5,21]),'transition');require(changed([11]),'centralSupersampling');
  require(changed([16,23]),'indicator');require(changed([17,18,19,20,21,22,27,28,29])||(v[23]!==old[23]&&v[23]!==0),'neuralRegions');
  require(changed([24,25,26,27,28,29]),'nasalHeightControls');
  for(const i of [2,18])require(v[i]!==old[i],['rectangle','ellipse'][v[i]]);
  require(v[7]!==old[7],['spatialPeriphery','temporalPeriphery'][v[7]]);
  if(v[1]){
   require(true,['rectangle','ellipse'][v[2]]);require(true,['spatialPeriphery','temporalPeriphery'][v[7]]);
   require(v[5]>0,'transition');require(v[11]!==10000,'centralSupersampling');require(!!v[16],'indicator');
   require(d.geometryMode==='nasalOval'&&!v[24],'nasalHeightControls');
   if(has(d,'neuralRegions')&&!v[17]){require(true,['rectangle','ellipse'][v[18]]);require(v[21]>0,'transition');require(d.geometryMode==='nasalOval'&&!v[27],'nasalHeightControls');}
   require(!!v[16]&&v[23]!==0,'neuralRegions');
  }
 }
 function geometry(parts,neural=false){const v=parts.map(Number),offset=neural&&!v[17]?27:24,width=neural&&!v[17]?19:3;const reduction=v[offset]?0:(10000-v[width])*v[offset+1]/10000;return {height:100-reduction/100,upper:reduction*v[offset+2]/1000000,lower:reduction*(10000-v[offset+2])/1000000};}
 class Model{
  constructor(){this.descriptor=null;this.snapshot=null;this.base=null;this.draft=null;this.pending=null;this.result=null;this.online=false;this.review=false;this.issue='noDescriptor';}
  get dirty(){return !!this.draft&&!!this.base&&this.draft.join(';')!==parse(this.base.config).join(';');}
  get conflict(){return !!this.base&&!!this.snapshot&&(this.base.sessionId!==this.snapshot.sessionId||revision(this.base.revision)!==revision(this.snapshot.revision)||revision(this.base.generalRevision)!==revision(this.snapshot.generalRevision));}
  load(){this.base=structuredClone(this.snapshot);this.draft=parse(this.snapshot.config);this.review=false;}
  ingest(envelope){
   const d=envelope?.descriptor,s=envelope?.snapshot,problem=compatibility(d);
   if(problem){if(this.dirty||this.pending)this.review=true;this.descriptor=d;this.online=false;this.issue=problem;return;}
   if(!s||s.sessionId!==d.sessionId)fail('wrongSession');
   const parts=parse(s.config);revision(s.revision);revision(s.generalRevision);
   const rt=s.runtime;if(!rt||typeof rt.active!=='boolean')fail('invalidSnapshot');
   const requested=revision(rt.requestedRevision),applied=revision(rt.appliedRevision);
   if(rt.active&&(!Number(parts[1])||applied===0n||requested!==applied||applied!==revision(s.revision)))fail('invalidSnapshot');
   const changed=this.base&&this.base.sessionId!==s.sessionId;
   const hadDraft=this.dirty||!!this.pending||this.review;
   this.descriptor=structuredClone(d);this.snapshot=structuredClone(s);this.online=true;this.issue='';
   if(changed){this.pending=null;this.result=null;if(hadDraft)this.review=true;else this.load();}
   const result=envelope.result||s.lastResult;
   if(result&&result.sessionId===s.sessionId&&(!this.pending||result.requestId===this.pending.requestId)){
    if(result.controlVersion!==1||!id(result.requestId)||!['queued','accepted','rejected'].includes(result.status)||!['notRequested','saved','failed'].includes(result.persistence))fail('invalidResult');
    revision(result.revision);this.result=structuredClone(result);
    if(this.pending&&result.requestId===this.pending.requestId&&result.status!=='queued'){
     if(result.status==='accepted'&&(revision(result.revision)<=revision(this.pending.packet.split('|')[0])||revision(s.revision)<revision(result.revision)))fail('invalidResult');
     if(result.status==='accepted'){this.pending=null;this.load();}
     else {this.pending=null;this.review=true;}
    }
   }
   if(!this.base||(!this.dirty&&!this.pending&&!this.review))this.load();
   if(this.draft){try{capabilities(d,this.draft,parse(s.config));}catch(e){this.issue=e.message;}}
  }
  disconnect(){this.online=false;this.issue='disconnected';if(this.dirty||this.pending)this.review=true;}
  edit(index,value){
   if(!this.online||compatibility(this.descriptor)||!this.descriptor.available||this.pending||!this.draft)fail('notReady');
   if(!Number.isInteger(index)||index<1||index>29||!Number.isInteger(value))fail('invalidConfig');
   const p=[...this.draft];p[index]=String(value);this.draft=parse(p.join(';'));
  }
  discard(){if(this.pending)fail('busy');if(!this.online||!this.snapshot)fail('notReady');this.load();this.result=null;}
  rebase(){if(this.pending)fail('busy');if(!this.online||!this.snapshot)fail('notReady');capabilities(this.descriptor,this.draft,parse(this.snapshot.config));this.base=structuredClone(this.snapshot);this.review=false;this.issue='';}
  capture(save,requestId){
   if(!this.online||!this.snapshot||!this.draft||this.descriptor.available!==true)fail('notReady');
   if(compatibility(this.descriptor))fail('incompatibleVersion');if(this.pending)fail('busy');
   if(this.review||this.conflict)fail('revisionConflict');
   if(!this.descriptor.operations.includes(save?'applySave':'apply'))fail('unsupportedOperation');
   revision(this.base.revision,true);revision(this.base.generalRevision,true);if(revision(this.base.revision)===max64)fail('notReady');
   capabilities(this.descriptor,this.draft,parse(this.snapshot.config));if(!id(requestId))fail('invalidPacket');
   const packet=this.base.revision+'|'+this.base.generalRevision+'|'+this.draft.join(';');if(packet.length>1100)fail('invalidPacket');
   return {controlVersion:1,sessionId:this.base.sessionId,requestId,event:save?'DLSS_FOVEADO.ApplySave':'DLSS_FOVEADO.Apply',packet};
  }
  begin(request){if(this.pending)fail('busy');this.pending=structuredClone(request);this.result=null;}
 }
 const messages={
  es:{enable:'Activar foveación fija',central:'Región central SR',neural:'Región Neural',shape:'Forma',width:'Campo horizontal',height:'Altura',transition:'Transición',axes:'Vincular ejes',lock:'Bloquear altura',ratio:'Proporción vertical',upper:'Reparto superior',lower:'Reparto inferior',linked:'Vincular Neural con SR',periphery:'Periferia',scale:'Escala periférica',centralPreset:'Preset central',peripheralPreset:'Preset periférico',super:'Supermuestreo central',show:'Mostrar indicador',indicator:'Regiones del indicador',advanced:'Posiciones fijas avanzadas',leftX:'Ojo izquierdo X',leftY:'Ojo izquierdo Y',rightX:'Ojo derecho X',rightY:'Ojo derecho Y',apply:'Aplicar',save:'Aplicar y guardar',discard:'Descartar',review:'Conservar borrador y usar revisiones actuales',draft:'Borrador sin aplicar',revisionConflict:'Cambios externos o nueva sesión: revisa el borrador o descártalo.',noDescriptor:'Esperando descriptor del host',invalidDescriptor:'Descriptor no válido',incompatibleVersion:'Versión de control, API o configuración incompatible',notReady:'Host no disponible o iniciando',unavailableCapability:'El borrador requiere una capacidad no disponible',unsupportedOperation:'Operación no disponible',invalidPacket:'Paquete o revisión no válido',invalidConfig:'Configuración no válida',wrongSession:'Sesión cambiada',busy:'Hay una solicitud pendiente',disconnected:'Desconectado: estado anterior no vigente',unknown:'Resultado desconocido; consultando sin reenviar',queued:'En cola; pendiente del punto seguro',accepted:'Configuración solicitada aceptada',rejected:'Solicitud rechazada',saved:'Guardado confirmado',failed:'Guardado fallido; se conserva la configuración solicitada',notRequested:'Guardado no solicitado',active:'Activa en ambos ojos',waiting:'Sin confirmación binocular activa',disabled:'Foveación desactivada',details:'Diagnóstico del host',rectangle:'Rectángulo',ellipse:'Elipse',spatial:'Espacial',temporal:'Temporal',inherit:'Heredar del host',both:'Ambas',retained:'Altura conservada',top:'Recorte superior',bottom:'Recorte inferior',hostControls:'Controles Neural del host',before:'Neural antes',after:'Neural después'},
  en:{enable:'Enable fixed foveation',central:'Central SR region',neural:'Neural region',shape:'Shape',width:'Horizontal field',height:'Height',transition:'Transition',axes:'Link axes',lock:'Lock height',ratio:'Vertical ratio',upper:'Upper share',lower:'Lower share',linked:'Link Neural to SR',periphery:'Periphery',scale:'Peripheral scale',centralPreset:'Central preset',peripheralPreset:'Peripheral preset',super:'Central supersampling',show:'Show indicator',indicator:'Indicator regions',advanced:'Advanced fixed positions',leftX:'Left eye X',leftY:'Left eye Y',rightX:'Right eye X',rightY:'Right eye Y',apply:'Apply',save:'Apply and save',discard:'Discard',review:'Keep draft and use current revisions',draft:'Unapplied draft',revisionConflict:'External changes or new session: review or discard the draft.',noDescriptor:'Waiting for host descriptor',invalidDescriptor:'Invalid descriptor',incompatibleVersion:'Incompatible control, API or configuration version',notReady:'Host unavailable or starting',unavailableCapability:'Draft requires an unavailable capability',unsupportedOperation:'Operation unavailable',invalidPacket:'Invalid packet or revision',invalidConfig:'Invalid configuration',wrongSession:'Session changed',busy:'A request is pending',disconnected:'Disconnected: previous state is stale',unknown:'Result unknown; querying without replay',queued:'Queued; awaiting safe point',accepted:'Requested configuration accepted',rejected:'Request rejected',saved:'Save confirmed',failed:'Save failed; requested configuration retained',notRequested:'Save not requested',active:'Active in both eyes',waiting:'No active binocular confirmation',disabled:'Foveation disabled',details:'Host diagnostics',rectangle:'Rectangle',ellipse:'Ellipse',spatial:'Spatial',temporal:'Temporal',inherit:'Inherit host',both:'Both',retained:'Retained height',top:'Upper crop',bottom:'Lower crop',hostControls:'Host Neural controls',before:'Neural before',after:'Neural after'}
 };
 function mount(target,options={}){
  if(typeof target==='string')target=document.querySelector(target);if(!target)throw Error('Missing FOVEATED_DLSS target');
  const model=new Model(),nodes=new Map(),groups=[],external=[];let lang=options.language||document.documentElement.lang||'en',stopped=false,timer=null,inflight=false,error='';
  const t=key=>options.translate?.(key,lang)||messages[lang]?.[key]||messages.en[key]||key;
  const element=(tag,parent,text)=>{const el=document.createElement(tag);if(text)el.textContent=text;parent.append(el);return el;};
  const labels=[];const label=(el,key)=>{labels.push([el,key]);el.textContent=t(key);};
  const style=element('style',target);style.textContent='.fov-panel{font:14px sans-serif}.fov-panel fieldset{border:1px solid #555;margin:10px 0;padding:10px;min-width:0}.fov-row{display:grid;grid-template-columns:minmax(150px,1fr) minmax(90px,1fr) 70px;align-items:center;gap:8px;margin:10px 0}.fov-row input[type=range]{width:100%;min-width:0}.fov-panel button{padding:8px;margin:4px}.fov-panel select{min-width:0;max-width:100%}.fov-panel [hidden]{display:none!important}.fov-status{white-space:pre-wrap;overflow-wrap:anywhere}.fov-actions{position:sticky;top:0;background:#20252b;padding:8px;z-index:1}.fov-panel :disabled{opacity:.55}.fov-panel summary{cursor:pointer}';
  target.classList.add('fov-panel');element('h2',target,'FOVEATED_DLSS');
  const notice=element('div',target);notice.className='fov-status';notice.setAttribute('role','status');notice.dataset.fov='notice';
  const actions=element('div',target);actions.className='fov-actions';const buttons={};
  for(const key of ['apply','save','discard','review']){const b=element('button',actions);b.type='button';b.dataset.fov=key;label(b,key);buttons[key]=b;}
  const actual=element('div',target);actual.className='fov-status';actual.dataset.fov='runtime';
  function group(key,predicate=()=>true){const el=element('fieldset',target);label(element('legend',el),key);groups.push([el,predicate]);return el;}
  function field(parent,index,key,kind='range',items=null,condition=()=>true){
   const row=element('div',parent);row.className='fov-row';row.dataset.field=String(index);const lbl=element('label',row);label(lbl,key);
   const input=element(kind==='select'?'select':'input',row);input.id='fov-'+index;lbl.htmlFor=input.id;
   if(kind==='select')for(const [value,text] of items){const o=element('option',input);o.value=value;label(o,text);}else input.type=kind;
   const out=element('output',row);out.htmlFor=input.id;
   if(kind==='range'){input.min=ranges[index][0];input.max=ranges[index][1];input.step=1;}
   input.addEventListener(kind==='range'?'input':'change',()=>{try{model.edit(index,kind==='checkbox'?Number(input.checked):Number(input.value));error='';}catch(e){error=e.message;}render();});
   nodes.set(index,{row,input,out,kind,condition});return input;
  }
  field(target,1,'enable','checkbox');
  function region(key,neural){
   const shape=neural?18:2,width=neural?19:3,height=neural?20:4,transition=neural?21:5,axes=neural?22:6,lock=neural?27:24;
   const parent=group(key,d=>!neural||has(d,'neuralRegions'));
   if(neural)field(parent,17,'linked','checkbox');
   const independent=v=>!neural||!Number(v[17]);
   field(parent,shape,'shape','select',[[0,'rectangle'],[1,'ellipse']],(d,v)=>independent(v));
   field(parent,width,'width','range',null,(d,v)=>independent(v));
   field(parent,height,'height','range',null,(d,v)=>independent(v)&&d.geometryMode==='symmetric');
   field(parent,axes,'axes','checkbox',null,(d,v)=>independent(v)&&d.geometryMode==='symmetric');
   field(parent,transition,'transition','range',null,(d,v)=>independent(v)&&has(d,'transition'));
   field(parent,lock,'lock','checkbox',null,(d,v)=>independent(v)&&d.geometryMode==='nasalOval'&&has(d,'nasalHeightControls'));
   for(const [i,k] of [[lock+1,'ratio'],[lock+2,'upper']])field(parent,i,k,'range',null,(d,v)=>independent(v)&&d.geometryMode==='nasalOval'&&has(d,'nasalHeightControls')&&!Number(v[lock]));
   // Lower share is a view of the upper field, never a 31st persistent field.
   const lower=element('div',parent);lower.className='fov-row';const ll=element('label',lower);label(ll,'lower');
   const li=element('input',lower);li.type='range';li.min=0;li.max=10000;li.step=1;li.id='fov-lower-'+key;ll.htmlFor=li.id;const lo=element('output',lower);
   li.oninput=()=>{try{model.edit(lock+2,10000-Number(li.value));error='';}catch(e){error=e.message;}render();};
   const summary=element('p',parent);summary.dataset.fov='geometry-'+key;
   external.push(()=>{const d=model.descriptor||{},v=model.draft;if(!v)return;lower.hidden=d.geometryMode!=='nasalOval';li.disabled=!editable()||!independent(v)||!has(d,'nasalHeightControls')||!!Number(v[lock]);li.value=10000-Number(v[lock+2]);lo.textContent=(Number(li.value)/100).toFixed(2)+'%';const g=geometry(v,neural);summary.hidden=d.geometryMode!=='nasalOval';summary.textContent=`${t('retained')}: ${g.height.toFixed(2)}% · ${t('top')}: ${g.upper.toFixed(2)}% · ${t('bottom')}: ${g.lower.toFixed(2)}%`;});
  }
  region('central',false);
  const processing=group('periphery');
  const presets=[[0,'Auto'],[10,'J'],[11,'K'],[12,'L'],[13,'M']];
  field(processing,10,'centralPreset','select',[[0,'inherit'],...presets.slice(1)]);
  field(processing,11,'super','range',null,d=>has(d,'centralSupersampling'));
  field(processing,7,'periphery','select',[[0,'spatial'],[1,'temporal']]);
  field(processing,8,'scale');field(processing,9,'peripheralPreset','select',presets,(d,v)=>Number(v[7])===1&&has(d,'temporalPeriphery'));
  region('neural',true);
  const indicators=group('indicator',d=>has(d,'indicator'));
  field(indicators,16,'show','checkbox');field(indicators,23,'indicator','select',[[0,'SR'],[1,'Neural'],[2,'both']]);
  const advanced=element('details',target);label(element('summary',advanced),'advanced');for(const [i,k] of [[12,'leftX'],[13,'leftY'],[14,'rightX'],[15,'rightY']])field(advanced,i,k);
  // These slots are owned by a consumer's existing controls; they never enter Config.
  const hostSlots=[];
  for(const name of ['before','after']){const fn=options.neuralControls?.[name];if(typeof fn!=='function')continue;const slot=group(name,d=>has(d,'neuralRegions')&&has(d,'neural'+name[0].toUpperCase()+name.slice(1)));hostSlots.push({slot,name,fn,controller:null});}
  const log=element('details',target);label(element('summary',log),'details');const detail=element('pre',log);detail.className='fov-status';detail.dataset.fov='details';
  function editable(){return model.online&&!compatibility(model.descriptor)&&model.descriptor.available&&has(model.descriptor,'fixedFoveation')&&!model.pending&&!!model.draft&&revision(model.snapshot.revision)>0n&&revision(model.snapshot.generalRevision)>0n&&model.descriptor.operations.some(o=>['apply','applySave'].includes(o));}
  function render(){
   for(const [el,key] of labels)el.textContent=t(key);
   const d=model.descriptor||{},v=model.draft,usable=editable();
   const navigation=options.navigationButton||document.querySelector('[data-overlay-page="foveated_dlss"]');
   if(navigation){navigation.disabled=!model.online||!!compatibility(model.descriptor)||!d.available||!has(d,'fixedFoveation');navigation.setAttribute('aria-disabled',String(navigation.disabled));}
   for(const [el,predicate] of groups){el.hidden=!d||!predicate(d);el.disabled=!usable;}
   for(const [i,n] of nodes){n.input.disabled=!usable||!v||!n.condition(d,v);if(!v)continue;
    if(n.kind==='checkbox')n.input.checked=!!Number(v[i]);else n.input.value=v[i];n.out.textContent=n.kind==='range'?(Number(v[i])/100).toFixed(2)+'%':'';
    n.row.hidden=([4,6,20,22].includes(i)&&d.geometryMode==='nasalOval')||([24,25,26,27,28,29].includes(i)&&d.geometryMode==='symmetric');
    if(n.kind==='select')for(const opt of n.input.options){const x=Number(opt.value);opt.disabled=([2,18].includes(i)&&!has(d,['rectangle','ellipse'][x]))||(i===7&&!has(d,['spatialPeriphery','temporalPeriphery'][x]))||(i===23&&x!==0&&!has(d,'neuralRegions'));}
   }
   for(const update of external)update();
   for(const entry of hostSlots){if(!entry.slot.hidden&&!entry.controller)entry.controller=entry.fn(entry.slot,{model})||{};entry.controller?.availability?.(usable&&!entry.slot.hidden);}
   const can=save=>{try{model.capture(save,'preflight');return true;}catch{return false;}};
   buttons.apply.disabled=inflight||!can(false);buttons.save.disabled=inflight||!can(true);
   buttons.discard.disabled=inflight||!model.online||!!model.pending||!model.snapshot;
   buttons.review.hidden=!(model.review||model.conflict);buttons.review.disabled=inflight||!model.online||!!model.pending;
   const state=[error&&t(error),model.issue&&t(model.issue),d&&!d.available&&(d.unavailableReason||t('notReady')),(model.review||model.conflict)&&t('revisionConflict'),model.dirty&&t('draft')].filter(Boolean);
   if(model.pending)state.push(t(model.result?.status==='queued'?'queued':'unknown'));
   if(model.result&&!model.pending)state.push(t(model.result.status),model.result.code&&t(model.result.code),t(model.result.persistence),model.result.message);
   notice.textContent=state.filter(Boolean).join('\n');
   const rt=model.snapshot?.runtime;
   actual.textContent=rt?`${model.online?'':t('disconnected')+'\n'}${t(model.online&&rt.active?'active':v&&!Number(parse(model.snapshot.config)[1])?'disabled':'waiting')}\nrequested: ${rt.requestedRevision} · applied: ${rt.appliedRevision}`:'';
   detail.textContent=[d&&`${d.hostId} ${d.hostVersion} · engine ${d.engineVersion} · control ${d.controlVersion}`,rt?.message,rt?.details].filter(Boolean).join('\n');
   target.dispatchEvent(new CustomEvent('foveateddraftchange',{bubbles:true}));
  }
  const token=document.querySelector('meta[name=overlay-token]')?.content;
  async function rpc(request){if(options.exchange)return options.exchange(request);const abort=new AbortController(),timeout=setTimeout(()=>abort.abort(),options.timeoutMs||5000);try{const r=await fetch('/api',{method:'POST',headers:{'Content-Type':'application/json','X-Overlay-Token':token},body:JSON.stringify(request),signal:abort.signal});const value=await r.json();if(!r.ok||value.error)throw Error('disconnected');return value;}finally{clearTimeout(timeout);}}
  async function refresh(){if(stopped||inflight)return;inflight=true;try{model.ingest(await rpc({op:'foveated_state',requestId:model.pending?.requestId}));error='';}catch(e){model.disconnect();error=e.message;}finally{inflight=false;render();}}
  async function apply(save){if(inflight)return;inflight=true;try{
   // One fresh preflight, followed by host-side comparison again at Submit/Tick.
   model.ingest(await rpc({op:'foveated_state'}));const request=model.capture(save,crypto.randomUUID());model.begin(request);error='';render();
   model.ingest(await rpc({op:'foveated_command',...request}));
  }catch(e){error=e.message;if(model.pending)model.disconnect();}finally{inflight=false;render();}}
  buttons.apply.onclick=()=>apply(false);buttons.save.onclick=()=>apply(true);
  buttons.discard.onclick=()=>{try{model.discard();error='';}catch(e){error=e.message;}render();};
  buttons.review.onclick=()=>{try{model.rebase();error='';}catch(e){error=e.message;}render();};
  const language=e=>{lang=e.detail?.language||e.detail||document.documentElement.lang;render();};document.addEventListener('overlaylanguagechange',language);
  async function poll(){await refresh();if(!stopped)timer=setTimeout(poll,options.pollMs||500);}
  render();poll();
  return {model,refresh,apply,setLanguage(value){lang=value;render();},destroy(){stopped=true;clearTimeout(timer);document.removeEventListener('overlaylanguagechange',language);for(const x of hostSlots)x.controller?.destroy?.();}};
 }
 const api={Model,parse,revision,compatibility,capabilities,geometry,mount,ranges};
 if(typeof module!=='undefined'&&module.exports)module.exports=api;
 else root.OverlayFoveated=api;
})(globalThis);
