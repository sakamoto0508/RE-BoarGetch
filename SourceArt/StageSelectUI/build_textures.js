// Stage Select textures: renders each static scene.js layer to SourceArt/StageSelectUI/Textures/T_SS_<Layer>.png
// and writes layout.json (layer rects + live widget layout) for Verification/StageSelectUISetup.py.
//   node SourceArt/StageSelectUI/build_textures.js
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const { LAYERS, LIVE, layerPage, url } = require('./scene');
const HERE = __dirname;
const OUT = path.join(HERE, 'Textures');
const TMP = path.join(HERE, '.build');
fs.mkdirSync(OUT, { recursive: true });
fs.mkdirSync(TMP, { recursive: true });
const CHROME = ['C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(fs.existsSync);

const layout = { layers: {}, live: LIVE };
for (const name of Object.keys(LAYERS)) {
  const { w, h, html } = layerPage(name);
  const htmlPath = path.join(TMP, name + '.html');
  fs.writeFileSync(htmlPath, html);
  const png = path.join(OUT, `T_SS_${name}.png`);
  execFileSync(CHROME, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1', '--allow-file-access-from-files',
    '--default-background-color=00000000', `--window-size=${w},${h}`, '--virtual-time-budget=5000', `--screenshot=${png}`, url(htmlPath)], { stdio: 'ignore' });
  layout.layers[name] = { rect: LAYERS[name].rect, texture: [w, h], standalone: !!LAYERS[name].standalone };
  console.log(name, `${w}x${h}`, fs.statSync(png).size);
}
fs.writeFileSync(path.join(HERE, 'layout.json'), JSON.stringify(layout, null, 1));
