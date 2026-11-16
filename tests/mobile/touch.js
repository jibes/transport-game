// Touch-driven UI test for the Web build (used for the mobile UI).
//
// Usage: node tests/mobile/touch.js <outdir> <steps.json>
//   env URL   page to load (default http://127.0.0.1:8765/openttd.html)
//   env ARGS  game command line, e.g. "-g -G 1" = new game with seed 1, skipping the menu
//   env VW/VH viewport size in CSS px (default 915x412 = landscape phone; 412x915 = portrait)
//
// steps: [{"wait":ms} | {"tap":[x,y]} | {"long":[x,y]} | {"drag":[x1,y1,x2,y2]} | {"flick":[x1,y1,x2,y2]}
//         | {"pinch":[cx,cy,d1,d2]} | {"resize":[w,h]} | {"js":"code"} | {"reload":"game args"} | {"shot":"name"}]
// Prints console errors at the end; screenshots go to <outdir>/<name>.png.
const { chromium } = require('playwright');
const fs = require('fs');

(async () => {
	const [out, stepsFile] = process.argv.slice(2);
	const steps = JSON.parse(fs.readFileSync(stepsFile, 'utf8'));
	const base = process.env.URL || 'http://127.0.0.1:8765/openttd.html';
	const withArgs = args => args ? base + (base.includes('?') ? '&' : '?') + 'args=' + encodeURIComponent(args) : base;
	const url = withArgs(process.env.ARGS);

	const browser = await chromium.launch();
	const ctx = await browser.newContext({ viewport: { width: +(process.env.VW || 915), height: +(process.env.VH || 412) }, hasTouch: true, isMobile: true, deviceScaleFactor: 1 });
	const page = await ctx.newPage();
	const logs = [];
	page.on('console', m => logs.push(m.text()));
	page.on('pageerror', e => logs.push('PAGEERROR ' + e.message));
	const cdp = await ctx.newCDPSession(page);
	const touch = (type, pts) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: pts.map((q, i) => ({ x: q[0], y: q[1], id: i })) });
	const sleep = ms => new Promise(r => setTimeout(r, ms));
	const move = async (x1, y1, x2, y2, n, dt) => {
		await touch('touchStart', [[x1, y1]]);
		for (let i = 1; i <= n; i++) { await touch('touchMove', [[x1 + (x2 - x1) * i / n, y1 + (y2 - y1) * i / n]]); await sleep(dt); }
		await touch('touchEnd', []);
	};

	await page.goto(url);
	for (const s of steps) {
		if (s.wait) await sleep(s.wait);
		if (s.tap) { await touch('touchStart', [s.tap]); await sleep(60); await touch('touchEnd', []); await sleep(300); }
		if (s.long) { await touch('touchStart', [s.long]); await sleep(800); await touch('touchEnd', []); await sleep(300); }
		if (s.drag) { await move(...s.drag, 15, 20); await sleep(300); }
		if (s.flick) { await move(...s.flick, 8, 12); }
		if (s.pinch) {
			const [cx, cy, d1, d2] = s.pinch;
			const pts = d => [[cx - d / 2, cy], [cx + d / 2, cy]];
			await touch('touchStart', pts(d1));
			for (let i = 1; i <= 15; i++) { await touch('touchMove', pts(d1 + (d2 - d1) * i / 15)); await sleep(20); }
			await touch('touchEnd', []);
			await sleep(500);
		}
		if (s.js) await page.evaluate(s.js);
		if (s.reload !== undefined) await page.goto(withArgs(s.reload));
		if (s.resize) { await page.setViewportSize({ width: s.resize[0], height: s.resize[1] }); await sleep(1500); }
		if (s.shot) await page.screenshot({ path: `${out}/${s.shot}.png` });
	}
	const errors = logs.filter(l => /error|Error|PAGEERROR|abort/.test(l));
	if (errors.length) console.log(errors.slice(-20).join('\n'));
	await browser.close();
})();
