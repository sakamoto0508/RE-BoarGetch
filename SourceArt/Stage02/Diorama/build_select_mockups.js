// Stage Select composites for review (NOT in-game screenshots: no PIE was run). Reuses the existing StageSelectUI
// mockup layers (SourceArt/StageSelectUI/scene.js + Textures) and swaps in the real-GPU diorama captures from
// capture_dioramas.py. Save-dependent values (coins, encyclopedia, CLEAR) are shown as "-" / hidden.
//   node SourceArt/Stage02/Diorama/build_select_mockups.js
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const UI = path.join(__dirname, '../../StageSelectUI');
const { mockupPage, url } = require(path.join(UI, 'scene'));
const SHOTS = path.join(__dirname, 'Screenshots');
const CHROME = ['C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(fs.existsSync);

const STAGES = {
  Stage01: { number: 'Stage01', name: 'Stage 1', desc: '3体を捕獲しよう', target: '3' },
  Stage02: { number: 'Stage02', name: '段丘牧場', desc: '4体を捕獲しよう', target: '4' },
};

function render(html, out) {
  const htmlPath = path.join(SHOTS, '.build', path.basename(out, '.png') + '.html');
  fs.mkdirSync(path.dirname(htmlPath), { recursive: true });
  fs.writeFileSync(htmlPath, html);
  execFileSync(CHROME, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1', '--allow-file-access-from-files',
    '--window-size=1920,1080', '--virtual-time-budget=6000', `--screenshot=${out}`, url(htmlPath)], { stdio: 'ignore' });
  console.log('wrote', out);
}

const sub = (s, a, b) => { if (!s.includes(a)) throw new Error('missing: ' + a.slice(0, 60)); return s.split(a).join(b); };
const icon = (n) => url(path.join(UI, 'Textures', `T_SS_${n}.png`));
const base = mockupPage(null, icon);
const s1Dio = url(path.join(UI, 'Diorama_Stage01.png'));
for (const [key, st] of Object.entries(STAGES)) {
  let h = sub(base, s1Dio, url(path.join(SHOTS, `Diorama_${key}.png`)));
  h = sub(h, '>Stage01</div>', `>${st.number}</div>`);
  h = sub(h, '>Stage 1</div>', `>${st.name}</div>`);
  h = h.replace(/研究所まわりの草原エリア。<br><span style="margin-left:24px">3体のボアを捕獲しよう。<\/span>/, st.desc);
  h = h.replace(/(目標捕獲数<\/span>\s*<span[^>]*>)3</, `$1${st.target}<`);
  h = h.replace(/(特別コイン<\/span>\s*<span[^>]*>)0 \/ 1</, '$1-<');
  h = h.replace(/(図鑑登録<\/span>\s*<span[^>]*>)3 \/ 3</, '$1-<');
  h = h.replace(/T_SS_ClearBadge\.png/g, 'about:blank');
  h = sub(h, '</body>', `<div style="position:absolute;left:1470px;top:742px;width:250px;height:112px;background:#0a2440"></div><div style="position:absolute;left:1470px;top:780px;width:250px;text-align:center;font:600 18px sans-serif;color:#7fb8d8">状態（進行データ依存）</div><div style="position:absolute;left:24px;bottom:16px;font:600 16px sans-serif;color:#ffcf4a;background:rgba(0,0,0,.55);padding:4px 10px">合成モック（PIE未実施）: UIレイヤー＋実GPUのジオラマ撮影 / 進行データ依存の値は「-」</div></body>`);
  render(h, path.join(SHOTS, `StageSelect_${key}_mock.png`));
}
// large Stage 2 diorama on the Stage Select backdrop colour
render(`<!doctype html><html><body style="margin:0;width:1920px;height:1080px;overflow:hidden;background:radial-gradient(ellipse at 50% 60%,#0b2a4a,#04101f 70%)">
<img src="${url(path.join(SHOTS, 'Diorama_Stage02.png'))}" style="position:absolute;left:${960 - 1023 * 1.05}px;top:${545 - 890 * 1.05}px;width:${2048 * 1.05}px;filter:drop-shadow(0 24px 24px rgba(0,0,0,.5))">
<div style="position:absolute;left:24px;bottom:16px;font:600 16px sans-serif;color:#ffcf4a;background:rgba(0,0,0,.55);padding:4px 10px">BP_Stage02_Diorama 実GPU撮影（ABoarStagePreviewActorと同じ正射影Camera）拡大</div></body></html>`,
  path.join(SHOTS, 'Diorama_Stage02_large.png'));
