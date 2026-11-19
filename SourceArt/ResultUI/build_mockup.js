// Result UI mockup (1920x1080) rendered with headless Chrome.
//   node SourceArt/ResultUI/build_mockup.js <stage screenshot.png> <out.png>
// Card portraits are dummies cropped from SourceArt/Boar/Preview renders (replaceable image area).
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const HERE = __dirname;
const ROOT = path.resolve(HERE, '../..');
const [bg, out] = process.argv.slice(2);
const CHROME = ['C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(fs.existsSync);
const url = (p) => 'file:///' + path.resolve(p).replace(/\\/g, '/');
const BOAR_SHEET = url(path.join(ROOT, 'SourceArt/Boar/Preview/A_Boar_Alert_sheet.png'));
const BOAR_ICON = url(path.join(ROOT, 'SourceArt/StageHUD/Textures/T_StageHUD_Boar.png'));

const C = { cyan: '#3cd2f5', cyanDim: 'rgba(60,210,245,.35)', amber: '#ffc933', navy: 'rgba(6,20,40,.82)' };
const chamfer = (c) => `clip-path:polygon(${c}px 0,calc(100% - ${c}px) 0,100% ${c}px,100% calc(100% - ${c}px),calc(100% - ${c}px) 100%,${c}px 100%,0 calc(100% - ${c}px),0 ${c}px)`;

// Framed chamfered panel: outer cyan line + inner fill (two stacked clipped boxes).
const panel = (x, y, w, h, c, inner, opt = {}) => `
<div style="position:absolute;left:${x}px;top:${y}px;width:${w}px;height:${h}px;${opt.glow ? `filter:drop-shadow(0 0 10px rgba(60,210,245,.45))` : ''}">
  <div style="position:absolute;inset:0;background:${opt.edge || C.cyan};${chamfer(c)}"></div>
  <div style="position:absolute;inset:${opt.bw || 2}px;background:${opt.fill || C.navy};${chamfer(c - 1)};overflow:hidden">
    <div class="grid" style="position:absolute;inset:0"></div>${inner}
  </div>
</div>`;

const icons = {
  boar: `<img src="${BOAR_ICON}" style="width:52px;height:52px;object-fit:contain;filter:brightness(0) invert(1) drop-shadow(0 0 6px rgba(120,220,255,.6))">`,
  timer: `<svg width="50" height="50" viewBox="0 0 64 64"><g fill="none" stroke="#e6f7ff" stroke-width="5" stroke-linecap="round"><circle cx="32" cy="36" r="22"/><path d="M32 36V22M26 6h12M32 6v8M50 16l4-4"/></g></svg>`,
  coin: `<svg width="52" height="52" viewBox="0 0 64 64"><defs><radialGradient id="cg" cx=".35" cy=".3" r=".8"><stop offset="0" stop-color="#fff3a8"/><stop offset=".5" stop-color="#ffc21a"/><stop offset="1" stop-color="#c77800"/></radialGradient></defs>
    <circle cx="32" cy="32" r="27" fill="url(#cg)" stroke="#8a5200" stroke-width="2"/><circle cx="32" cy="32" r="19" fill="none" stroke="#fff1a0" stroke-width="2.5" opacity=".8"/>
    <path d="M32 19l4 9 9 1-7 6 2 9-8-5-8 5 2-9-7-6 9-1z" fill="#fff6c8" opacity=".95"/></svg>`,
};

const row = (y, icon, label, value) => `
<div style="position:absolute;left:0;top:${y}px;width:100%;height:132px">
  <div style="position:absolute;left:28px;top:30px;width:72px;height:72px;border:1.5px solid ${C.cyanDim};background:rgba(60,210,245,.08);${chamfer(10)};display:flex;align-items:center;justify-content:center">${icon}</div>
  <div class="jp" style="position:absolute;left:128px;top:44px;font-size:32px;color:#d8f2ff">${label}</div>
  <div style="position:absolute;right:36px;top:0;height:132px;display:flex;align-items:center">${value}</div>
  ${y > 0 ? `<div style="position:absolute;left:28px;right:28px;top:0;height:1px;background:linear-gradient(90deg,transparent,${C.cyanDim} 15%,${C.cyanDim} 85%,transparent)"></div>` : ''}
</div>`;

// Boar card: [No.] [image area] [name plate], NEW tag separate on the top-right corner.
const card = (x, y, opt) => {
  const W = 206, H = 372, IMG = 174;
  const img = opt.empty
    ? `<div style="position:absolute;inset:0;display:flex;align-items:center;justify-content:center;flex-direction:column;gap:10px">
         <svg width="84" height="84" viewBox="0 0 64 64"><g fill="none" stroke="rgba(120,200,235,.45)" stroke-width="3" stroke-dasharray="5 4"><rect x="6" y="6" width="52" height="52" rx="4"/></g><text x="32" y="41" text-anchor="middle" font-size="26" font-family="Chakra Petch" font-weight="700" fill="rgba(120,200,235,.55)">?</text></svg>
       </div>`
    : `<div style="position:absolute;inset:0;background:url('${BOAR_SHEET}') no-repeat;background-size:${3200 * opt.zoom}px ${900 * opt.zoom}px;background-position:${-opt.cx * opt.zoom + IMG / 2}px ${-opt.cy * opt.zoom + IMG / 2}px;filter:${opt.filter || ''}"></div>
       <div style="position:absolute;inset:0;background:radial-gradient(circle at 50% 40%,transparent 45%,rgba(4,14,30,.55) 100%)"></div>`;
  const edge = opt.empty ? 'rgba(60,210,245,.35)' : (opt.isNew ? C.amber : C.cyan);
  return `
<div style="position:absolute;left:${x}px;top:${y}px;width:${W}px;height:${H}px;${opt.isNew ? 'filter:drop-shadow(0 0 12px rgba(255,180,0,.35))' : ''}">
  <div style="position:absolute;inset:0;background:${edge};${chamfer(16)}"></div>
  <div style="position:absolute;inset:2px;background:${opt.empty ? 'rgba(6,18,34,.75)' : 'rgba(8,24,46,.92)'};${chamfer(15)}"></div>
  <div class="num" style="position:absolute;left:16px;top:12px;font-size:17px;letter-spacing:2px;color:${opt.empty ? 'rgba(140,200,230,.5)' : '#8fdcf5'}">${opt.no}</div>
  <div style="position:absolute;left:16px;top:42px;width:${IMG}px;height:${IMG}px;background:linear-gradient(160deg,#16405f,#0b2440 60%,#08182c);overflow:hidden;outline:1.5px solid ${opt.empty ? 'rgba(60,210,245,.25)' : 'rgba(60,210,245,.6)'};outline-offset:-1px">
    ${img}
    <svg style="position:absolute;inset:0" width="${IMG}" height="${IMG}"><g stroke="${opt.isNew ? C.amber : C.cyan}" stroke-width="3" fill="none" opacity="${opt.empty ? .35 : .9}">
      <path d="M2 20V2H20M${IMG - 20} 2H${IMG - 2}V20M${IMG - 2} ${IMG - 20}V${IMG - 2}H${IMG - 20}M20 ${IMG - 2}H2V${IMG - 20}"/></g></svg>
  </div>
  <div style="position:absolute;left:16px;right:16px;top:${42 + IMG + 14}px;height:1px;background:${C.cyanDim}"></div>
  <div class="jp" style="position:absolute;left:0;right:0;top:${42 + IMG + 30}px;text-align:center;font-size:${opt.empty ? 24 : 25}px;color:${opt.empty ? 'rgba(160,210,235,.5)' : '#f2fbff'}">${opt.name}</div>
  <div class="jp2" style="position:absolute;left:0;right:0;top:${42 + IMG + 72}px;text-align:center;font-size:17px;color:${opt.empty ? 'rgba(140,200,230,.4)' : '#7fc9e4'}">${opt.sub}</div>
  ${opt.isNew ? `<div class="num" style="position:absolute;right:-10px;top:-14px;padding:3px 18px 3px 22px;font-size:24px;font-style:italic;letter-spacing:2px;color:#2a1a00;background:linear-gradient(180deg,#ffe066,#ffb300);clip-path:polygon(10px 0,100% 0,calc(100% - 10px) 100%,0 100%);filter:drop-shadow(0 0 8px rgba(255,190,0,.9))">NEW</div>` : ''}
</div>`;
};

const PX = 150, PY = 100, PW = 1620, PH = 880;
const html = `<!doctype html><html><head><meta charset="utf-8">
<link href="https://fonts.googleapis.com/css2?family=Chakra+Petch:ital,wght@0,600;0,700;1,700&family=Dela+Gothic+One&family=M+PLUS+1p:wght@500;700;800&display=block" rel="stylesheet">
<style>
html,body{margin:0;width:1920px;height:1080px;overflow:hidden;background:#000}
.jp{font-family:'M PLUS 1p';font-weight:800;letter-spacing:2px}
.jp2{font-family:'M PLUS 1p';font-weight:500;letter-spacing:1px}
.num{font-family:'Chakra Petch';font-weight:700}
.grid{background-image:linear-gradient(rgba(60,210,245,.06) 1px,transparent 1px),linear-gradient(90deg,rgba(60,210,245,.06) 1px,transparent 1px);background-size:32px 32px}
</style></head><body>
<!-- background: stage view, softened -->
<div style="position:absolute;inset:-30px;background:url('${url(bg)}') no-repeat;background-size:2664px 1500px;background-position:-420px -390px;filter:blur(5px) saturate(.85) brightness(.9)"></div>
<div style="position:absolute;inset:0;background:linear-gradient(180deg,rgba(4,16,36,.50),rgba(4,14,32,.62))"></div>
<div style="position:absolute;inset:0;background:radial-gradient(ellipse 75% 70% at 50% 50%,transparent 40%,rgba(2,8,20,.55) 100%)"></div>
<div style="position:absolute;inset:0;background:repeating-linear-gradient(0deg,rgba(0,0,0,.12) 0 1.5px,transparent 1.5px 4px)"></div>

${panel(PX, PY, PW, PH, 26, `
  <!-- watermark boar -->
  <div style="position:absolute;right:30px;top:6px;width:360px;height:260px;background:url('${BOAR_ICON}') no-repeat center/contain;filter:brightness(0) invert(1);opacity:.06"></div>
  <!-- header -->
  <div class="num" style="position:absolute;left:68px;top:40px;font-size:24px;letter-spacing:5px;color:${C.cyan};font-style:italic">CAPTURE REPORT <span style="color:rgba(60,210,245,.45);margin-left:14px">// STAGE 01</span></div>
  <div class="num" style="position:absolute;left:58px;top:64px;font-size:104px;line-height:1.05;font-style:italic;letter-spacing:6px;color:#fff;text-shadow:0 0 18px rgba(80,200,255,.55),0 4px 0 rgba(0,0,0,.4)">RESULT</div>
  <svg style="position:absolute;left:30px;top:96px" width="440" height="110"><g fill="${C.amber}" style="filter:drop-shadow(0 0 6px rgba(255,190,0,.8))">
    <polygon points="10,96 400,96 392,104 2,104" opacity=".95"/>
  </g></svg>
  <svg style="position:absolute;left:460px;top:112px" width="150" height="60"><g fill="${C.amber}" style="filter:drop-shadow(0 0 6px rgba(255,190,0,.8))">
    <polygon points="18,8 34,8 20,48 4,48"/><polygon points="46,8 62,8 48,48 32,48" opacity=".8"/><polygon points="74,8 90,8 76,48 60,48" opacity=".6"/><polygon points="102,8 118,8 104,48 88,48" opacity=".4"/></g></svg>
  <div style="position:absolute;right:56px;top:66px;display:flex;align-items:center;gap:16px">
    <div class="jp2" style="font-size:20px;color:#8fd6ee">STAGE 01　はじまりの草原</div>
    <div class="num" style="padding:8px 26px;font-size:28px;font-style:italic;letter-spacing:3px;color:#241600;background:linear-gradient(180deg,#ffe066,#ffb300);${chamfer(8)};box-shadow:0 0 18px rgba(255,190,0,.6)">STAGE CLEAR</div>
  </div>
  <div style="position:absolute;left:40px;right:40px;top:214px;height:2px;background:linear-gradient(90deg,${C.cyan},rgba(60,210,245,.15) 70%,transparent)"></div>
`, { glow: true })}

<!-- left: results -->
<div class="jp" style="position:absolute;left:${PX + 64}px;top:${PY + 250}px;font-size:22px;color:${C.cyan};letter-spacing:4px">■ 捕獲結果</div>
${panel(PX + 56, PY + 292, 780, 432, 18, `
  ${row(14, icons.boar, '収容数', `<span class="num" style="font-size:84px;color:${C.amber};text-shadow:0 0 18px rgba(255,180,0,.6)">3</span><span class="num" style="font-size:44px;color:#cfeeff;margin:22px 0 0 14px">/ 3</span>
     <span style="display:flex;gap:6px;margin:18px 0 0 22px">${[0, 1, 2].map(() => `<i style="width:14px;height:30px;background:${C.amber};transform:skewX(-14deg);box-shadow:0 0 8px rgba(255,180,0,.7)"></i>`).join('')}</span>`)}
  ${row(146, icons.timer, 'クリアタイム', `<span class="num" style="font-size:60px;color:#f2fbff;letter-spacing:2px">02:14<span style="font-size:42px">.56</span></span>`)}
  ${row(278, icons.coin, '特別コイン', `<span class="num" style="font-size:60px;color:${C.amber};text-shadow:0 0 14px rgba(255,180,0,.5)">+1</span>`)}
`, { edge: 'rgba(60,210,245,.55)', fill: 'rgba(6,22,44,.78)', bw: 1.5 })}

<!-- right: new registrations -->
<div style="position:absolute;left:${PX + 900}px;top:${PY + 250}px;width:660px;display:flex;justify-content:space-between;align-items:baseline">
  <span class="jp" style="font-size:22px;color:${C.cyan};letter-spacing:4px">■ 新規登録ボア</span>
  <span class="jp2" style="font-size:19px;color:#8fd6ee">図鑑に <span class="num" style="font-size:26px;color:${C.amber}">2</span> 件登録</span>
</div>
${panel(PX + 892, PY + 292, 676, 432, 18, '', { edge: 'rgba(60,210,245,.55)', fill: 'rgba(6,22,44,.62)', bw: 1.5 })}
${card(PX + 906, PY + 322, { no: 'No.001', name: 'ノーマルボア', sub: '草原エリア', isNew: true, cx: 1000, cy: 452, zoom: 0.95 })}
${card(PX + 1129, PY + 322, { no: 'No.004', name: 'トゲボア', sub: '草原エリア', isNew: true, cx: 600, cy: 752, zoom: 0.95, filter: 'hue-rotate(160deg) saturate(1.6) brightness(1.05)' })}
${card(PX + 1352, PY + 322, { no: 'No.---', name: '---', sub: '未登録スロット', empty: true })}

<!-- footer guide -->
<div style="position:absolute;left:0;right:0;top:${PY + 772}px;display:flex;justify-content:center">
  <div style="display:flex;align-items:center;gap:18px;padding:12px 44px;border-top:1px solid ${C.cyanDim};border-bottom:1px solid ${C.cyanDim};background:linear-gradient(90deg,transparent,rgba(10,40,70,.7) 20%,rgba(10,40,70,.7) 80%,transparent)">
    <span class="num" style="width:42px;height:42px;border-radius:50%;background:#e9f7ff;color:#0a2036;display:flex;align-items:center;justify-content:center;font-size:24px">A</span>
    <span class="jp" style="font-size:28px;color:#e2f6ff">任意のボタンでロビーへ戻る</span>
    <svg width="30" height="30" viewBox="0 0 30 30"><path d="M8 4l12 11-12 11" fill="none" stroke="${C.amber}" stroke-width="4"/></svg>
  </div>
</div>
</body></html>`;
fs.mkdirSync(path.join(HERE, '.build'), { recursive: true });
const htmlPath = path.join(HERE, '.build', 'mockup.html');
fs.writeFileSync(htmlPath, html);
execFileSync(CHROME, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1',
  '--allow-file-access-from-files', '--window-size=1920,1080', '--virtual-time-budget=6000',
  `--screenshot=${path.resolve(out)}`, url(htmlPath)], { stdio: 'ignore' });
console.log('wrote', out);
