// Stage Select scene (1920x1080 design space) as named layers. Static layers become UMG textures
// (build_textures.js); "dyn" holds the live parts (diorama, texts, values) and is drawn only in the mockup.
const path = require('path');
const url = (p) => 'file:///' + path.resolve(p).replace(/\\/g, '/');
const HERE = __dirname;
const DIORAMA = url(path.join(HERE, 'Diorama_Stage01.png'));
const BOAR = url(path.join(HERE, '../StageHUD/Textures/T_StageHUD_Boar.png'));
const COIN = url(path.join(HERE, '../ResultUI/Textures/T_Result_IconCoin.png'));
const CY = 'rgba(60,210,245,', AMBER = '#ffc933';
const oct = (c) => `clip-path:polygon(${c}px 0,calc(100% - ${c}px) 0,100% ${c}px,100% calc(100% - ${c}px),calc(100% - ${c}px) 100%,${c}px 100%,0 calc(100% - ${c}px),0 ${c}px)`;
const box = (x, y, w, h, c, edge, fill, inner = '', extra = '') => `
<div style="position:absolute;left:${x}px;top:${y}px;width:${w}px;height:${h}px;${extra}">
  <div style="position:absolute;inset:0;background:${edge};${oct(c)}"></div>
  <div style="position:absolute;inset:2px;background:${fill};${oct(c - 1)};overflow:hidden">${inner}</div></div>`;

// Diorama: capture image 2048x1536 at scale S (content centre x≈1017, top≈260 source px).
const S = .46;
const DIO = { x: 960 - 1017 * S, y: 186 - 260 * S, w: 2048 * S, h: 1536 * S };
const PED_Y = DIO.y + 1240 * S;

// Layout of the live widgets (also used by the WBP setup through layout.json).
const LIVE = {
  Diorama: [DIO.x, DIO.y, DIO.w, DIO.h],
  StageNumber: [960, 82], StageName: [960, 148],
  Thumbnail: [220, 746, 236, 106],
  Description: [482, 754, 470, 100],
  Stats: { x: 1012, right: 1432, rows: [757, 802, 847] },
  StatusCenter: [1594, 792],
  ArrowLeft: [330, 340, 116, 116], ArrowRight: [1474, 340, 116, 116],
  Start: [560, 902, 340, 84], Cancel: [1020, 902, 340, 84],
  Guide: [1844, 944],
};

const LAYERS = {
  Backdrop: { rect: [0, 0, 1920, 1080], scale: .25 },
  Frame: { rect: [0, 0, 1920, 1080] },
  Header: { rect: [40, 28, 620, 130] },
  Dome: { rect: [400, 130, 1120, 520] },
  Pedestal: { rect: [420, 470, 1080, 340] },
  TitlePlate: { rect: [680, 24, 560, 158] },
  ArrowLeft: { rect: [300, 320, 170, 160] },
  ArrowRight: { rect: [1450, 320, 170, 160] },
  InfoPanel: { rect: [186, 720, 1548, 158] },
  ThumbPlaceholder: { rect: [220, 746, 236, 106] },
  ClearBadge: { rect: [1480, 750, 228, 96] },
  ButtonSelected: { rect: [0, 0, 380, 124], standalone: true },
  ButtonNormal: { rect: [0, 0, 380, 124], standalone: true },
  IconCircle: { rect: [0, 0, 64, 64], standalone: true },
  IconCross: { rect: [0, 0, 64, 64], standalone: true },
  IconBook: { rect: [0, 0, 64, 56], standalone: true },
  KeyEnter: { rect: [0, 0, 84, 36], standalone: true },
  KeyEsc: { rect: [0, 0, 60, 36], standalone: true },
};

const arrowBox = (x, dir) => box(x, 340, 116, 116, 18, `${CY}.75)`, 'rgba(6,26,52,.78)', `
  <div class="grid" style="position:absolute;inset:0"></div>
  <svg style="position:absolute;inset:0" width="112" height="112" viewBox="0 0 112 112"><path d="${dir < 0 ? 'M66 30 40 56 66 82' : 'M46 30 72 56 46 82'}" fill="none" stroke="#7fe8ff" stroke-width="8" stroke-linecap="round" stroke-linejoin="round" style="filter:drop-shadow(0 0 6px rgba(60,210,245,.9))"/></svg>`,
  'filter:drop-shadow(0 0 10px rgba(60,210,245,.35))') + `
  <div style="position:absolute;left:${dir < 0 ? x - 10 : x + 120}px;top:352px;width:6px;height:92px;background:linear-gradient(${AMBER},${AMBER} 30%,transparent 30%,transparent 40%,${AMBER} 40%,${AMBER} 70%,transparent 70%);opacity:.9;box-shadow:0 0 6px rgba(255,190,0,.6)"></div>`;
