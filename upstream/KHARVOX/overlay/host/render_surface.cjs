// Reusable offscreen HTML renderer. Requires Playwright and installed Edge.
// Receives only app-local OpenXR pointer events, never desktop mouse injection.
const {chromium}=require('playwright');
const fs=require('node:fs');const path=require('node:path');
const url=new URL(process.argv[2]);
if(url.hostname!=='127.0.0.1'||!url.searchParams.get('token'))throw Error('Expected authenticated loopback URL');
const headers={'X-Overlay-Token':url.searchParams.get('token')};
// Preserve button/leave ordering; browser automation need not replay every
// intermediate pose sampled while it was capturing the previous frame.
function compact(events){const out=[];for(const e of events){const last=out[out.length-1];if(last&&last.type===e.type&&(e.type===1||e.type===4))out[out.length-1]=e;else out.push(e);}return out;}
async function post(path,data){const r=await fetch(url.origin+path,{method:'POST',headers,body:data});const j=await r.json();if(!r.ok||j.error)throw Error(j.error||r.statusText);return j;}
(async()=>{
 const browser=await chromium.launch({channel:'msedge',headless:true});
 const colorScheme=process.env.OVERLAY_COLOR_SCHEME||'dark';
 if(!['dark','light'].includes(colorScheme))throw Error('Invalid overlay color scheme');
 const page=await browser.newPage({viewport:{width:672,height:720},deviceScaleFactor:1,colorScheme});
 page.on('console',message=>{if(message.text().startsWith('Overlay action:'))console.log(new Date().toISOString(),message.text());});
 await page.goto(url.href);let down=false,lastFrame=0,lastPulse=0,alive=true,lastDiagnostic=0,wasVisible=false,capturePending=false,previousPixels=null;
 const deadline=process.env.OVERLAY_TEST_DURATION_MS?Date.now()+Number(process.env.OVERLAY_TEST_DURATION_MS):Infinity;
 process.on('SIGINT',()=>alive=false);
 try{while(alive&&Date.now()<deadline){
  const input=await post('/xr/input','');
  if(input.status)throw Error('OpenXR surface reported error '+input.status);
  if(input.visible&&!wasVisible)capturePending=true;
  if(input.visible!==wasVisible||(input.visible&&Date.now()-lastDiagnostic>5000)){console.log(JSON.stringify({time:new Date().toISOString(),visible:input.visible,...input.diagnostics}));lastDiagnostic=Date.now();}
  wasVisible=input.visible;
  if(input.reset)console.log('Input pressure: ambiguous batch cancelled; host remains available');
  for(const e of compact(input.events)){
   if(e.type===1){await page.mouse.move(e.x,e.y);}
   else if(e.type===2&&!down){await page.mouse.move(e.x,e.y);await page.mouse.down();down=true;}
   else if(e.type===3&&down){await page.mouse.up();down=false;}
   else if(e.type===4){await page.mouse.move(-1,-1);if(down){await page.mouse.up();down=false;}}
   else if(e.type===5)await page.mouse.wheel(0,e.value);
  }
  if(!input.visible&&down){await page.mouse.move(-1,-1);await page.mouse.up();down=false;}
  if(input.shutdown)break;
  if(!previousPixels||(input.visible&&Date.now()-lastFrame>150)){const pixels=await page.screenshot({type:'png'});if(!previousPixels||!pixels.equals(previousPixels)){await post('/xr/frame',pixels);previousPixels=pixels;}lastFrame=Date.now();if(capturePending&&input.visible&&process.env.OVERLAY_EVIDENCE_DIR){fs.mkdirSync(process.env.OVERLAY_EVIDENCE_DIR,{recursive:true});fs.writeFileSync(path.join(process.env.OVERLAY_EVIDENCE_DIR,'opened-panel.png'),pixels);capturePending=false;}}
  if(Date.now()-lastPulse>500){await post('/xr/pulse','');lastPulse=Date.now();}
  await new Promise(r=>setTimeout(r,15));
 }}finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
