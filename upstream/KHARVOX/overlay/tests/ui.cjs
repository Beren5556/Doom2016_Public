const {chromium}=require('playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path');
(async()=>{
 const browser=await chromium.launch({channel:'msedge',headless:true});
 let page;try{
  page=await browser.newPage({viewport:{width:672,height:720},deviceScaleFactor:1,colorScheme:'dark'});
  const errors=[];page.on('pageerror',error=>errors.push(error.message));await page.goto(process.argv[2]);
  await page.waitForFunction(()=>document.querySelector('#pv-native-status')?.textContent.includes('120 evaluations'));
  assert.equal(await page.locator('#pv-mode').inputValue(),'DLAA');assert.equal(await page.locator('#pv-preset').inputValue(),'K');
  assert(await page.locator('#pv-preset').isEnabled());await page.waitForFunction(()=>!document.querySelector('#output-width').disabled);
  assert.equal(await page.locator('[data-overlay-page=game]').count(),0);
  for(const n of [1,2,3])assert(await page.locator('#pv-mode option').nth(n).isEnabled());
  assert(await page.locator('#pv-mode option').nth(4).isDisabled());
  fs.mkdirSync(process.argv[3],{recursive:true});
  const outputBefore=Number(await page.locator('#output-width').inputValue());await page.locator('#output-plus').click();
  await page.locator('#pv-mode').click();await page.waitForTimeout(1100);await page.getByRole('listbox').getByRole('option',{name:'DLSS',exact:true}).click();
  assert(await page.locator('#pv-internal').isEnabled());await page.locator('[data-action=all-apply]').click();
  await page.waitForFunction(()=>document.querySelector('#pv-action-status').textContent.includes('Applied requested settings.'));
  console.log('Action:',await page.locator('#pv-action-status').textContent());
  assert((await page.locator('#pv-action-status').textContent()).includes('Applied requested settings.'));
  await page.waitForFunction(width=>document.querySelector('#output-effective').textContent.includes(`${width} ×`),outputBefore+50);
  await page.waitForFunction(()=>document.querySelector('#output-effective').textContent.includes('DLSS input: 2667 × 2833 → output: 4000 × 4250'));
  await page.waitForFunction(()=>window.overlayFoveated?.model.descriptor?.generalMode===3);
  await page.locator('[data-overlay-page=foveated_dlss]').click();
  await page.locator('#exp-mode').click();await page.getByRole('listbox').getByRole('option',{name:/Dynamic/}).click();
  await page.locator('#exp-save').click();await page.waitForFunction(()=>window.overlayFoveated.model.snapshot.experimental.selectedMode===2&&!window.overlayFoveated.model.pending);
  await page.waitForFunction(()=>window.overlayFoveated.model.snapshot.runtime.active);
  // Real native maps are pixel-to-common. Both eye previews must contain
  // visible contours, not paths thousands of pixels outside their clip.
  for(const id of ['exp-left-eye','exp-right-eye']){
   const bounds=await page.locator(`#${id} path[data-contour]`).first().evaluate(e=>{
    const b=e.getBBox();return {x:b.x,y:b.y,w:b.width,h:b.height};
   });
   assert(bounds.w>20&&bounds.w<500&&bounds.h>20&&bounds.h<500,JSON.stringify(bounds));
   assert(bounds.x<240&&bounds.x+bounds.w>0&&bounds.y<192&&bounds.y+bounds.h>0);
  }
  assert(await page.locator('#exp-show').isEnabled());
  await page.locator('#exp-show').check();await page.locator('#exp-apply').click();
  await page.waitForFunction(()=>window.overlayFoveated.model.snapshot.config.split(';')[13]==='1'&&!window.overlayFoveated.model.pending);
  await page.locator('#exp-show').uncheck();await page.locator('#exp-apply').click();
  await page.waitForFunction(()=>window.overlayFoveated.model.snapshot.config.split(';')[13]==='0'&&!window.overlayFoveated.model.pending);
  await page.screenshot({path:path.join(process.argv[3],'foveated-dynamic.png'),fullPage:true});
  await page.locator('[data-overlay-page=parameters]').click();
  await page.locator('#pv-mode').click();
  const neuralOption=page.getByRole('listbox').getByRole('option',{name:'DLSS5 / Neural',exact:true});
  assert.equal(await neuralOption.getAttribute('aria-disabled'),'true');
  await neuralOption.dispatchEvent('click');
  assert.equal(await page.locator('#pv-mode').inputValue(),'DLSS');
  await page.locator('[data-overlay-page=foveated_dlss]').click();
  await page.locator('#exp-mode').click();await page.getByRole('listbox').getByRole('option',{name:'Fixed / standard',exact:true}).click();
  await page.locator('#exp-apply').click();await page.waitForFunction(()=>window.overlayFoveated.model.snapshot.experimental.selectedMode===1&&!window.overlayFoveated.model.pending);
  await page.screenshot({path:path.join(process.argv[3],'dlss-fixed.png'),fullPage:true});
  await page.locator('[data-overlay-page=parameters]').click();
  await page.locator('#pv-preset').click();await page.getByRole('listbox').getByRole('option',{name:'L',exact:true}).click();
  await page.locator('[data-action=all-save]').click();
  await page.waitForFunction(()=>document.querySelector('#pv-render-status').textContent.includes('Startup settings saved.'));
  assert.equal(await page.locator('#pv-mode').inputValue(),'DLSS');
  assert(await page.locator('#pv-mode option').nth(4).isDisabled());
  await page.screenshot({path:path.join(process.argv[3],'neural-disabled.png'),fullPage:true});
  await page.locator('[data-overlay-page=log]').click();assert(await page.locator('#pv-actions').isHidden());
  assert.deepEqual(errors,[]);console.log('PASS B3 panel: Neural disabled, usable DLSS, presets, dynamic/fixed policy, persisted settings, separate live telemetry, no Game tab');
 }catch(error){if(page){console.error('UI failure state:',await page.locator('#pv-action-status').textContent(),await page.locator('#pv-native-status').textContent());fs.mkdirSync(process.argv[3],{recursive:true});await page.screenshot({path:path.join(process.argv[3],'failure.png'),fullPage:true});}throw error;}finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
