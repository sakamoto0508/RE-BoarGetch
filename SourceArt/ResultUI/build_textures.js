// Result UI textures: renders each part as a transparent PNG with headless Chrome.
//   node SourceArt/ResultUI/build_textures.js
// Output: SourceArt/ResultUI/Textures/*.png (imported to /Game/UI/Result by ResultUISetup.py).
// Static English text (CAPTURE REPORT / RESULT / STAGE CLEAR / NEW) is baked; Japanese and numbers stay UMG text.
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const HERE = __dirname;
const ROOT = path.resolve(HERE, '../..');
const OUT = path.join(HERE, 'Textures');
const TMP = path.join(HERE, '.build');
fs.mkdirSync(OUT, { recursive: true });
fs.mkdirSync(TMP, { recursive: true });
const CHROME = ['C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(fs.existsSync);
const url = (p) => 'file:///' + path.resolve(p).replace(/\\/g, '/');
const BOAR_ICON = url(path.join(ROOT, 'SourceArt/StageHUD/Textures/T_StageHUD_Boar.png'));
const CYAN = '#3cd2f5', AMBER = '#ffc933';

const page = (w, h, body, css = '') => `<!doctype html><html><head><meta charset="utf-8">
<link href="https://fonts.googleapis.com/css2?family=Chakra+Petch:ital,wght@0,700;1,700&display=block" rel="stylesheet">
<style>html,body{margin:0;padding:0;width:${w}px;height:${h}px;overflow:hidden;background:transparent}
.num{font-family:'Chakra Petch';font-weight:700}${css}</style></head><body>${body}</body></html>`;
const oct = (x, y, w, h, c) =>
  `${x + c},${y} ${x + w - c},${y} ${x + w},${y + c} ${x + w},${y + h - c} ${x + w - c},${y + h} ${x + c},${y + h} ${x},${y + h - c} ${x},${y + c}`;
const gridPattern = (id, size, color, op) =>
  `<pattern id="${id}" width="${size}" height="${size}" patternUnits="userSpaceOnUse"><path d="M${size} 0V${size}M0 ${size}H${size}" stroke="${color}" stroke-opacity="${op}" stroke-width="1" fill="none"/></pattern>`;

// Chamfered panel: fill + grid + edge (+ optional glow). Extra SVG goes inside the clip.
function panelSvg(W, H, P, c, o) {
  const pts = oct(P, P, W, H, c);
  return `<svg width="${W + 2 * P}" height="${H + 2 * P}" xmlns="http://www.w3.org/2000/svg"><defs>
    ${gridPattern('grid', o.grid || 32, CYAN, o.gridOp ?? .06)}
    <linearGradient id="fill" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="${o.fillTop}"/><stop offset="1" stop-color="${o.fillBot}"/></linearGradient>
    <clipPath id="clip"><polygon points="${pts}"/></clipPath>
    <filter id="soft"><feGaussianBlur stdDeviation="${o.glowStd || 8}"/></filter>
    <filter id="g" x="-10%" y="-10%" width="120%" height="120%"><feGaussianBlur stdDeviation="2.5" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
  </defs>
  ${o.glow ? `<polygon points="${pts}" fill="none" stroke="${o.glow}" stroke-width="${o.glowW || 8}" opacity="${o.glowOp || .45}" filter="url(#soft)"/>` : ''}
  <polygon points="${pts}" fill="url(#fill)"/>
  <g clip-path="url(#clip)">${o.gridOp === 0 ? '' : '<rect width="100%" height="100%" fill="url(#grid)"/>'}${o.inner || ''}</g>
  <polygon points="${pts}" fill="none" stroke="${o.edge}" stroke-width="${o.edgeW || 2}" ${o.edgeGlow ? 'filter="url(#g)"' : ''}/>
  ${o.outer || ''}</svg>`;
}

// ---- Main report panel 1620x880 (pad 24): the only framed surface. Header rule y=160, column divider x=868. ----
const MP = 24;
const mainPanel = page(1620 + 2 * MP, 880 + 2 * MP, panelSvg(1620, 880, MP, 26, {
  fillTop: 'rgba(10,30,56,0.84)', fillBot: 'rgba(6,20,40,0.86)', edge: 'rgba(60,210,245,0.85)', edgeW: 2,
  glow: '#1fb8e6', glowOp: .35, glowStd: 8, gridOp: 0,
  inner: `<defs><linearGradient id="rule" x1="0" x2="1"><stop offset="0" stop-color="${CYAN}" stop-opacity=".8"/><stop offset=".75" stop-color="${CYAN}" stop-opacity=".15"/><stop offset="1" stop-color="${CYAN}" stop-opacity="0"/></linearGradient></defs>
    <rect x="${MP + 40}" y="${MP + 160}" width="1540" height="1.5" fill="url(#rule)"/>
    <rect x="${MP + 868}" y="${MP + 196}" width="1.5" height="530" fill="rgba(60,210,245,0.22)"/>`,
}));

// ---- Result rows backdrop (760x432, 3 rows of 144): row tint + dividers only, no frame / grid / icon boxes ----
const resultsPanel = page(760, 432, `<svg width="760" height="432">
  <rect x="0" y="0" width="760" height="144" fill="rgba(255,255,255,0.025)"/>
  <rect x="0" y="288" width="760" height="144" fill="rgba(255,255,255,0.025)"/>
  <rect x="24" y="144" width="712" height="1" fill="rgba(60,210,245,0.22)"/>
  <rect x="24" y="288" width="712" height="1" fill="rgba(60,210,245,0.22)"/></svg>`);

// ---- Title: CAPTURE REPORT / RESULT (72px) + one amber rule (480x150) ----
const title = page(480, 150, `
<div class="num" style="position:absolute;left:32px;top:14px;font-size:20px;letter-spacing:6px;color:#62d8f6;font-style:italic">CAPTURE REPORT</div>
<div class="num" style="position:absolute;left:26px;top:34px;font-size:72px;line-height:1;font-style:italic;letter-spacing:4px;color:#fff;text-shadow:0 0 10px rgba(80,200,255,.35),0 3px 0 rgba(0,0,0,.35)">RESULT</div>
<div style="position:absolute;left:30px;top:114px;width:300px;height:4px;background:linear-gradient(90deg,${AMBER},rgba(255,201,51,0));transform:skewX(-30deg)"></div>`);

// ---- STAGE CLEAR badge (266x52, pad 20) ----
const badge = page(306, 92, `<div class="num" style="position:absolute;left:20px;top:20px;width:266px;height:52px;display:flex;align-items:center;justify-content:center;
  font-size:28px;font-style:italic;letter-spacing:3px;color:#241600;background:linear-gradient(180deg,#ffe066,#ffb300);
  clip-path:polygon(8px 0,calc(100% - 8px) 0,100% 8px,100% calc(100% - 8px),calc(100% - 8px) 100%,8px 100%,0 calc(100% - 8px),0 8px)">STAGE CLEAR</div>`,
  'body{filter:drop-shadow(0 0 10px rgba(255,190,0,.65))}');

// ---- Card frames 206x380 (pad 16): photo well at (16,44) 174x174; NEW = amber edge + small glow ----
const CW = 206, CH = 380, CP = 16, IMG = 174;
function cardFrame(kind) {
  const edge = kind === 'new' ? AMBER : kind === 'normal' ? 'rgba(60,210,245,0.8)' : 'rgba(60,210,245,0.28)';
  const fill = kind === 'empty' ? ['rgba(6,18,34,0.55)', 'rgba(5,14,28,0.55)'] : ['rgba(9,26,48,0.94)', 'rgba(7,21,40,0.94)'];
  const well = `<defs><linearGradient id="well" x1="0" y1="0" x2=".6" y2="1"><stop offset="0" stop-color="#16405f"/><stop offset=".6" stop-color="#0b2440"/><stop offset="1" stop-color="#08182c"/></linearGradient></defs>
    <rect x="${CP + 16}" y="${CP + 44}" width="${IMG}" height="${IMG}" fill="url(#well)" opacity="${kind === 'empty' ? .55 : 1}"/>`;
  return page(CW + 2 * CP, CH + 2 * CP, panelSvg(CW, CH, CP, 14, {
    fillTop: fill[0], fillBot: fill[1], edge, edgeW: kind === 'new' ? 2.5 : 1.5, gridOp: 0,
    glow: kind === 'new' ? '#ffae00' : null, glowOp: .30, glowStd: 6, inner: well,
  }));
}
const corners = (color, op) => page(IMG, IMG, `<svg width="${IMG}" height="${IMG}"><g stroke="${color}" stroke-width="3" fill="none" opacity="${op}">
  <path d="M2 22V2H22M${IMG - 22} 2H${IMG - 2}V22M${IMG - 2} ${IMG - 22}V${IMG - 2}H${IMG - 22}M22 ${IMG - 2}H2V${IMG - 22}"/></g></svg>`);

// Placeholder for boars without an EncyclopediaPhoto: silhouette + "PHOTO" caption (2x for crispness).
const S2 = IMG * 2;
const placeholder = page(S2, S2, `
<div style="position:absolute;left:0;top:0;width:${S2}px;height:${S2}px;background:radial-gradient(circle at 50% 42%,rgba(60,210,245,.16),rgba(60,210,245,0) 62%)"></div>
<div style="position:absolute;left:54px;top:74px;width:240px;height:164px;background:rgba(110,215,245,.38);-webkit-mask:url('${BOAR_ICON}') no-repeat center/contain;filter:drop-shadow(0 0 8px rgba(80,210,255,.45))"></div>
<div class="num" style="position:absolute;left:0;width:${S2}px;top:262px;text-align:center;font-size:24px;letter-spacing:8px;text-indent:8px;color:rgba(120,210,240,.55)">NO PHOTO</div>
<svg style="position:absolute;inset:0" width="${S2}" height="${S2}"><g stroke="rgba(120,210,240,.18)" stroke-width="2"><path d="M${S2 / 2} 30V60M${S2 / 2} ${S2 - 60}V${S2 - 30}M30 ${S2 / 2}H60M${S2 - 60} ${S2 / 2}H${S2 - 30}"/></g></svg>`);
const emptyPhoto = page(S2, S2, `<svg width="${S2}" height="${S2}" viewBox="0 0 ${S2} ${S2}"><g fill="none" stroke="rgba(120,200,235,.45)" stroke-width="4" stroke-dasharray="10 8">
  <rect x="${S2 / 2 - 70}" y="${S2 / 2 - 70}" width="140" height="140" rx="8"/></g>
  <text x="${S2 / 2}" y="${S2 / 2 + 26}" text-anchor="middle" font-size="76" font-family="Chakra Petch" font-weight="700" fill="rgba(120,200,235,.55)">?</text></svg>`);

// NEW tag: compact label inside the card's top-right corner (80x28)
const newTag = page(80, 28, `<div class="num" style="position:absolute;left:0;top:0;width:80px;height:28px;display:flex;align-items:center;justify-content:center;
  font-size:18px;font-style:italic;letter-spacing:1px;color:#2a1a00;background:linear-gradient(180deg,#ffe066,#ffb300);clip-path:polygon(7px 0,100% 0,calc(100% - 7px) 100%,0 100%)">NEW</div>`);

// Icons (white/coloured, 128)
const timer = page(128, 128, `<svg width="128" height="128" viewBox="0 0 64 64"><g fill="none" stroke="#e6f7ff" stroke-width="5" stroke-linecap="round" style="filter:drop-shadow(0 0 2px rgba(120,220,255,.8))"><circle cx="32" cy="36" r="22"/><path d="M32 36V22M26 6h12M32 6v8M50 16l4-4"/></g></svg>`);
const coin = page(128, 128, `<svg width="128" height="128" viewBox="0 0 64 64"><defs><radialGradient id="cg" cx=".35" cy=".3" r=".8"><stop offset="0" stop-color="#fff3a8"/><stop offset=".5" stop-color="#ffc21a"/><stop offset="1" stop-color="#c77800"/></radialGradient></defs>
  <circle cx="32" cy="32" r="27" fill="url(#cg)" stroke="#8a5200" stroke-width="2"/><circle cx="32" cy="32" r="19" fill="none" stroke="#fff1a0" stroke-width="2.5" opacity=".8"/>
  <path d="M32 19l4 9 9 1-7 6 2 9-8-5-8 5 2-9-7-6 9-1z" fill="#fff6c8" opacity=".95"/></svg>`);
// Section mark (amber slanted bar) and device-specific guide glyphs (PlayStation ○ / Enter key / mouse).
const sectionMark = page(16, 28, `<div style="position:absolute;left:4px;top:1px;width:6px;height:26px;background:${AMBER};transform:skewX(-14deg)"></div>`);
const iconCircle = page(88, 88, `<svg width="88" height="88"><circle cx="44" cy="44" r="33" fill="none" stroke="#fff" stroke-width="11"/></svg>`);
const keyEnter = page(88, 40, `<div class="num" style="position:absolute;left:1px;top:1px;width:86px;height:38px;box-sizing:border-box;border:1.5px solid rgba(60,210,245,.85);border-radius:5px;display:flex;align-items:center;justify-content:center;font-size:21px;color:#e6f8ff;background:rgba(10,40,70,.7)">Enter</div>`);
const iconMouse = page(60, 76, `<svg width="60" height="76" viewBox="0 0 30 38"><rect x="2" y="2" width="26" height="34" rx="13" fill="none" stroke="#e6f8ff" stroke-width="2.5"/><path d="M15 2v13M2 15h26" stroke="#e6f8ff" stroke-width="2.5"/><path d="M15 2a13 13 0 0 0-13 13h13z" fill="${AMBER}" opacity=".9"/></svg>`);

const jobs = [
  ['T_Result_Panel', 1620 + 2 * MP, 880 + 2 * MP, mainPanel],
  ['T_Result_ResultsPanel', 760, 432, resultsPanel],
  ['T_Result_Title', 480, 150, title],
  ['T_Result_ClearBadge', 306, 92, badge],
  ['T_Result_CardFrameNew', CW + 2 * CP, CH + 2 * CP, cardFrame('new')],
  ['T_Result_CardFrameNormal', CW + 2 * CP, CH + 2 * CP, cardFrame('normal')],
  ['T_Result_CardFrameEmpty', CW + 2 * CP, CH + 2 * CP, cardFrame('empty')],
  ['T_Result_CornersNew', IMG, IMG, corners(AMBER, .95)],
  ['T_Result_CornersNormal', IMG, IMG, corners(CYAN, .9)],
  ['T_Result_CornersEmpty', IMG, IMG, corners(CYAN, .35)],
  ['T_Result_PhotoPlaceholder', S2, S2, placeholder],
  ['T_Result_EmptyPhoto', S2, S2, emptyPhoto],
  ['T_Result_NewTag', 80, 28, newTag],
  ['T_Result_IconTimer', 128, 128, timer],
  ['T_Result_IconCoin', 128, 128, coin],
  ['T_Result_SectionMark', 16, 28, sectionMark],
  ['T_Result_IconCircle', 88, 88, iconCircle],
  ['T_Result_KeyEnter', 88, 40, keyEnter],
  ['T_Result_IconMouse', 60, 76, iconMouse],
];
for (const [name, w, h, html] of jobs) {
  const htmlPath = path.join(TMP, name + '.html');
  fs.writeFileSync(htmlPath, html);
  const png = path.join(OUT, name + '.png');
  execFileSync(CHROME, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1', '--allow-file-access-from-files',
    '--default-background-color=00000000', `--window-size=${w},${h}`, '--virtual-time-budget=5000',
    `--screenshot=${png}`, url(htmlPath)], { stdio: 'ignore' });
  console.log(name, w + 'x' + h, fs.statSync(png).size);
}
