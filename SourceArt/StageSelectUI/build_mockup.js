// Stage Select mockup (1920x1080) built from the same scene.js layers as the in-game textures, plus the live
// parts (current Stage01 diorama from Verification/DioramaCapture.py, texts, values). Run build_textures.js first.
//   node SourceArt/StageSelectUI/build_mockup.js <stage-shot.png for the thumbnail> <out.png>
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const { mockupPage, url } = require('./scene');
const HERE = __dirname;
const [thumb, out] = process.argv.slice(2);
const CHROME = ['C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(fs.existsSync);
const html = mockupPage(thumb, (n) => url(path.join(HERE, 'Textures', `T_SS_${n}.png`)));
fs.mkdirSync(path.join(HERE, '.build'), { recursive: true });
const htmlPath = path.join(HERE, '.build', 'mockup.html');
fs.writeFileSync(htmlPath, html);
execFileSync(CHROME, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1', '--allow-file-access-from-files',
  '--window-size=1920,1080', '--virtual-time-budget=6000', `--screenshot=${path.resolve(out)}`, url(htmlPath)], { stdio: 'ignore' });
console.log('wrote', out);
