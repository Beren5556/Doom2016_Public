'use strict';
// Control 2 only. Explicit separate path; never mount against control 1.
// foveated.js supplies legacy snapshot validation, not automatic conversion.
((root)=>{
 const legacy=typeof module!=='undefined'&&module.exports?require('./foveated.js'):root.OverlayFoveated;
 const max64=18446744073709551615n;
 const ranges=[[3,3],[0,1],[0,1],...[3,4,5,6].map(()=>[0,4500]),[0,5000],[0,1],[2500,10000],[0,13],[0,13],[10000,20000],[0,1],[0,1],[0,1],...[16,17,18,19].map(()=>[0,4500]),[0,5000],[0,2]];
 const fail=code=>{throw Error(code);};
 function revision(s,positive=false){if(typeof s!=='string'||s.length>1024||!/^\d+$/.test(s))fail('invalidPacket');const n=BigInt(s);if(n>max64||(positive&&n===0n))fail('invalidPacket');return n;}
 function id(s){return typeof s==='string'&&s.length>0&&new TextEncoder().encode(s).length<=128;}
 function parse(text){
  if(typeof text!=='string'||text.length>1024||!/^\d+(;\d+)*$/.test(text))fail('invalidConfig');
  const p=text.split(';');
  if(p.length!==22||p.some((s,i)=>BigInt(s)<BigInt(ranges[i][0])||BigInt(s)>BigInt(ranges[i][1]))||[10,11].some(i=>![0,10,11,12,13].includes(Number(p[i]))))fail('invalidConfig');
  return p;
 }
 function parseSnapshot(text){return typeof text==='string'&&/^0*3;/.test(text)?parse(text):legacy.parse(text);}
 function draftForSnapshot(s){
  const v=parseSnapshot(s.config);if(Number(v[0])===3)return v;
  if(s.migration?.kind!=='legacyToFourSides'||s.migration.requiresApply!==true)fail('missingMigration');
  try{return parse(s.migration.draftConfig);}catch{fail('missingMigration');}
 }
 function compatibility(d){
  if(!d)return 'noDescriptor';
  if(d.controlVersion!==2||d.sourceContract!==4||d.configWriteVersion!==3||!Array.isArray(d.configReadVersions)||![1,2,3].every(v=>d.configReadVersions.includes(v))||!['D3D11','Vulkan'].includes(d.backend)||d.placement!=='fixed'||d.geometryMode!=='binocularFourSides')return 'incompatibleVersion';
  if(!id(d.sessionId)||!Array.isArray(d.capabilities)||!Array.isArray(d.operations)||typeof d.available!=='boolean'||typeof d.hostId!=='string'||typeof d.hostVersion!=='string')return 'invalidDescriptor';
  return '';
 }
 const has=(d,c)=>d?.capabilities?.includes(c);
 function capabilities(d,v,old){
  v=v.map(Number);old=old.map(Number);
  const require=(condition,cap)=>{if(condition&&!has(d,cap))fail('unavailableCapability');};
  const changed=indices=>indices.some(i=>v[i]!==old[i]);
  for(const cap of ['fixedFoveation','fourSideRegions','binocularProjection'])require(true,cap);
  require(changed([7,20]),'transition');require(changed([12]),'centralSupersampling');
  require(changed([13,21]),'indicator');require(changed([14,15,16,17,18,19,20])||(v[21]!==old[21]&&v[21]!==0),'neuralRegions');
  for(const i of [2,15])require(v[i]!==old[i],['rectangle','ellipse'][v[i]]);
  require(v[8]!==old[8],['spatialPeriphery','temporalPeriphery'][v[8]]);
  if(v[1]){
   require(true,['rectangle','ellipse'][v[2]]);require(true,['spatialPeriphery','temporalPeriphery'][v[8]]);
   require(v[7]>0,'transition');require(v[12]!==10000,'centralSupersampling');require(!!v[13],'indicator');
   if(has(d,'neuralRegions')&&!v[14]){require(true,['rectangle','ellipse'][v[15]]);require(v[20]>0,'transition');}
   require(!!v[13]&&v[21]!==0,'neuralRegions');
  }
 }
 // Only percentages/radii in the abstract common plane; never fabricate eye maps.
 function geometry(parts,neural=false){const v=parts.map(Number),i=neural&&!v[14]?16:3;const [top,bottom,right,left]=v.slice(i,i+4).map(b=>.5-b/10000);return {top,bottom,right,left,width:(right+left)*100,height:(top+bottom)*100};}
 class Model{
  constructor(){this.descriptor=null;this.snapshot=null;this.base=null;this.draft=null;this.pending=null;this.result=null;this.online=false;this.review=false;this.issue='noDescriptor';}
  get dirty(){return !!this.draft&&!!this.baseline&&this.draft.join(';')!==this.baseline.join(';');}
  get migration(){return !!this.snapshot&&Number(parseSnapshot(this.snapshot.config)[0])!==3;}
  get ready(){try{draftForSnapshot(this.snapshot);return true;}catch{return false;}}
  get conflict(){return !!this.base&&!!this.snapshot&&(this.base.sessionId!==this.snapshot.sessionId||revision(this.base.revision)!==revision(this.snapshot.revision)||revision(this.base.generalRevision)!==revision(this.snapshot.generalRevision));}
  load(){this.base=structuredClone(this.snapshot);try{this.baseline=draftForSnapshot(this.snapshot);this.draft=[...this.baseline];}catch{this.baseline=null;this.draft=null;this.issue='missingMigration';}this.review=false;}
  ingest(envelope){
   const d=envelope?.descriptor,s=envelope?.snapshot,problem=compatibility(d);
   if(problem){if(this.dirty||this.pending)this.review=true;this.descriptor=d;this.online=false;this.issue=problem;return;}
   if(!s||s.sessionId!==d.sessionId)fail('wrongSession');
   const parts=parseSnapshot(s.config);revision(s.revision);revision(s.generalRevision);
   const rt=s.runtime;if(!rt||typeof rt.active!=='boolean')fail('invalidSnapshot');
   const requested=revision(rt.requestedRevision),applied=revision(rt.appliedRevision);
   if(rt.active&&(!Number(parts[1])||applied===0n||requested!==applied||applied!==revision(s.revision)))fail('invalidSnapshot');
   const changed=this.base&&this.base.sessionId!==s.sessionId;
   const hadDraft=this.dirty||!!this.pending||this.review;
   this.descriptor=structuredClone(d);this.snapshot=structuredClone(s);this.online=true;this.issue='';
   if(changed){this.pending=null;this.result=null;if(hadDraft)this.review=true;else this.load();}
   const result=envelope.result||s.lastResult;
   if(result&&result.sessionId===s.sessionId&&(!this.pending||result.requestId===this.pending.requestId)){
    if(result.controlVersion!==2||!id(result.requestId)||!['queued','accepted','rejected'].includes(result.status)||!['notRequested','saved','failed'].includes(result.persistence))fail('invalidResult');
    revision(result.revision);this.result=structuredClone(result);
    if(this.pending&&result.requestId===this.pending.requestId&&result.status!=='queued'){
     if(result.status==='accepted'&&(revision(result.revision)<=revision(this.pending.packet.split('|')[0])||revision(s.revision)<revision(result.revision)))fail('invalidResult');
     if(result.status==='accepted'){this.pending=null;this.load();}
     else {this.pending=null;this.review=true;}
    }
   }
   if(!this.base||(!this.dirty&&!this.pending&&!this.review))this.load();
   if(this.draft){try{capabilities(d,this.draft,draftForSnapshot(s));}catch(e){this.issue=e.message;}}
  }
  disconnect(){this.online=false;this.issue='disconnected';if(this.dirty||this.pending)this.review=true;}
  edit(index,value){
   if(!this.online||compatibility(this.descriptor)||!this.descriptor.available||!this.ready||this.pending||!this.draft)fail('notReady');
   if(!Number.isInteger(index)||index<1||index>21||!Number.isInteger(value))fail('invalidConfig');
   const p=[...this.draft];p[index]=String(value);this.draft=parse(p.join(';'));
  }
  discard(){if(this.pending)fail('busy');if(!this.online||!this.snapshot)fail('notReady');this.load();this.result=null;}
  rebase(){if(this.pending)fail('busy');if(!this.online||!this.snapshot)fail('notReady');capabilities(this.descriptor,this.draft,draftForSnapshot(this.snapshot));this.base=structuredClone(this.snapshot);this.review=false;this.issue='';}
  capture(save,requestId){
   if(!this.online||!this.snapshot||!this.draft||this.descriptor.available!==true)fail('notReady');
   if(compatibility(this.descriptor))fail('incompatibleVersion');if(this.pending)fail('busy');
   if(this.review||this.conflict)fail('revisionConflict');
   if(!this.descriptor.operations.includes(save?'applySave':'apply'))fail('unsupportedOperation');
   revision(this.base.revision,true);revision(this.base.generalRevision,true);if(revision(this.base.revision)===max64)fail('notReady');
   capabilities(this.descriptor,this.draft,draftForSnapshot(this.snapshot));if(!id(requestId))fail('invalidPacket');
   const packet=this.base.revision+'|'+this.base.generalRevision+'|'+this.draft.join(';');if(packet.length>1100)fail('invalidPacket');
   return {controlVersion:2,sessionId:this.base.sessionId,requestId,event:save?'DLSS_FOVEADO.ApplySave':'DLSS_FOVEADO.Apply',packet};
  }
  begin(request){if(this.pending)fail('busy');this.pending=structuredClone(request);this.result=null;}
 }
 const messages={
  es:{enable:'Activar foveación fija',central:'Región central SR',neural:'Región Neural',shape:'Forma',width:'Campo horizontal',height:'Altura',transition:'Transición',axes:'Vincular ejes',lock:'Bloquear altura',ratio:'Proporción vertical',upper:'Reparto superior',lower:'Reparto inferior',linked:'Vincular Neural con SR',periphery:'Periferia',scale:'Escala periférica',centralPreset:'Preset central',peripheralPreset:'Preset periférico',super:'Supermuestreo central',show:'Mostrar indicador',indicator:'Regiones del indicador',advanced:'Posiciones fijas avanzadas',leftX:'Ojo izquierdo X',leftY:'Ojo izquierdo Y',rightX:'Ojo derecho X',rightY:'Ojo derecho Y',apply:'Aplicar',save:'Aplicar y guardar',discard:'Descartar',review:'Conservar borrador y usar revisiones actuales',draft:'Borrador sin aplicar',revisionConflict:'Cambios externos o nueva sesión: revisa el borrador o descártalo.',noDescriptor:'Esperando descriptor del host',invalidDescriptor:'Descriptor no válido',incompatibleVersion:'Versión de control, API o configuración incompatible',notReady:'Host no disponible o iniciando',unavailableCapability:'El borrador requiere una capacidad no disponible',unsupportedOperation:'Operación no disponible',invalidPacket:'Paquete o revisión no válido',invalidConfig:'Configuración no válida',wrongSession:'Sesión cambiada',busy:'Hay una solicitud pendiente',disconnected:'Desconectado: estado anterior no vigente',unknown:'Resultado desconocido; consultando sin reenviar',queued:'En cola; pendiente del punto seguro',accepted:'Configuración solicitada aceptada',rejected:'Solicitud rechazada',saved:'Guardado confirmado',failed:'Guardado fallido; se conserva la configuración solicitada',notRequested:'Guardado no solicitado',active:'Activa en ambos ojos',waiting:'Sin confirmación binocular activa',disabled:'Foveación desactivada',details:'Diagnóstico del host',rectangle:'Rectángulo',ellipse:'Elipse',spatial:'Espacial',temporal:'Temporal',inherit:'Heredar del host',both:'Ambas',retained:'Altura conservada',top:'Recorte superior',bottom:'Recorte inferior',hostControls:'Controles Neural del host',before:'Neural antes',after:'Neural después'},
  en:{enable:'Enable fixed foveation',central:'Central SR region',neural:'Neural region',shape:'Shape',width:'Horizontal field',height:'Height',transition:'Transition',axes:'Link axes',lock:'Lock height',ratio:'Vertical ratio',upper:'Upper share',lower:'Lower share',linked:'Link Neural to SR',periphery:'Periphery',scale:'Peripheral scale',centralPreset:'Central preset',peripheralPreset:'Peripheral preset',super:'Central supersampling',show:'Show indicator',indicator:'Indicator regions',advanced:'Advanced fixed positions',leftX:'Left eye X',leftY:'Left eye Y',rightX:'Right eye X',rightY:'Right eye Y',apply:'Apply',save:'Apply and save',discard:'Discard',review:'Keep draft and use current revisions',draft:'Unapplied draft',revisionConflict:'External changes or new session: review or discard the draft.',noDescriptor:'Waiting for host descriptor',invalidDescriptor:'Invalid descriptor',incompatibleVersion:'Incompatible control, API or configuration version',notReady:'Host unavailable or starting',unavailableCapability:'Draft requires an unavailable capability',unsupportedOperation:'Operation unavailable',invalidPacket:'Invalid packet or revision',invalidConfig:'Invalid configuration',wrongSession:'Session changed',busy:'A request is pending',disconnected:'Disconnected: previous state is stale',unknown:'Result unknown; querying without replay',queued:'Queued; awaiting safe point',accepted:'Requested configuration accepted',rejected:'Request rejected',saved:'Save confirmed',failed:'Save failed; requested configuration retained',notRequested:'Save not requested',active:'Active in both eyes',waiting:'No active binocular confirmation',disabled:'Foveation disabled',details:'Host diagnostics',rectangle:'Rectangle',ellipse:'Ellipse',spatial:'Spatial',temporal:'Temporal',inherit:'Inherit host',both:'Both',retained:'Retained height',top:'Upper crop',bottom:'Lower crop',hostControls:'Host Neural controls',before:'Neural before',after:'Neural after'}
 };
 Object.assign(messages.es,{top:'Superior',bottom:'Inferior',right:'Derecha',left:'Izquierda',migration:'Configuración anterior activa; aplica los cuatro lados para cambiarla. La propuesta no garantiza equivalencia visual.',missingMigration:'Falta un borrador de migración válido del host.',fourSides:'Contorno en el plano binocular común',extent:'Extensión del contorno',projection:'Proyección binocular no disponible'});
 Object.assign(messages.en,{top:'Top',bottom:'Bottom',right:'Right',left:'Left',migration:'Previous configuration remains active; apply four sides to change it. The proposal does not guarantee visual equivalence.',missingMigration:'A valid host migration draft is missing.',fourSides:'Contour in the common binocular plane',extent:'Contour extent',projection:'Binocular projection unavailable'});
 function mount(target,options={}){
  if(typeof target==='string')target=document.querySelector(target);if(!target)throw Error('Missing FOVEATED_DLSS target');
  const model=new Model(),nodes=new Map(),groups=[],external=[];let lang=options.language||'en',stopped=false,timer=null,inflight=false,error='';
  const t=key=>options.translate?.(key,lang)||messages[lang]?.[key]||messages.en[key]||key;
  const element=(tag,parent,text)=>{const el=document.createElement(tag);if(text)el.textContent=text;parent.append(el);return el;};
  const labels=[];const label=(el,key)=>{labels.push([el,key]);el.textContent=t(key);};
  // Apply the same compact theme to the existing views; their behavior stays owned by their widgets.
  for(const view of document.querySelectorAll('[data-overlay-view],#pv-actions'))view.classList.add('overlay-panel');
  for(const tab of document.querySelectorAll('[data-overlay-page]'))tab.classList.add('overlay-tab');
  document.getElementById('pv-actions')?.classList.add('overlay-actions');
  const style=element('style',target);style.textContent=`
   .overlay-panel{font:14px sans-serif;min-width:0;overflow-wrap:anywhere}
   .overlay-panel h2,.overlay-panel .heading{font-size:1.5em;font-weight:bold;margin:.83em 0}
   .overlay-panel fieldset{border:1px solid #555;margin:10px 0;padding:10px;min-width:0}
   .overlay-panel button,.overlay-panel select,.overlay-panel input{font:inherit}
   .overlay-panel button,.overlay-tab{padding:8px;margin:4px;min-width:36px;min-height:36px}
   .overlay-tab{font:14px sans-serif}
   .overlay-panel select{min-width:0;max-width:100%}
   .overlay-panel .range{display:flex;align-items:center;gap:12px;min-width:0}
   .overlay-panel .range input[type=range]{flex:1;min-width:0}
   .overlay-panel[hidden],.overlay-panel [hidden]{display:none!important}
   .overlay-panel .status,.fov-status{white-space:pre-wrap;overflow-wrap:anywhere}
   .overlay-panel :disabled{opacity:.55}
   .overlay-panel summary{cursor:pointer}
   .overlay-actions{position:sticky;top:var(--overlay-sticky-top,0px);background:#20252b;padding:8px;z-index:1}
   .overlay-actions p:empty{display:none}
   .fov-row{display:grid;grid-template-columns:minmax(130px,1fr) minmax(0,2fr) 70px;align-items:center;gap:8px;margin:10px 0}
   .fov-row input[type=range]{width:100%;min-width:0;margin:0}
   .overlay-panel .fov-range{display:grid;grid-template-columns:auto minmax(0,1fr) auto;gap:4px}
   .fov-range button{margin:0}
   @media(max-width:480px){.fov-row{grid-template-columns:minmax(0,1fr) 70px}.fov-row>label{grid-column:1/-1}}
  `;
  target.classList.add('fov-panel','overlay-panel');element('h2',target,'FOVEATED_DLSS');
  const notice=element('div',target);notice.className='fov-status';notice.setAttribute('role','status');notice.dataset.fov='notice';
  const actions=element('div',target);actions.className='fov-actions overlay-actions';const buttons={};
  const header=target.closest('#ac7-panel-preview')?.querySelector(':scope>.title');
  const headerSize=header&&new ResizeObserver(()=>{const offset=getComputedStyle(header).position==='sticky'?header.getBoundingClientRect().height:0;for(const bar of [actions,document.getElementById('pv-actions')])bar?.style.setProperty('--overlay-sticky-top',offset+'px');});
  headerSize?.observe(header);
  for(const key of ['apply','save','discard','review']){const b=element('button',actions);b.type='button';b.dataset.fov=key;label(b,key);buttons[key]=b;}
  const actual=element('div',target);actual.className='fov-status';actual.dataset.fov='runtime';
  function group(key,predicate=()=>true){const el=element('fieldset',target);label(element('legend',el),key);groups.push([el,predicate]);return el;}
  function field(parent,index,key,kind='range',items=null,condition=()=>true){
   const row=element('div',parent);row.className='fov-row';row.dataset.field=String(index);const lbl=element('label',row);label(lbl,key);
   const control=kind==='range'?element('div',row):row;if(kind==='range')control.className='range fov-range';
   const minus=kind==='range'?element('button',control,'−'):null;
   const input=element(kind==='select'?'select':'input',control);input.id='fov-'+index;lbl.htmlFor=input.id;
   if(kind==='select')for(const [value,text] of items){const o=element('option',input);o.value=value;label(o,text);}else input.type=kind;
   const out=element('output',row);out.htmlFor=input.id;
   const plus=kind==='range'?element('button',control,'+'):null;
   if(kind==='range'){
    input.min=ranges[index][0];input.max=ranges[index][1];input.step=1;
    const adjust=delta=>{if(input.disabled)return;input.value=Math.max(Number(input.min),Math.min(Number(input.max),Number(input.value)+delta));input.dispatchEvent(new Event('input',{bubbles:true}));};
    for(const [button,delta,name] of [[minus,-100,'minus'],[plus,100,'plus']]){button.type='button';button.id=`fov-${index}-${name}`;button.style.minWidth='36px';button.onclick=()=>adjust(delta);}
    input.addEventListener('keydown',e=>{if(['ArrowLeft','ArrowDown','ArrowRight','ArrowUp'].includes(e.key)){e.preventDefault();adjust(['ArrowLeft','ArrowDown'].includes(e.key)?-100:100);}});
   }
   input.addEventListener(kind==='range'?'input':'change',()=>{try{model.edit(index,kind==='checkbox'?Number(input.checked):Number(input.value));error='';}catch(e){error=e.message;}render();});
   nodes.set(index,{row,input,out,kind,condition,minus,plus,key});return input;
  }
  field(target,1,'enable','checkbox');
  function region(key,neural){
   const shape=neural?15:2,first=neural?16:3,transition=neural?20:7;
   const parent=group(key,d=>!neural||has(d,'neuralRegions'));
   if(neural)field(parent,14,'linked','checkbox');
   const independent=v=>!neural||!Number(v[14]);
   field(parent,shape,'shape','select',[[0,'rectangle'],[1,'ellipse']],(d,v)=>independent(v));
   for(const [offset,name] of [[0,'top'],[1,'bottom'],[3,'left'],[2,'right']])field(parent,first+offset,name,'range',null,(d,v)=>independent(v));
   field(parent,transition,'transition','range',null,(d,v)=>independent(v)&&has(d,'transition'));
   const summary=element('p',parent);summary.dataset.fov='geometry-'+key;
   external.push(()=>{if(!model.draft)return;const g=geometry(model.draft,neural);summary.textContent=`${t('extent')}: ${g.width.toFixed(2)}% × ${g.height.toFixed(2)}%`});
  }
  region('central',false);
  const processing=group('periphery');
  const presets=[[0,'Auto'],[10,'J'],[11,'K'],[12,'L'],[13,'M']];
  field(processing,11,'centralPreset','select',[[0,'inherit'],...presets.slice(1)]);
  field(processing,12,'super','range',null,d=>has(d,'centralSupersampling'));
  field(processing,8,'periphery','select',[[0,'spatial'],[1,'temporal']]);
  field(processing,9,'scale');field(processing,10,'peripheralPreset','select',presets,(d,v)=>Number(v[8])===1&&has(d,'temporalPeriphery'));
  region('neural',true);
  const indicators=group('indicator',d=>has(d,'indicator'));
  field(indicators,13,'show','checkbox');field(indicators,21,'indicator','select',[[0,'SR'],[1,'Neural'],[2,'both']]);
  // These slots are owned by a consumer's existing controls; they never enter Config.
  const hostSlots=[];
  for(const name of ['before','after']){const fn=options.neuralControls?.[name];if(typeof fn!=='function')continue;const slot=group(name,d=>has(d,'neuralRegions')&&has(d,'neural'+name[0].toUpperCase()+name.slice(1)));hostSlots.push({slot,name,fn,controller:null});}
  const log=element('details',target);label(element('summary',log),'details');const detail=element('pre',log);detail.className='fov-status';detail.dataset.fov='details';
  function editable(){return model.online&&!compatibility(model.descriptor)&&model.descriptor.available&&['fixedFoveation','fourSideRegions','binocularProjection'].every(c=>has(model.descriptor,c))&&model.ready&&!model.pending&&!!model.draft&&revision(model.snapshot.revision)>0n&&revision(model.snapshot.generalRevision)>0n&&model.descriptor.operations.some(o=>['apply','applySave'].includes(o));}
  function render(){
   for(const [el,key] of labels)el.textContent=t(key);
   const d=model.descriptor||{},v=model.draft,usable=editable();
   const navigation=options.navigationButton||document.querySelector('[data-overlay-page="foveated_dlss"]');
   if(navigation){navigation.disabled=!model.online||!!compatibility(model.descriptor)||!d.available||!['fixedFoveation','fourSideRegions','binocularProjection'].every(c=>has(d,c))||!model.ready;navigation.setAttribute('aria-disabled',String(navigation.disabled));navigation.title=navigation.disabled?(d.unavailableReason||t(model.issue||(!model.ready?'missingMigration':'unavailableCapability'))):'';}
   for(const [el,predicate] of groups){el.hidden=!d||!predicate(d);el.disabled=!usable;}
   for(const [i,n] of nodes){n.input.disabled=!usable||!v||!n.condition(d,v);
    if(n.kind==='range')for(const [b,decrease] of [[n.minus,true],[n.plus,false]]){
     b.disabled=n.input.disabled||(decrease?Number(v?.[i])<=ranges[i][0]:Number(v?.[i])>=ranges[i][1]);
     b.setAttribute('aria-label',`${lang==='es'?(decrease?'Reducir':'Aumentar'):(decrease?'Decrease':'Increase')} ${t(n.key)} ${lang==='es'?'en 1 punto porcentual':'by 1 percentage point'}`);
    }
    if(!v)continue;
    if(n.kind==='checkbox')n.input.checked=!!Number(v[i]);else n.input.value=v[i];n.out.textContent=n.kind==='range'?(Number(v[i])/100).toFixed(2)+'%':'';
    if(n.kind==='select')for(const opt of n.input.options){const x=Number(opt.value);opt.disabled=([2,15].includes(i)&&!has(d,['rectangle','ellipse'][x]))||(i===8&&!has(d,['spatialPeriphery','temporalPeriphery'][x]))||(i===21&&x!==0&&!has(d,'neuralRegions'));}
   }
   for(const update of external)update();
   for(const entry of hostSlots){if(!entry.slot.hidden&&!entry.controller)entry.controller=entry.fn(entry.slot,{model})||{};entry.controller?.availability?.(usable&&!entry.slot.hidden);}
   const can=save=>{try{model.capture(save,'preflight');return true;}catch{return false;}};
   buttons.apply.disabled=inflight||!can(false);buttons.save.disabled=inflight||!can(true);
   buttons.discard.disabled=inflight||!model.online||!!model.pending||!model.snapshot;
   buttons.review.hidden=!(model.review||model.conflict);buttons.review.disabled=inflight||!model.online||!!model.pending;
   const state=[model.migration&&t('migration'),error&&t(error),model.issue&&t(model.issue),d&&!d.available&&(d.unavailableReason||t('notReady')),(model.review||model.conflict)&&t('revisionConflict'),model.dirty&&t('draft')].filter(Boolean);
   if(model.pending)state.push(t(model.result?.status==='queued'?'queued':'unknown'));
   if(model.result&&!model.pending)state.push(t(model.result.status),model.result.code&&t(model.result.code),t(model.result.persistence),model.result.message);
   notice.textContent=state.filter(Boolean).join('\n');
   const rt=model.snapshot?.runtime;
   actual.textContent=rt?`${model.online?'':t('disconnected')+'\n'}${t(model.online&&rt.active?'active':v&&!Number(parseSnapshot(model.snapshot.config)[1])?'disabled':'waiting')}\nrequested: ${rt.requestedRevision} · applied: ${rt.appliedRevision}`:'';
   const engineName=d?.engineVersion==='1.1.0'?'FoveatedDLSS 1.1':`FoveatedDLSS ${d?.engineVersion}`;
   detail.textContent=[d&&`${d.hostId} ${d.hostVersion} · ${engineName} · control ${d.controlVersion}`,rt?.message,rt?.details].filter(Boolean).join('\n');
   target.dispatchEvent(new CustomEvent('foveateddraftchange',{bubbles:true}));
  }
  const token=document.querySelector('meta[name=overlay-token]')?.content;
  async function rpc(request){if(options.exchange)return options.exchange(request);const abort=new AbortController(),timeout=setTimeout(()=>abort.abort(),options.timeoutMs||5000);try{const r=await fetch('/api',{method:'POST',headers:{'Content-Type':'application/json','X-Overlay-Token':token},body:JSON.stringify(request),signal:abort.signal});const value=await r.json();if(!r.ok||value.error)throw Error('disconnected');return value;}finally{clearTimeout(timeout);}}
  async function refresh(){if(stopped||inflight)return;inflight=true;render();try{model.ingest(await rpc({op:'foveated_state',requestId:model.pending?.requestId}));error='';}catch(e){model.disconnect();error=e.message;}finally{inflight=false;render();}}
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
  return {model,refresh,apply,setLanguage(value){lang=value;render();},destroy(){stopped=true;clearTimeout(timer);headerSize?.disconnect();document.removeEventListener('overlaylanguagechange',language);for(const x of hostSlots)x.controller?.destroy?.();}};
 }
 const api={Model,parse,parseSnapshot,draftForSnapshot,revision,compatibility,capabilities,geometry,mount,ranges};
 if(typeof module!=='undefined'&&module.exports)module.exports=api;
 else root.OverlayFoveatedV2=api;
})(globalThis);
