// GameOver UI mockup (1920x1080): composes the same textures the WBP uses over a game screenshot.
//   node SourceArt/GameOverUI/build_mockup.js <background.png> <out.png>
// Layout values mirror GameOverUISetup.py (center-anchored canvas).
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const HERE = __dirname;
const TEX = path.join(HERE, 'Textures');
// Optional crop of the source screenshot: x,y,w,h,srcW,srcH (e.g. drop the PIE window title bar).
const [bg, out, crop] = process.argv.slice(2);
const CHROME = ['C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(fs.existsSync);
const url = (p) => 'file:///' + path.resolve(p).replace(/\\/g, '/');
const t = (n) => url(path.join(TEX, n + '.png'));
const L = require('./layout.json');

const btn = (b, selected) => {
  const cx = 960, cy = 540 + b.y;
  const fw = L.frame.w, fh = L.frame.h;
  return `
  <img src="${t(selected ? 'T_GameOver_ButtonSelected' : 'T_GameOver_ButtonNormal')}" style="position:absolute;left:${cx - fw / 2}px;top:${cy - fh / 2}px;width:${fw}px;height:${fh}px">
  <div style="position:absolute;left:${cx - L.button.w / 2}px;top:${cy - L.button.h / 2}px;width:${L.button.w}px;height:${L.button.h}px;display:flex;align-items:center">
    <img src="${t(b.icon)}" style="width:${L.icon}px;height:${L.icon}px;margin-left:${L.iconLeft}px;filter:${selected ? 'brightness(0) saturate(100%) invert(80%) sepia(60%) saturate(1500%) hue-rotate(358deg) brightness(104%)' : 'brightness(0) saturate(100%) invert(86%) sepia(18%) saturate(900%) hue-rotate(160deg)'}">
    <span style="margin-left:${L.labelGap}px;font-family:'Dela Gothic One';font-size:${L.fontSize}px;letter-spacing:4px;color:${selected ? '#fff6dc' : '#e2f6ff'};
      text-shadow:0 2px 0 rgba(0,0,0,.55)${selected ? ',0 0 14px rgba(255,180,0,.55)' : ''}">${b.label}</span>
  </div>`;
};

function bgStyle() {
  if (!crop) return `background:url('${url(bg)}') center/cover`;
  const [x, y, w, h, sw, sh] = crop.split(',').map(Number);
  const k = Math.max(1960 / w, 1120 / h);
  return `background:url('${url(bg)}') no-repeat;background-size:${sw * k}px ${sh * k}px;background-position:${-(x * k + (w * k - 1960) / 2)}px ${-(y * k + (h * k - 1120) / 2)}px`;
}
const html = `<!doctype html><html><head><meta charset="utf-8">
<link href="https://fonts.googleapis.com/css2?family=Dela+Gothic+One&display=block" rel="stylesheet">
<style>html,body{margin:0;width:1920px;height:1080px;overflow:hidden;background:#000}</style></head><body>
<div style="position:absolute;inset:-20px;${bgStyle()};filter:blur(${L.blurPx}px) brightness(0.82) saturate(0.9)"></div>
<div style="position:absolute;inset:0;background:rgba(6,18,40,${L.tint})"></div>
<img src="${t('T_GameOver_Vignette')}" style="position:absolute;inset:0;width:1920px;height:1080px">
<div style="position:absolute;inset:0;background:url('${t('T_GameOver_CRTTile')}');opacity:${L.crtOpacity}"></div>
<img src="${t('T_GameOver_Title')}" style="position:absolute;left:${960 - L.title.w / 2}px;top:${540 + L.title.y - L.title.h / 2}px;width:${L.title.w}px;height:${L.title.h}px">
${btn(L.retry, true)}
${btn(L.lobby, false)}
</body></html>`;
const htmlPath = path.join(HERE, '.build', 'mockup.html');
fs.writeFileSync(htmlPath, html);
execFileSync(CHROME, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1',
  '--window-size=1920,1080', '--virtual-time-budget=5000', `--screenshot=${path.resolve(out)}`, url(htmlPath)], { stdio: 'ignore' });
console.log('wrote', out);
