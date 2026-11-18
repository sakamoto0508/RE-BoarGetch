// GameOver UI textures: renders each part as a transparent PNG with headless Chrome.
//   node SourceArt/GameOverUI/build_textures.js
// Output: SourceArt/GameOverUI/Textures/*.png (imported to /Game/UI/GameOver by GameOverUISetup.py)
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const HERE = __dirname;
const OUT = path.join(HERE, 'Textures');
const TMP = path.join(HERE, '.build');
fs.mkdirSync(OUT, { recursive: true });
fs.mkdirSync(TMP, { recursive: true });
const CHROME = ['C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(fs.existsSync);

const FONT = `<link href="https://fonts.googleapis.com/css2?family=Dela+Gothic+One&display=block" rel="stylesheet">`;
const page = (w, h, body) => `<!doctype html><html><head><meta charset="utf-8">${FONT}
<style>html,body{margin:0;padding:0;width:${w}px;height:${h}px;overflow:hidden;background:transparent}</style>
</head><body>${body}</body></html>`;

// ---- Button frames (visual 720x100, 36px glow padding) ----
const BW = 720, BH = 100, P = 36, TW = BW + P * 2, TH = BH + P * 2, C = 18;
const poly = (x, y, w, h, c) =>
  `${x + c},${y} ${x + w - c},${y} ${x + w},${y + c} ${x + w},${y + h - c} ${x + w - c},${y + h} ${x + c},${y + h} ${x},${y + h - c} ${x},${y + c}`;

function button(selected) {
  const edge = selected ? '#ffc933' : '#3cd2f5';
  const glow = selected ? '#ffae00' : '#1fb8e6';
  const fillTop = selected ? 'rgba(46,40,10,0.78)' : 'rgba(10,34,58,0.74)';
  const fillBot = selected ? 'rgba(14,20,18,0.82)' : 'rgba(4,16,32,0.80)';
  const sw = selected ? 3.5 : 2;
  const outer = poly(P, P, BW, BH, C);
  const inner = poly(P + 7, P + 7, BW - 14, BH - 14, C - 5);
  const glowStd = selected ? 9 : 4;
  const glowOp = selected ? 1 : 0.55;
  const ticks = selected ? `
    <polygon points="${P + 34},${P - 7} ${P + 150},${P - 7} ${P + 144},${P - 2} ${P + 40},${P - 2}" fill="${edge}" filter="url(#g)"/>
    <polygon points="${P + BW - 150},${P + BH + 2} ${P + BW - 34},${P + BH + 2} ${P + BW - 40},${P + BH + 7} ${P + BW - 144},${P + BH + 7}" fill="${edge}" filter="url(#g)"/>
    <g fill="${edge}" opacity="0.9">
      <polygon points="${P - 22},${P + 34} ${P - 14},${P + 34} ${P - 6},${P + 50} ${P - 14},${P + 66} ${P - 22},${P + 66} ${P - 14},${P + 50}"/>
    </g>
    <g fill="${edge}" opacity="0.9">
      <polygon points="${P + BW + 22},${P + 34} ${P + BW + 14},${P + 34} ${P + BW + 6},${P + 50} ${P + BW + 14},${P + 66} ${P + BW + 22},${P + 66} ${P + BW + 14},${P + 50}"/>
    </g>` : `
    <polygon points="${P + 30},${P - 4} ${P + 110},${P - 4} ${P + 106},${P} ${P + 34},${P}" fill="${edge}" opacity="0.9"/>
    <polygon points="${P + BW - 110},${P + BH} ${P + BW - 30},${P + BH} ${P + BW - 34},${P + BH + 4} ${P + BW - 106},${P + BH + 4}" fill="${edge}" opacity="0.9"/>`;
  return page(TW, TH, `<svg width="${TW}" height="${TH}" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="f" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="${fillTop}"/><stop offset="1" stop-color="${fillBot}"/></linearGradient>
    <pattern id="scan" width="4" height="4" patternUnits="userSpaceOnUse"><rect width="4" height="1.3" fill="rgba(255,255,255,0.035)"/></pattern>
    <pattern id="grid" width="24" height="24" patternUnits="userSpaceOnUse"><path d="M24 0V24M0 24H24" stroke="${edge}" stroke-opacity="0.07" stroke-width="1" fill="none"/></pattern>
    <filter id="g" x="-20%" y="-60%" width="140%" height="220%"><feGaussianBlur stdDeviation="${glowStd}" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
    <filter id="soft"><feGaussianBlur stdDeviation="${selected ? 14 : 8}"/></filter>
    <clipPath id="clip"><polygon points="${outer}"/></clipPath>
  </defs>
  <polygon points="${outer}" fill="none" stroke="${glow}" stroke-width="${selected ? 10 : 6}" opacity="${selected ? 0.55 : 0.22}" filter="url(#soft)"/>
  <polygon points="${outer}" fill="url(#f)"/>
  <g clip-path="url(#clip)">
    <rect x="0" y="0" width="${TW}" height="${TH}" fill="url(#grid)"/>
    <rect x="0" y="0" width="${TW}" height="${TH}" fill="url(#scan)"/>
    <rect x="${P}" y="${P}" width="${BW}" height="${BH * 0.45}" fill="rgba(255,255,255,${selected ? 0.05 : 0.04})"/>
    ${selected ? `<polygon points="${outer}" fill="none" stroke="${glow}" stroke-width="22" opacity="0.28" filter="url(#soft)"/>` : ''}
  </g>
  <polygon points="${inner}" fill="none" stroke="${edge}" stroke-width="1" opacity="${selected ? 0.45 : 0.3}"/>
  <polygon points="${outer}" fill="none" stroke="${edge}" stroke-width="${sw}" opacity="${glowOp}" filter="url(#g)"/>
  ${ticks}
</svg>`);
}

// ---- Icons (white, tinted in UMG) ----
const ICON = 128;
const iconPage = (inner) => page(ICON, ICON, `<svg width="${ICON}" height="${ICON}" viewBox="0 0 128 128" xmlns="http://www.w3.org/2000/svg">
  <defs><filter id="g" x="-30%" y="-30%" width="160%" height="160%"><feGaussianBlur stdDeviation="3.5" result="b"/><feComponentTransfer in="b" result="b2"><feFuncA type="linear" slope="0.7"/></feComponentTransfer><feMerge><feMergeNode in="b2"/><feMergeNode in="SourceGraphic"/></feMerge></filter></defs>
  <g filter="url(#g)" fill="#fff" stroke="#fff">${inner}</g></svg>`);
// Circular retry arrow: 300deg arc + arrow head.
const retryIcon = iconPage(`
  <path d="M 92.4 41.6 A 34 34 0 1 0 98 72" fill="none" stroke-width="13" stroke-linecap="round"/>
  <polygon points="104,22 106,58 72,50" stroke="none"/>`);
const homeIcon = iconPage(`
  <path d="M64 18 L112 60 L100 60 L100 108 L74 108 L74 80 L54 80 L54 108 L28 108 L28 60 L16 60 Z" stroke="none"/>
  <rect x="86" y="24" width="12" height="22" stroke="none"/>`);

// ---- Title (GAME OVER, red warning plate + stripes) ----
const TTW = 1400, TTH = 300;
const title = page(TTW, TTH, `
<style>
  .wrap{position:absolute;inset:0}
  .t{position:absolute;left:0;right:0;top:88px;text-align:center;font-family:'Dela Gothic One','Arial Black',sans-serif;
     font-size:112px;line-height:1;letter-spacing:4px;transform:skewX(-12deg);}
  .glow{color:transparent;-webkit-text-stroke:10px rgba(255,30,50,.55);filter:blur(14px)}
  .edge{color:transparent;-webkit-text-stroke:6px #4a0612}
  .fill{background:linear-gradient(180deg,#fff6f4 0%,#ffd0d3 45%,#ff7d8a 62%,#ffb0b6 100%);-webkit-background-clip:text;color:transparent;
        filter:drop-shadow(0 0 6px rgba(255,60,80,.9)) drop-shadow(0 0 22px rgba(255,20,40,.7))}
  .scan{position:absolute;left:0;right:0;top:88px;height:120px;background:repeating-linear-gradient(0deg,rgba(0,0,0,.18) 0 2px,transparent 2px 5px);
        -webkit-mask:linear-gradient(#000,#000);mix-blend-mode:multiply;pointer-events:none}
</style>
<svg class="wrap" width="${TTW}" height="${TTH}" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="plate" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="rgba(70,6,18,0.78)"/><stop offset="1" stop-color="rgba(20,4,12,0.70)"/></linearGradient>
    <linearGradient id="fadeL" x1="0" x2="1"><stop offset="0" stop-color="#ff2a3c" stop-opacity="0"/><stop offset=".25" stop-color="#ff2a3c"/><stop offset=".75" stop-color="#ff2a3c"/><stop offset="1" stop-color="#ff2a3c" stop-opacity="0"/></linearGradient>
    <filter id="g" x="-10%" y="-200%" width="120%" height="500%"><feGaussianBlur stdDeviation="5" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
    <filter id="big"><feGaussianBlur stdDeviation="24"/></filter>
  </defs>
  <ellipse cx="700" cy="150" rx="520" ry="90" fill="rgba(255,20,40,0.22)" filter="url(#big)"/>
  <polygon points="270,62 1190,62 1140,238 220,238" fill="url(#plate)"/>
  <polygon points="270,62 1190,62 1140,238 220,238" fill="none" stroke="rgba(255,60,80,0.35)" stroke-width="1.5"/>
  <polygon points="290,54 1200,54 1196,62 286,62" fill="url(#fadeL)" filter="url(#g)"/>
  <polygon points="214,238 1124,238 1120,246 210,246" fill="url(#fadeL)" filter="url(#g)"/>
  <g fill="#ff2a3c" filter="url(#g)">
    <polygon points="196,96 226,96 196,204 166,204"/>
    <polygon points="152,112 174,112 148,204 126,204" opacity=".8"/>
    <polygon points="114,128 130,128 110,204 94,204" opacity=".6"/>
    <polygon points="1204,96 1234,96 1204,204 1174,204"/>
    <polygon points="1250,96 1272,96 1246,188 1224,188" opacity=".8"/>
    <polygon points="1288,96 1304,96 1284,172 1268,172" opacity=".6"/>
  </g>
  <g fill="#ff5a6a" opacity=".9">
    <rect x="300" y="72" width="44" height="4"/><rect x="1060" y="224" width="44" height="4"/>
  </g>
</svg>
<div class="t glow">GAME OVER</div>
<div class="t edge">GAME OVER</div>
<div class="t fill">GAME OVER</div>
`);

// ---- Full-screen overlay: soft vignette (stretched) and a CRT/grid tile (tiled) ----
const vignette = page(512, 288, `<div style="width:512px;height:288px;background:
  radial-gradient(ellipse 72% 68% at 50% 46%, rgba(4,14,30,0.0) 0%, rgba(4,14,30,0.16) 55%, rgba(2,8,20,0.50) 100%)"></div>`);
const crtTile = page(64, 64, `<svg width="64" height="64" xmlns="http://www.w3.org/2000/svg">
  <defs><pattern id="s" width="64" height="4" patternUnits="userSpaceOnUse"><rect width="64" height="1.5" fill="rgba(0,0,0,0.16)"/></pattern></defs>
  <rect width="64" height="64" fill="url(#s)"/>
  <path d="M0 0.5H64M0.5 0V64" stroke="rgba(70,210,255,0.07)" stroke-width="1"/></svg>`);

const jobs = [
  ['T_GameOver_ButtonNormal', TW, TH, button(false)],
  ['T_GameOver_ButtonSelected', TW, TH, button(true)],
  ['T_GameOver_IconRetry', ICON, ICON, retryIcon],
  ['T_GameOver_IconHome', ICON, ICON, homeIcon],
  ['T_GameOver_Title', TTW, TTH, title],
  ['T_GameOver_Vignette', 512, 288, vignette],
  ['T_GameOver_CRTTile', 64, 64, crtTile],
];
for (const [name, w, h, html] of jobs) {
  const htmlPath = path.join(TMP, name + '.html');
  fs.writeFileSync(htmlPath, html);
  const png = path.join(OUT, name + '.png');
  execFileSync(CHROME, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1',
    '--default-background-color=00000000', `--window-size=${w},${h}`, '--virtual-time-budget=5000',
    `--screenshot=${png}`, 'file:///' + htmlPath.replace(/\\/g, '/')], { stdio: 'ignore' });
  console.log(name, fs.statSync(png).size);
}
module.exports = { BW, BH, P, TW, TH, TTW, TTH };
