// Optional visual acceptance against a LOCAL fixture collector, not real reports.
const {chromium}=require('playwright');
const fs=require('node:fs');
const path=require('node:path');
(async()=>{
    const base=process.argv[2];
    if(new URL(base).hostname!=='127.0.0.1') throw Error('Local fixture collector required');
    const browser=await chromium.launch({channel:'msedge',headless:true});
    try {
        const context=await browser.newContext();
        await context.request.post(base+'/admin/login',{form:{token:'fixture-admin'}});
        const page=await context.newPage();
        const out=path.resolve('build/diagnostics-dashboard-test');fs.mkdirSync(out,{recursive:true});
        for(const [width,height] of [[1366,900],[390,844]]) {
            await page.setViewportSize({width,height});
            await page.goto(base+'/admin/events');
            await page.getByText('Report details',{exact:true}).first().click();
            if(await page.locator('h1').innerText()!=='Report history') throw Error('History not rendered');
            if(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth)) throw Error('Page overflow');
            await page.screenshot({path:path.join(out,`history-${width}.png`),fullPage:true});
            await page.locator('select[name=type]').selectOption('app.system');
            await page.getByRole('button',{name:'Filter',exact:true}).click();
            if(await page.locator('tr').count()!==2) throw Error('Event filter mismatch');
        }
        console.log('Dashboard desktop/mobile layout, detail expansion and filters PASS');
    } finally {await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