const actionBox = (selected) => box(20, 20, 340, 84, 18, selected ? AMBER : `${CY}.8)`, selected ? 'rgba(44,36,8,.84)' : 'rgba(6,26,52,.86)',
  '<div class="grid" style="position:absolute;inset:0;opacity:.6"></div>', selected ? 'filter:drop-shadow(0 0 12px rgba(255,180,0,.7))' : 'filter:drop-shadow(0 0 6px rgba(60,210,245,.25))');
const keycap = (k, w) => `<div class="num" style="position:absolute;left:2px;top:2px;width:${w - 4}px;height:32px;box-sizing:border-box;border:1.5px solid ${CY}.8);border-radius:4px;display:flex;align-items:center;justify-content:center;font-size:18px;color:#dff6ff;background:rgba(10,40,70,.6)">${k}</div>`;

const layerHtml = {
  Backdrop: `<div style="position:absolute;inset:0;background:radial-gradient(ellipse 70% 65% at 50% 42%,#0d3358 0%,#08213e 45%,#04101f 100%)"></div>
    <div style="position:absolute;inset:0;background:radial-gradient(ellipse 40% 34% at 50% 44%,rgba(60,210,245,.16),transparent 70%)"></div>`,
  Frame: `<svg style="position:absolute;left:0;top:0" width="1920" height="1080"><g fill="none" stroke="${CY}.6)" stroke-width="2">
    <path d="M60 24H1860L1896 60V1020L1860 1056H60L24 1020V60Z"/></g>
    <g fill="${CY}.9)"><rect x="760" y="20" width="160" height="5"/><rect x="1000" y="1055" width="160" height="5"/></g>
    <g fill="${CY}.5)">${Array.from({ length: 10 }, (_, i) => `<polygon points="${60 + i * 22},1040 ${74 + i * 22},1040 ${64 + i * 22},1052 ${50 + i * 22},1052"/>`).join('')}</g></svg>`,
  Header: `<div class="num" style="position:absolute;left:62px;top:40px;font-size:30px;font-style:italic;color:#8fe6ff;letter-spacing:1px;text-shadow:0 0 10px rgba(60,210,245,.6)">RE-BoarGetch</div>
    <svg style="position:absolute;left:58px;top:72px" width="230" height="16"><path d="M4 10Q110 -4 226 8" fill="none" stroke="${CY}.55)" stroke-width="2"/></svg>
    <div style="position:absolute;left:62px;top:92px;display:flex;align-items:baseline;gap:18px">
      <svg width="54" height="34"><g fill="${AMBER}"><polygon points="14,2 24,2 12,32 2,32"/><polygon points="30,2 40,2 28,32 18,32" opacity=".75"/><polygon points="46,2 54,2 44,32 34,32" opacity=".5"/></g></svg>
      <span style="font-family:'Dela Gothic One';font-size:40px;color:#f4fbff;letter-spacing:3px;text-shadow:0 2px 0 rgba(0,0,0,.4)">ステージ選択</span>
      <span class="num" style="font-size:20px;font-style:italic;letter-spacing:3px;color:${CY}.85)">/ RESEARCH LINK</span></div>
    <div style="position:absolute;left:62px;top:146px;width:440px;height:2px;background:linear-gradient(90deg,${AMBER},rgba(255,201,51,0))"></div>`,
  Dome: `<svg style="position:absolute;left:0;top:0" width="1920" height="1080"><g fill="none" stroke="${CY}.45)" stroke-width="2" stroke-dasharray="3 9">
    <path d="M470 ${PED_Y - 30} A490 430 0 0 1 1450 ${PED_Y - 30}"/></g>
    <g fill="none" stroke="${CY}.22)" stroke-width="1.5" stroke-dasharray="2 12"><path d="M420 ${PED_Y - 10} A540 480 0 0 1 1500 ${PED_Y - 10}"/></g></svg>`,
  Pedestal: `<svg style="position:absolute;left:0;top:0" width="1920" height="1080">
    <defs><radialGradient id="pg" cx=".5" cy=".5" r=".5"><stop offset="0" stop-color="rgba(60,210,245,.35)"/><stop offset="1" stop-color="rgba(60,210,245,0)"/></radialGradient></defs>
    <ellipse cx="960" cy="${PED_Y + 10}" rx="520" ry="150" fill="url(#pg)"/>
    <ellipse cx="960" cy="${PED_Y}" rx="430" ry="104" fill="rgba(4,18,36,.85)" stroke="${CY}.9)" stroke-width="3" style="filter:drop-shadow(0 0 10px rgba(60,210,245,.8))"/>
    <ellipse cx="960" cy="${PED_Y}" rx="392" ry="90" fill="none" stroke="${CY}.35)" stroke-width="2" stroke-dasharray="40 14"/>
    <ellipse cx="960" cy="${PED_Y + 24}" rx="430" ry="104" fill="none" stroke="${CY}.35)" stroke-width="2"/>
    <path d="M530 ${PED_Y} v24 M1390 ${PED_Y} v24" stroke="${CY}.5)" stroke-width="2"/>
    <g stroke="${AMBER}" stroke-width="7" fill="none" style="filter:drop-shadow(0 0 6px rgba(255,190,0,.8))">
      <path d="M610 ${PED_Y + 70} q60 22 140 32"/><path d="M1170 ${PED_Y + 102} q80 -10 140 -32"/><path d="M930 ${PED_Y + 118} h60"/></g></svg>`,
  TitlePlate: box(700, 44, 520, 118, 22, `${CY}.75)`, 'rgba(6,26,52,.86)', `
    <div class="grid" style="position:absolute;inset:0"></div>
    <div style="position:absolute;left:60px;right:60px;top:84px;height:1px;background:${CY}.35)"></div>
    <div class="num" style="position:absolute;left:150px;top:88px;color:${CY}.6);letter-spacing:-2px">/////</div>
    <div class="num" style="position:absolute;right:150px;top:88px;color:${CY}.6);letter-spacing:-2px">/////</div>`,
    'filter:drop-shadow(0 0 12px rgba(60,210,245,.35))'),
  ArrowLeft: arrowBox(330, -1),
  ArrowRight: arrowBox(1474, 1),
  InfoPanel: box(190, 724, 1540, 150, 24, `${CY}.7)`, 'rgba(6,24,48,.86)', `
    <div class="grid" style="position:absolute;inset:0"></div>
    <div style="position:absolute;left:27px;top:19px;width:238px;height:108px;box-sizing:border-box;border:1.5px solid ${CY}.6)"></div>
    <div style="position:absolute;left:790px;top:16px;width:1.5px;height:114px;background:${CY}.35)"></div>
    <div style="position:absolute;left:820px;top:53px;width:420px;height:1px;background:${CY}.2)"></div>
    <div style="position:absolute;left:820px;top:98px;width:420px;height:1px;background:${CY}.2)"></div>
    <div style="position:absolute;left:1268px;top:16px;width:1.5px;height:114px;background:${CY}.35)"></div>`),
  ThumbPlaceholder: `<div style="position:absolute;left:220px;top:746px;width:236px;height:106px;background:linear-gradient(160deg,#16405f,#0b2440 60%,#08182c)">
    <div class="grid" style="position:absolute;inset:0"></div>
    <div class="num" style="position:absolute;inset:0;display:flex;align-items:center;justify-content:center;font-size:16px;letter-spacing:6px;color:rgba(120,210,240,.5)">NO IMAGE</div></div>`,
  ClearBadge: `<div class="num" style="position:absolute;left:1494px;top:764px;width:200px;height:56px;display:flex;align-items:center;justify-content:center;font-size:36px;font-style:italic;letter-spacing:3px;color:#241600;background:linear-gradient(180deg,#ffe066,#ffb300);clip-path:polygon(12px 0,100% 0,calc(100% - 12px) 100%,0 100%);filter:drop-shadow(0 0 8px rgba(255,190,0,.6))">CLEAR</div>
    <div style="position:absolute;left:1494px;top:828px;width:200px;height:10px;background:repeating-linear-gradient(115deg,rgba(255,201,51,.5) 0 6px,transparent 6px 12px)"></div>`,
  ButtonSelected: actionBox(true),
  ButtonNormal: actionBox(false),
  IconCircle: `<svg width="64" height="64"><circle cx="32" cy="32" r="21" fill="none" stroke="#fff" stroke-width="8"/></svg>`,
  IconCross: `<svg width="64" height="64"><circle cx="32" cy="32" r="29" fill="#fff"/><path d="M21 21l22 22M43 21 21 43" stroke="#0a2036" stroke-width="7" stroke-linecap="round"/></svg>`,
  IconBook: `<svg width="64" height="56" viewBox="0 0 64 54"><path d="M4 8q14-6 28 2v40Q18 42 4 48Z" fill="#7fd8f5"/><path d="M60 8q-14-6-28 2v40q14-8 28-2Z" fill="#bfefff"/></svg>`,
  KeyEnter: keycap('Enter', 84),
  KeyEsc: keycap('Esc', 60),
};

