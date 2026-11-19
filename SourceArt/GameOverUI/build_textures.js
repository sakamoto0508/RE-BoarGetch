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

// ---- Button frames (visual 600x96, 24px glow padding). Selected = amber edge + soft glow only (no chevrons/ticks). ----
const BW = 600, BH = 96, P = 24, TW = BW + P * 2, TH = BH + P * 2, C = 16;
const poly = (x, y, w, h, c) =>
  `${x + c},${y} ${x + w - c},${y} ${x + w},${y + c} ${x + w},${y + h - c} ${x + w - c},${y + h} ${x + c},${y + h} ${x},${y + h - c} ${x},${y + c}`;

function button(selected) {
  const edge = selected ? '#ffc933' : 'rgba(60,210,245,0.7)';
  const fillTop = selected ? 'rgba(52,42,10,0.86)' : 'rgba(10,30,54,0.80)';
  const fillBot = selected ? 'rgba(26,22,8,0.86)' : 'rgba(5,17,34,0.82)';
  const sw = selected ? 3 : 1.5;
  const outer = poly(P, P, BW, BH, C);
  return page(TW, TH, `<svg width="${TW}" height="${TH}" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="f" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="${fillTop}"/><stop offset="1" stop-color="${fillBot}"/></linearGradient>
    <filter id="soft" x="-20%" y="-60%" width="140%" height="220%"><feGaussianBlur stdDeviation="9"/></filter>
  </defs>
  ${selected ? `<polygon points="${outer}" fill="none" stroke="#ffaa00" stroke-width="10" opacity="0.42" filter="url(#soft)"/>` : ''}
  <polygon points="${outer}" fill="url(#f)"/>
  <rect x="${P + 18}" y="${P + sw}" width="${BW - 36}" height="1" fill="${selected ? 'rgba(255,230,150,0.35)' : 'rgba(60,210,245,0.18)'}"/>
  <polygon points="${outer}" fill="none" stroke="${edge}" stroke-width="${sw}"/>
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

// ---- Title (GAME OVER): white core + red rim, low glow, slim plate, one stripe per side ----
const TTW = 1240, TTH = 240, TCX = 620, TCY = 120;
const title = page(TTW, TTH, `
<style>
  .t{position:absolute;left:0;width:${TTW}px;top:${TCY - 56}px;text-align:center;font-family:'Dela Gothic One','Arial Black',sans-serif;
     font-size:108px;line-height:1;letter-spacing:6px;transform:skewX(-12deg)}
  .rim{color:transparent;-webkit-text-stroke:7px #6e0714}
  .fill{background:linear-gradient(180deg,#ffffff 0%,#ffe1e3 42%,#ff6f7c 58%,#ff3d4f 100%);-webkit-background-clip:text;color:transparent;
        filter:drop-shadow(0 0 3px rgba(255,50,70,.85)) drop-shadow(0 0 12px rgba(255,20,40,.40))}
</style>
<svg style="position:absolute;left:0;top:0" width="${TTW}" height="${TTH}" xmlns="http://www.w3.org/2000/svg">
  <defs><linearGradient id="pl" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="rgba(60,4,14,.72)"/><stop offset="1" stop-color="rgba(18,3,10,.66)"/></linearGradient>
    <linearGradient id="ln" x1="0" x2="1"><stop offset="0" stop-color="#ff2a3c" stop-opacity="0"/><stop offset=".2" stop-color="#ff2a3c"/><stop offset=".8" stop-color="#ff2a3c"/><stop offset="1" stop-color="#ff2a3c" stop-opacity="0"/></linearGradient>
    <filter id="g" x="-10%" y="-300%" width="120%" height="700%"><feGaussianBlur stdDeviation="3" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter></defs>
  <polygon points="${TCX - 470},${TCY - 74} ${TCX + 510},${TCY - 74} ${TCX + 470},${TCY + 74} ${TCX - 510},${TCY + 74}" fill="url(#pl)"/>
  <rect x="${TCX - 470}" y="${TCY - 79}" width="980" height="3" fill="url(#ln)" filter="url(#g)"/>
  <rect x="${TCX - 510}" y="${TCY + 76}" width="980" height="3" fill="url(#ln)" filter="url(#g)"/>
  <polygon points="${TCX - 548},${TCY - 44} ${TCX - 526},${TCY - 44} ${TCX - 552},${TCY + 44} ${TCX - 574},${TCY + 44}" fill="#ff2a3c" opacity=".9"/>
  <polygon points="${TCX + 574},${TCY - 44} ${TCX + 552},${TCY - 44} ${TCX + 526},${TCY + 44} ${TCX + 548},${TCY + 44}" fill="#ff2a3c" opacity=".9"/>
</svg>
<div class="t rim">GAME OVER</div><div class="t fill">GAME OVER</div>
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
