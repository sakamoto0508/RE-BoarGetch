// Stage Clear moment mockup (1920x1080) over a real game frame, rendered with headless Chrome.
//   node SourceArt/StageClearUI/build_mockup.js <frame.png> <out.png> [cropX,cropY,cropW,cropH,srcW,srcH]
// Only the upper/centre band is used: no lower info panels (details belong to the Result screen).
// The banner markup lives in scene.js and is shared with build_textures.js (in-game WBP_StageClearBanner).
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const { sceneHtml, url } = require('./scene');
const HERE = __dirname;
const [bg, out, crop] = process.argv.slice(2);
const CHROME = ['C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(fs.existsSync);

function bgStyle() {
  if (!crop) return `background:url('${url(bg)}') center/cover`;
  const [x, y, w, h, sw, sh] = crop.split(',').map(Number);
  const k = Math.max(1920 / w, 1080 / h);
  return `background:url('${url(bg)}') no-repeat;background-size:${sw * k}px ${sh * k}px;background-position:${-(x * k + (w * k - 1920) / 2)}px ${-(y * k + (h * k - 1080) / 2)}px`;
}

const { html } = sceneHtml({ background: bgStyle() });
fs.mkdirSync(path.join(HERE, '.build'), { recursive: true });
const htmlPath = path.join(HERE, '.build', 'mockup.html');
fs.writeFileSync(htmlPath, html);
execFileSync(CHROME, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1', '--allow-file-access-from-files',
  '--window-size=1920,1080', '--virtual-time-budget=6000', `--screenshot=${path.resolve(out)}`, url(htmlPath)], { stdio: 'ignore' });
console.log('wrote', out);
