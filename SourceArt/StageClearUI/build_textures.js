// Stage Clear banner textures: renders each scene.js layer as a transparent PNG cropped to its rect.
//   node SourceArt/StageClearUI/build_textures.js
// Output: SourceArt/StageClearUI/Textures/T_StageClear_<Layer>.png + layout.json (WBP placement, design space 1920x1080).
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const { sceneHtml, LAYERS, ORDER, url } = require('./scene');
const HERE = __dirname;
const OUT = path.join(HERE, 'Textures');
const TMP = path.join(HERE, '.build');
fs.mkdirSync(OUT, { recursive: true });
fs.mkdirSync(TMP, { recursive: true });
const CHROME = ['C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(fs.existsSync);

const layout = {};
for (const name of ORDER) {
  const { w, h, html } = sceneHtml({ only: name });
  const htmlPath = path.join(TMP, name + '.html');
  fs.writeFileSync(htmlPath, html);
  const png = path.join(OUT, `T_StageClear_${name}.png`);
  execFileSync(CHROME, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1', '--allow-file-access-from-files',
    '--default-background-color=00000000', `--window-size=${w},${h}`, '--virtual-time-budget=5000',
    `--screenshot=${png}`, url(htmlPath)], { stdio: 'ignore' });
  layout[name] = { rect: LAYERS[name].rect, texture: [w, h] };
  console.log(name, `${w}x${h}`, fs.statSync(png).size);
}
fs.writeFileSync(path.join(HERE, 'layout.json'), JSON.stringify(layout, null, 1));
