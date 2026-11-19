// Shared Stage Clear banner scene (1920x1080 design space). Each part is a named layer so the mockup and the
// in-game textures come from the same markup. Layer crop rects are where the WBP places each texture.
const path = require('path');
const url = (p) => 'file:///' + path.resolve(p).replace(/\\/g, '/');

const AMBER = '#ffc933';
const CY = 'rgba(60,210,245,';
const BAND_Y = 70, BAND_H = 300; // upper third, keeps the character face clear
const TY = BAND_Y + 104;          // title top (title only, no sub-labels)
const L1 = BAND_Y + 34, L2 = BAND_Y + BAND_H - 36; // cyan rails

// x, y, w, h in design space; scale < 1 renders a smaller texture that the WBP stretches back.
const LAYERS = {
  Shade: { rect: [0, 0, 1920, 1080], scale: .25 },
  Glow: { rect: [460, 90, 1000, 260] },
  Band: { rect: [0, 70, 1920, 300] },
  RailTop: { rect: [0, 92, 1920, 22] },
  RailBottom: { rect: [0, 322, 1920, 26] },
  Deco: { rect: [220, 150, 1500, 184] },
  Title: { rect: [240, 140, 1440, 200] },
  GlitchA: { rect: [240, 140, 1440, 200] },
  GlitchB: { rect: [240, 140, 1440, 200] },
  Sweep: { rect: [340, 234, 1240, 12] },
};

const corner = (x, y, sx, sy) => `<path d="M${x} ${y + sy * 28}V${y}H${x + sx * 28}" stroke="${CY}.9)" stroke-width="2" fill="none"/>
  <rect x="${x + sx * 34 - (sx < 0 ? 6 : 0)}" y="${y - 1}" width="6" height="2" fill="${CY}.9)"/>`;
const ticks = (x, y, n, dir) => Array.from({ length: n }, (_, i) =>
  `<rect x="${x + dir * i * 14}" y="${y - 6}" width="9" height="4" fill="${CY}${(.85 - i * .12).toFixed(2)})"/>`).join('');
const titleCopy = (cls, extra = '') => `<div class="t ${cls}" style="top:${TY}px;${extra}">STAGE CLEAR!</div>`;
const rail = (y) => `<div style="position:absolute;left:0;width:1920px;top:${y}px;height:2px;background:linear-gradient(90deg,transparent 4%,${CY}.85) 24%,${CY}.85) 76%,transparent 96%);box-shadow:0 0 6px ${CY}.5)"></div>`;

