const {chromium}=require('playwright');const assert=require('node:assert/strict');
(async()=>{const browser=await chromium.launch({channel:'msedge',headless:true});try{
 const page=await browser.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));await page.goto(process.argv[2]);
 await page.waitForFunction(()=>document.querySelector('#output-width')?.value==='3003'&&!document.querySelector('#output-width').disabled);
 await page.locator('#output-plus').click();assert.equal(await page.locator('#output-width').inputValue(),'3053');
 await page.locator('[data-overlay-page=log]').click();assert(await page.locator('#pv-actions').isHidden());
 await page.locator('[data-overlay-page=parameters]').click();assert.equal(await page.locator('#output-width').inputValue(),'3053');
 await page.locator('#apply').click();await page.waitForFunction(()=>document.querySelector('#result').textContent.includes('acknowledged'));
 assert(!await page.evaluate(()=>window.overlayResolution.dirty));
 await page.locator('#output-plus').click();await page.locator('#discard').click();assert.equal(await page.locator('#output-width').inputValue(),'3053');
 assert.deepEqual(errors,[]);console.log('PASS real common widget +/-50, Log, Apply and Discard without AC7 UI or UEVR');
}finally{await browser.close();}})().catch(e=>{console.error(e);process.exitCode=1;});