// Live content drawn in the mockup only (the WBP shows real data here).
function dynHtml(thumb) {
  const L = LIVE, st = L.Stats;
  const stat = (y, icon, label, value) => `<div style="position:absolute;left:${st.x}px;width:${st.right - st.x}px;top:${y - 23}px;height:46px;display:flex;align-items:center">
      <div style="width:40px;display:flex;justify-content:center">${icon}</div>
      <span class="jp" style="margin-left:14px;font-size:22px;color:#cfeeff">${label}</span>
      <span class="num" style="margin-left:auto;font-size:30px;color:${AMBER};text-shadow:0 0 8px rgba(255,180,0,.45)">${value}</span></div>`;
  const btn = ([x, y, w, h], icon, label, sel) => `<div style="position:absolute;left:${x}px;top:${y}px;width:${w}px;height:${h}px;display:flex;align-items:center;justify-content:center;gap:22px">
      <img src="${icon}" style="width:44px;height:44px"><span class="jp" style="font-size:32px;color:${sel ? '#fff6dc' : '#e2f6ff'};${sel ? 'text-shadow:0 0 10px rgba(255,180,0,.6)' : ''}">${label}</span></div>`;
  return `<img src="${DIORAMA}" style="position:absolute;left:${DIO.x}px;top:${DIO.y}px;width:${DIO.w}px;height:${DIO.h}px;filter:drop-shadow(0 18px 18px rgba(0,0,0,.45))">
    <div class="num" style="position:absolute;left:0;width:1920px;top:${L.StageNumber[1] - 34}px;text-align:center;font-size:62px;font-style:italic;color:${AMBER};letter-spacing:2px;text-shadow:0 0 14px rgba(255,180,0,.5),0 3px 0 rgba(60,30,0,.6)">Stage01</div>
    <div class="jp" style="position:absolute;left:0;width:1920px;top:${L.StageName[1] - 16}px;text-align:center;font-size:22px;color:#e8f8ff">Stage 1</div>
    ${thumb ? `<div style="position:absolute;left:220px;top:746px;width:236px;height:106px;background:url('${url(thumb)}') no-repeat;background-size:540px 304px;background-position:-150px -90px"></div>` : ''}
    <div class="jp2" style="position:absolute;left:${L.Description[0]}px;top:${L.Description[1]}px;width:${L.Description[2]}px;font-size:22px;line-height:1.75;color:#e2f6ff"><span style="color:${CY}1);font-weight:800;margin-right:10px">/</span>研究所まわりの草原エリア。<br><span style="margin-left:24px">3体のボアを捕獲しよう。</span></div>
    ${stat(st.rows[0], `<span style="width:36px;height:26px;background:#bfefff;-webkit-mask:url('${BOAR}') no-repeat center/contain"></span>`, '目標捕獲数', '3')}
    ${stat(st.rows[1], `<img src="${COIN}" style="width:30px;height:30px">`, '特別コイン', '0 / 1')}
    ${stat(st.rows[2], `<img src="ICONBOOK" style="width:32px;height:28px">`, '図鑑登録', '3 / 3')}
    ${btn(L.Start, 'ICONCIRCLE', '入る', true)}${btn(L.Cancel, 'ICONCROSS', '戻る', false)}
    <div style="position:absolute;right:${1920 - L.Guide[0]}px;top:${L.Guide[1] - 18}px;display:flex;align-items:center;gap:12px">
      <img src="KEYENTER"><span class="jp2" style="font-size:19px;color:#cfeeff">で入る</span><span style="width:18px"></span>
      <img src="KEYESC"><span class="jp2" style="font-size:19px;color:#cfeeff">で戻る</span></div>`;
}