const layerHtml = {
  Shade: `<div style="position:absolute;inset:0;background:radial-gradient(ellipse 90% 80% at 50% 60%,transparent 55%,rgba(4,16,36,.38) 100%)"></div>
    <div style="position:absolute;left:0;right:0;top:0;height:620px;background:linear-gradient(180deg,rgba(4,18,40,.30),rgba(4,18,40,0))"></div>`,
  Glow: `<div style="position:absolute;left:460px;top:${BAND_Y + 20}px;width:1000px;height:260px;background:radial-gradient(ellipse 50% 45% at 50% 50%,rgba(255,200,60,.14),transparent 72%)"></div>`,
  Band: `<div style="position:absolute;left:0;width:1920px;top:${BAND_Y}px;height:${BAND_H}px;
    background:linear-gradient(180deg,rgba(6,24,50,.0),rgba(6,24,50,.52) 22%,rgba(6,24,50,.52) 78%,rgba(6,24,50,0));
    -webkit-mask:linear-gradient(90deg,transparent 2%,#000 22%,#000 78%,transparent 98%)">
    <div style="position:absolute;inset:0;background-image:linear-gradient(${CY}.08) 1px,transparent 1px),linear-gradient(90deg,${CY}.08) 1px,transparent 1px);background-size:24px 24px"></div>
    <div style="position:absolute;inset:0;background:repeating-linear-gradient(0deg,rgba(0,0,0,.14) 0 1.5px,transparent 1.5px 4px)"></div></div>`,
  RailTop: `${rail(L1)}<svg style="position:absolute;left:0;top:0" width="1920" height="1080">${ticks(470, L1, 6, 1)}
    <rect x="452" y="${L1 - 3}" width="8" height="8" fill="none" stroke="${CY}.9)" stroke-width="1.5" transform="rotate(45 456 ${L1 + 1})"/>
    <polygon points="1180,${L1 - 2} 1300,${L1 - 2} 1296,${L1 + 3} 1176,${L1 + 3}" fill="${AMBER}" opacity=".9"/></svg>`,
  RailBottom: `${rail(L2)}<svg style="position:absolute;left:0;top:0" width="1920" height="1080">${ticks(1440, L2 + 10, 6, -1)}
    <rect x="1460" y="${L2 - 3}" width="8" height="8" fill="none" stroke="${CY}.9)" stroke-width="1.5" transform="rotate(45 1464 ${L2 + 1})"/>
    <polygon points="620,${L2 - 2} 740,${L2 - 2} 736,${L2 + 3} 616,${L2 + 3}" fill="${AMBER}" opacity=".9"/></svg>`,
  Deco: `<svg style="position:absolute;left:0;top:0" width="1920" height="1080">
    ${corner(300, TY - 14, 1, 1)}${corner(1620, TY - 14, -1, 1)}${corner(300, TY + 150, 1, -1)}${corner(1620, TY + 150, -1, -1)}
    <g fill="${AMBER}" opacity=".7">${[[232, 1], [256, .6], [276, .35]].map(([x, o]) => `<polygon points="${x + 22},${TY + 40} ${x + 32},${TY + 40} ${x + 10},${TY + 96} ${x},${TY + 96}" opacity="${o}"/>`).join('')}
    ${[[1666, 1], [1642, .6], [1622, .35]].map(([x, o]) => `<polygon points="${x + 22},${TY + 40} ${x + 32},${TY + 40} ${x + 10},${TY + 96} ${x},${TY + 96}" opacity="${o}"/>`).join('')}</g></svg>`,
  Title: `${titleCopy('glow')}${titleCopy('ghost', 'left:-6px')}${titleCopy('edge')}${titleCopy('fill')}`,
  GlitchA: titleCopy('fill', 'left:16px;clip-path:inset(38px 0 80px 0)'),
  GlitchB: titleCopy('ghost', 'left:-14px;clip-path:inset(86px 0 34px 0);opacity:.8'),
  Sweep: `<div style="position:absolute;left:340px;width:1240px;top:${TY + 66}px;height:1px;background:linear-gradient(90deg,transparent,rgba(200,245,255,.55) 30%,rgba(200,245,255,.55) 70%,transparent)"></div>`,
};
const ORDER = ['Shade', 'Glow', 'Band', 'RailTop', 'RailBottom', 'Deco', 'Title', 'GlitchA', 'GlitchB', 'Sweep'];

const STYLE = `.t{position:absolute;left:0;width:1920px;text-align:center;font-family:'Dela Gothic One';font-size:128px;line-height:1;letter-spacing:6px;transform:skewX(-11deg)}
.glow{color:transparent;-webkit-text-stroke:8px rgba(255,170,0,.32);filter:blur(10px)}
.edge{color:transparent;-webkit-text-stroke:6px #3d2400}
.ghost{color:rgba(70,220,255,.42)}
.fill{background:repeating-linear-gradient(0deg,rgba(110,50,0,.22) 0 2px,transparent 2px 5px),
        linear-gradient(180deg,#fff8dc 0%,#ffe278 40%,#ffc21a 62%,#ffb000 100%);
      -webkit-background-clip:text;color:transparent;filter:drop-shadow(0 0 5px rgba(255,190,40,.45))}`;

/** Full page. background: CSS for the game frame (mockup) or null (transparent texture).
 *  only: render a single layer, translated so its rect starts at (0,0) and scaled by its scale. */
function sceneHtml({ background = null, only = null } = {}) {
  const names = only ? [only] : ORDER;
  let inner = names.map((n) => `<div class="layer">${layerHtml[n]}</div>`).join('\n');
  let w = 1920, h = 1080;
  if (only) {
    const { rect: [x, y, rw, rh], scale = 1 } = LAYERS[only];
    w = Math.round(rw * scale); h = Math.round(rh * scale);
    inner = `<div style="position:absolute;left:0;top:0;width:1920px;height:1080px;transform-origin:0 0;transform:scale(${scale}) translate(${-x}px,${-y}px)">${inner}</div>`;
  }
  return {
    w, h, html: `<!doctype html><html><head><meta charset="utf-8">
<link href="https://fonts.googleapis.com/css2?family=Dela+Gothic+One&display=block" rel="stylesheet">
<style>html,body{margin:0;width:${w}px;height:${h}px;overflow:hidden;background:${background ? '#000' : 'transparent'}}
.layer{position:absolute;left:0;top:0;width:1920px;height:1080px}${STYLE}</style></head><body>
${background ? `<div style="position:absolute;inset:0;${background}"></div>` : ''}
${inner}
</body></html>`,
  };
}

module.exports = { sceneHtml, LAYERS, ORDER, url };