const STYLE = `.jp{font-family:'M PLUS 1p';font-weight:800;letter-spacing:2px}.jp2{font-family:'M PLUS 1p';font-weight:500;letter-spacing:1px}
.num{font-family:'Chakra Petch';font-weight:700}
.grid{background-image:linear-gradient(${CY}.06) 1px,transparent 1px),linear-gradient(90deg,${CY}.06) 1px,transparent 1px);background-size:32px 32px}`;
const FONTS = `<link href="https://fonts.googleapis.com/css2?family=Chakra+Petch:ital,wght@0,600;0,700;1,700&family=Dela+Gothic+One&family=M+PLUS+1p:wght@500;700;800&display=block" rel="stylesheet">`;
const SCENE_ORDER = ['Backdrop', 'Frame', 'Header', 'Dome', 'Pedestal', 'TitlePlate', 'ArrowLeft', 'ArrowRight', 'InfoPanel', 'ThumbPlaceholder', 'ClearBadge'];

/** Single layer page cropped to its rect (transparent) or the full mockup (iconUrl maps standalone icons to their PNGs). */
function layerPage(name) {
  const { rect: [x, y, w, h], scale = 1, standalone } = LAYERS[name];
  const W = Math.round(w * scale), H = Math.round(h * scale);
  const inner = standalone ? layerHtml[name]
    : `<div style="position:absolute;left:0;top:0;width:1920px;height:1080px;transform-origin:0 0;transform:scale(${scale}) translate(${-x}px,${-y}px)">${layerHtml[name]}</div>`;
  return { w: W, h: H, html: `<!doctype html><html><head><meta charset="utf-8">${FONTS}<style>html,body{margin:0;width:${W}px;height:${H}px;overflow:hidden;background:transparent}${STYLE}</style></head><body>${inner}</body></html>` };
}
function mockupPage(thumb, iconUrl) {
  const extraGrid = `<div style="position:absolute;inset:0;background-image:linear-gradient(${CY}.06) 1px,transparent 1px),linear-gradient(90deg,${CY}.06) 1px,transparent 1px);background-size:64px 64px"></div>
    <div style="position:absolute;inset:0;background:repeating-linear-gradient(0deg,rgba(0,0,0,.14) 0 1.5px,transparent 1.5px 4px)"></div>`;
  let body = SCENE_ORDER.map((n) => (n === 'Backdrop' ? layerHtml[n] + extraGrid : layerHtml[n])).join('\n');
  // action button frames at their places
  body += `<div style="position:absolute;left:${LIVE.Start[0] - 20}px;top:${LIVE.Start[1] - 20}px;width:380px;height:124px">${layerHtml.ButtonSelected}</div>`;
  body += `<div style="position:absolute;left:${LIVE.Cancel[0] - 20}px;top:${LIVE.Cancel[1] - 20}px;width:380px;height:124px">${layerHtml.ButtonNormal}</div>`;
  let dyn = dynHtml(thumb);
  for (const [k, n] of [['ICONBOOK', 'IconBook'], ['ICONCIRCLE', 'IconCircle'], ['ICONCROSS', 'IconCross'], ['KEYENTER', 'KeyEnter'], ['KEYESC', 'KeyEsc']]) dyn = dyn.split(k).join(iconUrl(n));
  return `<!doctype html><html><head><meta charset="utf-8">${FONTS}<style>html,body{margin:0;width:1920px;height:1080px;overflow:hidden;background:#04101f}${STYLE}</style></head><body>${body}${dyn}</body></html>`;
}

module.exports = { LAYERS, LIVE, layerPage, mockupPage, url, PED_Y };
