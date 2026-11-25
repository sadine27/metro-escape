/*
 * Metro Escape - browser front end.
 * This file only DRAWS. Every route, search trace, fire spread, Tarjan scan,
 * MST and benchmark is computed by the C engine (src/*.c) and fetched as JSON.
 */
'use strict';

/* ======================= tiny helpers ======================= */
const SVGNS = 'http://www.w3.org/2000/svg';
const $ = (s, root = document) => root.querySelector(s);
const sleep = ms => new Promise(r => setTimeout(r, ms));
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const easeInOut = t => (t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2);
const fmtTime = m => { m = ((Math.round(m) % 1440) + 1440) % 1440; return String(Math.floor(m / 60)).padStart(2, '0') + ':' + String(m % 60).padStart(2, '0'); };
const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

function svgEl(tag, attrs = {}, parent) {
  const e = document.createElementNS(SVGNS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  if (parent) parent.appendChild(e);
  return e;
}
function html(str) { const t = document.createElement('template'); t.innerHTML = str.trim(); return t.content.firstElementChild; }

const ICONS = {
  home: '<path d="M3 11l9-7 9 7"/><path d="M5 10v10h14V10"/>',
  route: '<circle cx="6" cy="18" r="2.5"/><circle cx="18" cy="6" r="2.5"/><path d="M8.5 18H15a3.5 3.5 0 0 0 0-7H9a3.5 3.5 0 0 1 0-7h6.5"/>',
  alt: '<path d="M6 3v12"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="6" r="3"/><path d="M18 9a9 9 0 0 1-9 9"/>',
  steps: '<path d="M4 20h4v-4h4v-4h4V8h4"/>',
  search: '<circle cx="11" cy="11" r="7"/><path d="M20 20l-3.5-3.5"/>',
  alert: '<path d="M12 3l9.5 17h-19z"/><path d="M12 10v4M12 17.5v.5"/>',
  flame: '<path d="M12 22c4 0 7-2.8 7-7 0-4.5-4-7-5-11-2 2-3 4-3 6-1.5-1-2-2.5-2-4-2.5 2.5-4 5.5-4 9 0 4.2 3 7 7 7z"/>',
  siren: '<path d="M7 18v-6a5 5 0 0 1 10 0v6"/><path d="M5 18h14v3H5zM12 3v2M4.2 6.2l1.4 1.4M19.8 6.2l-1.4 1.4"/>',
  shield: '<path d="M12 3l8 3v6c0 5-3.5 8-8 9-4.5-1-8-4-8-9V6z"/><path d="M12 8v5M12 16v.5"/>',
  islands: '<circle cx="7" cy="8" r="3"/><circle cx="17" cy="7" r="2.5"/><circle cx="12" cy="17" r="3.5"/>',
  wrench: '<path d="M14.7 6.3a4 4 0 0 0-5.4 5.2L3 17.8V21h3.2l6.3-6.3a4 4 0 0 0 5.2-5.4l-2.6 2.6-2.4-.6-.6-2.4z"/>',
  gauge: '<path d="M4 18a8 8 0 1 1 16 0"/><path d="M12 18l4-6"/>',
  play: '<path d="M8 5v14l11-7z"/>',
  pause: '<path d="M7 5h4v14H7zM13 5h4v14h-4z"/>',
};
const icon = n => `<svg viewBox="0 0 24 24">${ICONS[n]}</svg>`;

/* ======================= state ======================= */
let NET = null;                    // network JSON from C
const S = { tool: 'home', from: null, to: null, fire: null, me: null, pct: 140 };
let token = 0;                     // cancels running animations
const alive = t => t === token;
const map = $('#map');
let L = {};                        // svg layers
const SX = 17, SY = 42;            // terminal grid -> pixels

const P = id => { const s = NET.stations[id]; return { x: s.x * SX, y: s.y * SY }; };
const lineOf = e => NET.edges[e].line;
const colorOf = l => NET.lines[l].color;
const stName = id => NET.stations[id].name;

/* ======================= C engine API + log ======================= */
async function api(path) {
  const t0 = performance.now();
  const res = await fetch('/api/' + path);
  const data = await res.json();
  logEngine(path, data, performance.now() - t0);
  return data;
}

function logEngine(path, d, rt) {
  const ep = path.split('?')[0];
  let detail;
  if (d.dijkstra) detail = `Dijkstra ${d.dijkstra.explored} pops ${d.dijkstra.us.toFixed(1)}µs · A* ${d.astar.explored} pops ${d.astar.us.toFixed(1)}µs`;
  else if (d.stations && d.states) detail = `benchmark: ${d.stations} stations`;
  else if (d.us !== undefined) detail = `computed in ${d.us.toFixed(1)} µs`;
  else if (d.ok) detail = `network state · ${d.incidents.length} incident(s)`;
  else detail = `${rt.toFixed(0)} ms round trip`;
  const cap = $('#capEngine');
  if (cap) cap.innerHTML = `<span class="live"></span>C engine <b>/${esc(ep)}</b> ${esc(detail)}`;
  const box = $('#engineLog');
  if (!box) return;
  box.prepend(html(`<div class="log"><span class="ep">/${esc(ep)}</span><div class="d">${esc(detail)}</div></div>`));
  while (box.children.length > 3) box.lastChild.remove();
}

/* ======================= map construction ======================= */
function buildMap() {
  map.innerHTML = '';
  placeLabels();
  map.setAttribute('preserveAspectRatio', 'xMidYMid meet');

  const defs = svgEl('defs', {}, map);
  defs.innerHTML = `
    <filter id="glow" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="5"/></filter>
    <filter id="glowSoft" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="2.4"/></filter>
    <filter id="blurBig" x="-100%" y="-100%" width="300%" height="300%"><feGaussianBlur stdDeviation="9"/></filter>
    <radialGradient id="fireGlow"><stop offset="0" stop-color="#ffe08a" stop-opacity=".95"/>
      <stop offset=".4" stop-color="#ff7a2b" stop-opacity=".75"/><stop offset="1" stop-color="#ff2d2d" stop-opacity="0"/></radialGradient>
    <radialGradient id="waveGlow"><stop offset="0" stop-color="#bff8ff" stop-opacity=".9"/>
      <stop offset=".5" stop-color="#38e1ff" stop-opacity=".5"/><stop offset="1" stop-color="#38e1ff" stop-opacity="0"/></radialGradient>
    <pattern id="dots" width="24" height="24" patternUnits="userSpaceOnUse"><circle cx="1" cy="1" r="1" fill="rgba(148,163,214,0.09)"/></pattern>`;
  svgEl('rect', { x: -4000, y: -4000, width: 9000, height: 9000, fill: 'url(#dots)' }, map);

  for (const name of ['glow', 'edges', 'explore', 'spread', 'route', 'stations', 'labels', 'fx']) L[name] = svgEl('g', { class: 'layer-' + name }, map);
  L.glow.setAttribute('filter', 'url(#glow)');

  NET.edges.forEach((e, i) => {
    const a = P(e.u), b = P(e.v), c = colorOf(e.line);
    const closed = NET.stations[e.u].closed || NET.stations[e.v].closed;
    svgEl('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y, stroke: c, 'stroke-width': 13, opacity: closed ? 0 : 0.35, class: 'edge-glow', 'data-e': i }, L.glow);
    svgEl('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y, stroke: closed ? '#55607a' : c, 'stroke-width': 9, 'stroke-linecap': 'round', class: 'edge' + (closed ? ' closed' : ''), 'data-e': i }, L.edges);
    if (!closed && NET.lines[e.line].delayNow > 0)
      svgEl('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y, stroke: '#ffe9a8', 'stroke-width': 2.4, 'stroke-linecap': 'round', class: 'edge ants', opacity: 0.9 }, L.edges);
  });

  NET.stations.forEach(s => {
    const p = P(s.id), g = svgEl('g', { class: 'st', 'data-id': s.id }, L.stations);
    const big = s.lines.length > 1;
    if (s.exit) svgEl('circle', { cx: p.x, cy: p.y, r: big ? 21 : 18, fill: 'none', stroke: '#2fd27a', 'stroke-width': 2.5, opacity: 0.75, 'stroke-dasharray': '4 3' }, g);
    svgEl('circle', { cx: p.x, cy: p.y, r: big ? 12 : 9.5, fill: '#060a13', stroke: s.closed ? '#ff5a5f' : '#ffffff', 'stroke-width': big ? 4.5 : 4, class: 'ring' }, g);
    if (s.closed) {
      svgEl('path', { d: `M${p.x - 4} ${p.y - 4}L${p.x + 4} ${p.y + 4}M${p.x + 4} ${p.y - 4}L${p.x - 4} ${p.y + 4}`, stroke: '#ff5a5f', 'stroke-width': 2.5, 'stroke-linecap': 'round' }, g);
    }
    g.addEventListener('mouseenter', ev => showTip(s.id, ev));
    g.addEventListener('mousemove', ev => moveTip(ev));
    g.addEventListener('mouseleave', hideTip);
    g.addEventListener('click', ev => { ev.stopPropagation(); openPop(s.id); });

    const lp = LABELS[s.id];
    const t = svgEl('text', { x: lp.x, y: lp.y, 'text-anchor': lp.anchor, class: 'lbl', 'data-id': s.id }, L.labels);
    t.textContent = s.name;
    if (s.closed) t.style.fill = '#ff8a8d';
  });
  map.onclick = () => closePop();
  renderLegend();
  fitMap(true);
}

/* fit the map into the free area of the stage (above the dock, below the HUD), animating the viewBox */
let VB = null;
function contentBounds() {
  let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
  NET.stations.forEach(s => { const p = P(s.id); x0 = Math.min(x0, p.x - 30); x1 = Math.max(x1, p.x + 30); y0 = Math.min(y0, p.y - 60); y1 = Math.max(y1, p.y + 30); });
  LABELS.forEach(l => { if (l) { x0 = Math.min(x0, l.r.x0); x1 = Math.max(x1, l.r.x1); y0 = Math.min(y0, l.r.y0); y1 = Math.max(y1, l.r.y1); } });
  return { x0: x0 - 10, y0: y0 - 10, x1: x1 + 10, y1: y1 + 10 };
}
function fitMap(instant) {
  if (!NET) return;
  const st = $('#stage').getBoundingClientRect(), dk = $('#dock').getBoundingClientRect();
  const top = 84, side = 22, bottom = Math.max(20, st.bottom - dk.top + 8);
  const aw = st.width - 2 * side, ah = Math.max(120, st.height - top - bottom);
  const b = contentBounds(), cw = b.x1 - b.x0, ch = b.y1 - b.y0, sc = Math.min(aw / cw, ah / ch);
  const target = [b.x0 - side / sc - (aw / sc - cw) / 2, b.y0 - top / sc - (ah / sc - ch) / 2, st.width / sc, st.height / sc];
  const set = v => map.setAttribute('viewBox', v.map(n => n.toFixed(1)).join(' '));
  if (instant || !VB) { VB = target; set(VB); return; }
  const from = VB.slice();
  VB = target;
  tween(450, p => { const e = easeInOut(p); set(from.map((f, i) => f + (target[i] - f) * e)); });
}
window.addEventListener('resize', () => fitMap(true));
new ResizeObserver(() => fitMap(false)).observe($('#dock'));

/* greedy label placement: try 8 spots around each station, keep the one that
   overlaps the fewest tracks, stations and already-placed labels */
let LABELS = [];
function placeLabels() {
  const FS = 24, CW = FS * 0.62, H = FS * 1.05, GAP = 20;
  const segs = NET.edges.map(e => [P(e.u), P(e.v)]);
  const placed = [];
  const segHitsRect = (a, b, r) => {               // sample the segment: cheap and robust
    for (let k = 0; k <= 24; k++) {
      const x = a.x + (b.x - a.x) * k / 24, y = a.y + (b.y - a.y) * k / 24;
      if (x > r.x0 - 5 && x < r.x1 + 5 && y > r.y0 - 5 && y < r.y1 + 5) return true;
    }
    return false;
  };
  const overlap = (r, q) => !(r.x1 + 10 < q.x0 || q.x1 + 10 < r.x0 || r.y1 + 4 < q.y0 || q.y1 + 4 < r.y0);
  // stations with the most neighbours first: they are the hardest to label
  const order = NET.stations.map(s => s.id).sort((a, b) => NET.stations[b].lines.length - NET.stations[a].lines.length);
  LABELS = [];
  for (const id of order) {
    const s = NET.stations[id], p = P(id), w = s.name.length * CW;
    const pref = s.ly < 0 ? 'N' : s.ly > 0 ? 'S' : s.lx < 0 ? 'W' : 'E';      // the hand-tuned hint from metro.txt
    const cands = [
      ['E', p.x + GAP, p.y + FS * 0.35, 'start'], ['W', p.x - GAP, p.y + FS * 0.35, 'end'],
      ['N', p.x, p.y - GAP - 4, 'middle'], ['S', p.x, p.y + GAP + FS * 0.75, 'middle'],
      ['NE', p.x + GAP * 0.7, p.y - GAP * 0.8, 'start'], ['NW', p.x - GAP * 0.7, p.y - GAP * 0.8, 'end'],
      ['SE', p.x + GAP * 0.7, p.y + GAP * 0.8 + FS * 0.6, 'start'], ['SW', p.x - GAP * 0.7, p.y + GAP * 0.8 + FS * 0.6, 'end'],
    ];
    let best = null;
    for (const [dir, x, y, anchor] of cands) {
      const x0 = anchor === 'start' ? x : anchor === 'end' ? x - w : x - w / 2;
      const r = { x0, x1: x0 + w, y0: y - H * 0.8, y1: y + H * 0.2 };
      let cost = dir === pref ? 0 : 1.5;
      segs.forEach(([a, b]) => { if (segHitsRect(a, b, r)) cost += 4; });
      NET.stations.forEach(o => { const q = P(o.id); if (q.x > r.x0 - 14 && q.x < r.x1 + 14 && q.y > r.y0 - 14 && q.y < r.y1 + 14) cost += 8; });
      placed.forEach(q => { if (overlap(r, q)) cost += 10; });
      if (!best || cost < best.cost) best = { cost, x, y, anchor, r };
    }
    placed.push(best.r);
    LABELS[id] = best;
  }
}

function renderLegend() {
  const items = NET.lines.map(l => `<span class="lg"><i style="background:${l.color};box-shadow:0 0 10px ${l.color}"></i>${esc(l.name)}${l.delayNow ? ` <span class="delay">+${l.delayNow}′</span>` : ''}</span>`);
  items.push('<span class="lg"><span class="sym"></span>station</span>', '<span class="lg"><span class="sym exit"></span>city exit</span>');
  $('#legend').innerHTML = items.join('');
}

function renderTop() {
  $('#topStats').innerHTML = `
    <div class="stat"><b>${NET.stations.length}</b>stations</div>
    <div class="stat"><b>${NET.edges.length}</b>tracks</div>
    <div class="stat"><b>${NET.lines.length}</b>lines</div>
    <div class="stat"><b>${NET.engine.states}</b>search states</div>`;
  $('#clockInput').value = fmtTime(NET.clock);
  const n = NET.incidents.length, chip = $('#incChip');
  chip.className = 'chip ' + (n ? 'alert' : 'ok');
  chip.textContent = n ? `${n} active incident${n > 1 ? 's' : ''}` : 'All clear';
  $('#resetBtn').classList.toggle('hidden', n === 0 && NET.clock === 8 * 60 + 30 && NET.transfer === 4);
}

async function setNetwork(data) {
  if (data && data.ok === 0) { toast(data.error || 'Request failed'); return false; }
  NET = data && data.stations ? data : await api('network');
  buildMap();
  renderTop();
  return true;
}

/* ======================= tooltip + popover ======================= */
function screenOf(id) {
  const p = P(id), pt = map.createSVGPoint();
  pt.x = p.x; pt.y = p.y;
  const sp = pt.matrixTransform(map.getScreenCTM()), r = $('#stage').getBoundingClientRect();
  return { x: sp.x - r.left, y: sp.y - r.top };
}
function linePills(ls) { return ls.map(l => `<span class="pill" style="--c:${colorOf(l)}">${esc(NET.lines[l].name)}</span>`).join(''); }
function showTip(id, ev) {
  const s = NET.stations[id], tip = $('#tip');
  tip.innerHTML = `<b>${esc(s.name)}</b> <span style="color:var(--faint);font-family:var(--mono)">${s.code}</span>
    ${s.exit ? '<span style="color:var(--good);margin-left:6px">city exit</span>' : ''}${s.closed ? '<span style="color:var(--bad);margin-left:6px">closed</span>' : ''}
    <div class="lines">${linePills(s.lines)}</div>`;
  tip.classList.remove('hidden');
  moveTip(ev);
}
function moveTip(ev) {
  const r = $('#stage').getBoundingClientRect(), tip = $('#tip');
  tip.style.left = (ev.clientX - r.left + 14) + 'px';
  tip.style.top = (ev.clientY - r.top + 14) + 'px';
}
function hideTip() { $('#tip').classList.add('hidden'); }

function openPop(id) {
  hideTip();
  const s = NET.stations[id], pos = screenOf(id), pop = $('#pop');
  const acts = [
    ['#38e1ff', 'Route from here', () => { S.from = id; openTool('route'); }],
    ['#b07cff', 'Route to here', () => { S.to = id; openTool('route'); }],
    ['#ff6a2b', 'Start a fire here', () => { S.fire = id; openTool('fire'); }],
    ['#2fd27a', "I'm here (fire escape)", () => { S.me = id; openTool('fire'); }],
  ];
  if (!s.closed) acts.push(['#ff5a5f', 'Close this station', async () => { await setNetwork(await api(`incident?type=close&station=${id}`)); openTool('incidents'); }]);
  pop.innerHTML = `<div class="pop-head"><b>${esc(s.name)}</b> <span style="color:var(--faint);font-family:var(--mono);font-size:11px">${s.code}</span>
      <div class="lines" style="display:flex;gap:4px;margin-top:6px">${linePills(s.lines)}</div></div>` +
    acts.map((a, i) => `<div class="pop-item" data-i="${i}"><i style="background:${a[0]};box-shadow:0 0 8px ${a[0]}"></i>${a[1]}</div>`).join('') +
    (s.closed ? '<div class="pop-item disabled"><i style="background:#ff5a5f"></i>Closed - undo it in Incidents</div>' : '');
  pop.querySelectorAll('.pop-item[data-i]').forEach(it => it.onclick = ev => { ev.stopPropagation(); closePop(); acts[+it.dataset.i][2](); });
  const sr = $('#stage').getBoundingClientRect();
  pop.style.left = Math.min(pos.x + 14, sr.width - 230) + 'px';
  pop.style.top = Math.min(pos.y + 10, sr.height - 260) + 'px';
  pop.classList.remove('hidden');
}
function closePop() { $('#pop').classList.add('hidden'); }

/* ======================= fx primitives ======================= */
function clearFx() {
  token++;
  stopPlayer();
  for (const k of ['explore', 'spread', 'route', 'fx']) L[k] && (L[k].innerHTML = '');
  map.classList.remove('focus');
  document.querySelectorAll('.st.hl,.lbl.hl').forEach(e => e.classList.remove('hl'));
  document.querySelectorAll('.st .ring').forEach(r => { r.style.stroke = ''; r.style.fill = ''; });
  document.querySelectorAll('.lbl').forEach(t => { t.style.fill = NET.stations[+t.dataset.id].closed ? '#ff8a8d' : ''; });
  document.querySelectorAll('.edge').forEach(e => { e.style.opacity = ''; e.style.strokeDasharray = ''; });
  $('#banner').classList.add('hidden');
  $('#simclock').classList.add('hidden');
  hud('', '');
}
function hud(title, sub) { $('.hud-title').textContent = title; $('.hud-sub').textContent = sub; }
function focusOn(ids) {
  map.classList.add('focus');
  ids.forEach(id => { map.querySelector(`.st[data-id="${id}"]`).classList.add('hl'); map.querySelector(`.lbl[data-id="${id}"]`).classList.add('hl'); });
}
function ringColor(id, stroke, fill) {
  const r = map.querySelector(`.st[data-id="${id}"] .ring`);
  if (stroke != null) r.style.stroke = stroke;
  if (fill != null) r.style.fill = fill;
}
function tween(ms, fn, tok) {
  return new Promise(res => {
    const t0 = performance.now();
    const step = now => {
      if (tok !== undefined && !alive(tok)) return res();
      const p = clamp((now - t0) / ms, 0, 1);
      fn(p);
      if (p < 1) requestAnimationFrame(step); else res();
    };
    requestAnimationFrame(step);
  });
}
function burst(x, y, color, size = 90) {
  const c = svgEl('circle', { cx: x, cy: y, r: 6, fill: 'none', stroke: color, 'stroke-width': 4 }, L.fx);
  const c2 = svgEl('circle', { cx: x, cy: y, r: 6, fill: color, opacity: 0.5, filter: 'url(#glow)' }, L.fx);
  tween(900, p => {
    const e = 1 - Math.pow(1 - p, 3);
    c.setAttribute('r', 6 + e * size); c.setAttribute('opacity', 1 - p);
    c2.setAttribute('r', 6 + e * size * 0.5); c2.setAttribute('opacity', 0.5 * (1 - p));
  }).then(() => { c.remove(); c2.remove(); });
}
function banner(kind, b1, b2) {
  const b = $('#banner');
  b.className = kind;
  b.innerHTML = `<div class="b1">${b1}</div><div class="b2">${b2}</div>`;
}
function toast(msg) { hud('Notice', msg); }

/* pin markers for start / end of a route */
function pin(id, letter, color) {
  const p = P(id), g = svgEl('g', {}, L.fx);
  svgEl('circle', { cx: p.x, cy: p.y, r: 22, fill: color, opacity: 0.25, class: 'pulse' }, g);
  svgEl('circle', { cx: p.x, cy: p.y - 42, r: 17, fill: color, filter: 'url(#glowSoft)', opacity: 0.6 }, g);
  svgEl('circle', { cx: p.x, cy: p.y - 42, r: 16, fill: color }, g);
  svgEl('path', { d: `M${p.x - 8} ${p.y - 31} L${p.x} ${p.y - 14} L${p.x + 8} ${p.y - 31} Z`, fill: color }, g);
  const t = svgEl('text', { x: p.x, y: p.y - 35.5, 'text-anchor': 'middle', 'font-size': 18, 'font-weight': 800, fill: '#04101c' }, g);
  t.textContent = letter;
}

/* draw a route as glowing segments, animated in sequence */
async function drawRoute(r, tok, opts = {}) {
  if (!r.found) return;
  focusOn(r.stations);
  const segs = [];
  let total = 0;
  r.edges.forEach((e, i) => {
    const a = P(r.stations[i]), b = P(r.stations[i + 1]), len = Math.hypot(b.x - a.x, b.y - a.y);
    const color = opts.color || colorOf(lineOf(e));
    const glow = svgEl('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y, stroke: color, 'stroke-width': opts.width ? opts.width + 10 : 26, opacity: opts.dashed ? 0 : 0.4, filter: 'url(#glow)', 'stroke-linecap': 'round' }, L.route);
    const ln = svgEl('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y, stroke: color, 'stroke-width': opts.width || 13, 'stroke-linecap': 'round', opacity: opts.opacity || 1 }, L.route);
    if (opts.dashed) ln.setAttribute('stroke-dasharray', '2 13');
    segs.push({ glow, ln, len, start: total });
    total += len;
    if (!opts.dashed) { ln.style.strokeDasharray = `${len} ${len}`; ln.style.strokeDashoffset = len; glow.style.strokeDasharray = `${len} ${len}`; glow.style.strokeDashoffset = len; }
  });
  r.stations.forEach(id => ringColor(id, '#ffffff', '#ffffff'));
  if (!opts.noPins) { pin(r.stations[0], 'A', '#38e1ff'); pin(r.stations[r.stations.length - 1], 'B', '#b07cff'); }
  if (opts.dashed) return;
  await tween(opts.ms || 900, p => {
    const d = easeInOut(p) * total;
    for (const s of segs) {
      const off = s.len - clamp(d - s.start, 0, s.len);
      s.ln.style.strokeDashoffset = off; s.glow.style.strokeDashoffset = off;
    }
  }, tok);
}

/* ======================= timeline player ======================= */
const player = { T: 0, t: 0, playing: false, render: null, last: 0, raf: 0, resolve: null };
function playTimeline(T, render) {
  stopPlayer();
  Object.assign(player, { T, t: 0, render, playing: true, last: performance.now() });
  const range = $('#scrubRange');
  range.max = T; range.value = 0;
  $('#scrub').classList.remove('hidden');
  setPlayIcon();
  render(0, 0);
  player.raf = requestAnimationFrame(loop);
  return new Promise(res => { player.resolve = res; });
}
function loop(now) {
  if (!player.playing) return;
  const dt = now - player.last;
  player.last = now;
  player.t = Math.min(player.T, player.t + dt / +$('#speedSel').value);
  player.render(player.t, dt);
  $('#scrubRange').value = player.t;
  $('#scrubLabel').textContent = `T+${player.t.toFixed(1)}′`;
  if (player.t >= player.T) { player.playing = false; setPlayIcon(); finishPlayer(); return; }
  player.raf = requestAnimationFrame(loop);
}
function finishPlayer() { if (player.resolve) { const r = player.resolve; player.resolve = null; r(); } }
function stopPlayer() {
  player.playing = false;
  cancelAnimationFrame(player.raf);
  finishPlayer();
  $('#scrub').classList.add('hidden');
}
function setPlayIcon() { $('#playBtn').innerHTML = `<svg viewBox="0 0 24 24">${player.playing ? ICONS.pause : ICONS.play}</svg>`; }
$('#playBtn').onclick = () => {
  if (!player.render) return;
  if (player.playing) { player.playing = false; setPlayIcon(); return; }
  if (player.t >= player.T) player.t = 0;
  player.playing = true; player.last = performance.now(); setPlayIcon();
  player.raf = requestAnimationFrame(loop);
};
$('#scrubRange').oninput = e => {
  player.playing = false; setPlayIcon();
  player.t = +e.target.value;
  $('#scrubLabel').textContent = `T+${player.t.toFixed(1)}′`;
  player.render && player.render(player.t, 0);
};

/* ======================= route geometry for moving trains ======================= */
function leaveTime(r, i) {
  let t = r.arrive[i] - r.arrive[0];
  if (i > 0 && lineOf(r.edges[i]) !== lineOf(r.edges[i - 1])) t += NET.transfer;
  return t;
}
function posAt(r, t) {
  if (r.stations.length < 2) return { ...P(r.stations[0]), seg: 0 };
  const dep = r.arrive[0];
  let i = 0;
  while (i + 2 < r.stations.length && r.arrive[i + 1] - dep <= t) i++;
  const a = leaveTime(r, i), b = r.arrive[i + 1] - dep;
  const f = b > a ? clamp((t - a) / (b - a), 0, 1) : 1;
  const p = P(r.stations[i]), q = P(r.stations[i + 1]);
  return { x: p.x + (q.x - p.x) * f, y: p.y + (q.y - p.y) * f, seg: i };
}
function makeTrain(color, label) {
  const g = svgEl('g', {}, L.fx);
  svgEl('circle', { r: 28, fill: color, opacity: 0.4, filter: 'url(#blurBig)' }, g);
  svgEl('circle', { r: 14, fill: color, stroke: '#fff', 'stroke-width': 4 }, g);
  const core = svgEl('circle', { r: 5, fill: '#fff' }, g);
  let text = null;
  if (label) { text = svgEl('text', { y: -28, 'text-anchor': 'middle', class: 'train-label', fill: color }, g); text.textContent = label; }
  return { g, core, text, move(x, y) { g.setAttribute('transform', `translate(${x},${y})`); } };
}

/* ======================= itinerary ======================= */
function segmentsOf(r) {
  const segs = [];
  let i = 0;
  while (i < r.edges.length) {
    const ln = lineOf(r.edges[i]);
    let j = i;
    while (j < r.edges.length && lineOf(r.edges[j]) === ln) j++;
    segs.push({ line: ln, from: i, to: j });
    i = j;
  }
  return segs;
}
function itineraryHTML(r) {
  if (!r.found) return '<div class="warn">No route - the destination is cut off.</div>';
  if (r.stations.length === 1) return '<div>You are already there.</div>';
  const segs = segmentsOf(r);
  let out = '<div class="itin">';
  segs.forEach((sg, k) => {
    const c = colorOf(sg.line), st = r.stations[sg.from];
    let delay = 0;
    for (let e = sg.from; e < sg.to; e++) delay += r.arrive[e + 1] - r.arrive[e] - NET.edges[r.edges[e]].min;
    if (k > 0) {
      delay -= NET.transfer;
      out += `<div class="it-walk"><span></span><div class="rail"></div><div>walk ${NET.transfer} min to the ${esc(NET.lines[sg.line].name)} platform</div></div>`;
    }
    out += `<div class="it-stop" data-idx="${sg.from}"><div class="it-time">${fmtTime(r.arrive[sg.from])}</div><div class="it-dot" style="--c:${c}"></div>
      <div class="it-name">${esc(stName(st))}<small>${k ? 'change to' : 'board'} <span class="pill" style="--c:${c}">${esc(NET.lines[sg.line].name)}</span></small></div></div>`;
    const via = r.stations.slice(sg.from + 1, sg.to).map(stName);
    const mins = r.arrive[sg.to] - r.arrive[sg.from];
    out += `<div class="it-ride"><span></span><div class="rail" style="--c:${c}"></div><div class="info">${sg.to - sg.from} stop${sg.to - sg.from > 1 ? 's' : ''} · ${mins} min${via.length ? ' · via ' + esc(via.join(', ')) : ''}${delay > 0 ? ` <span class="warn">· +${delay} min delay/wait</span>` : ''}</div></div>`;
  });
  const last = r.stations.length - 1;
  out += `<div class="it-stop" data-idx="${last}"><div class="it-time">${fmtTime(r.arrive[last])}</div><div class="it-dot" style="--c:#fff"></div><div class="it-name">${esc(stName(r.stations[last]))}<small>arrive</small></div></div></div>`;
  return out;
}
function summaryHTML(r, label = 'Fastest route') {
  return `<div class="card"><h3>${label}</h3><div class="bigstat"><span class="n">${r.total}</span><span class="u">min</span></div>
    <div class="substats">${r.stations.length - 1} stops · ${r.transfers} transfer${r.transfers === 1 ? '' : 's'} · arrive ${fmtTime(r.arrive[r.arrive.length - 1])}</div>
    <div style="margin-top:14px">${itineraryHTML(r)}</div></div>`;
}

/* ======================= station input (trie autocomplete in C) ======================= */
function stationField(label, letter, color, get, set) {
  const f = html(`<div class="field"><label>${label}</label><span class="badge" style="background:${color}">${letter}</span>
    <input class="input" placeholder="Type a station or click the map"><div class="dd hidden"></div></div>`);
  const inp = $('input', f), dd = $('.dd', f);
  if (get() != null) inp.value = stName(get());
  let sel = 0, ids = [], seq = 0;
  async function update() {
    const q = inp.value.trim(), my = ++seq;
    if (!q) { dd.classList.add('hidden'); return; }
    const r = await api('search?q=' + encodeURIComponent(q));
    if (my !== seq) return;
    ids = r.exact >= 0 ? [r.exact, ...r.ids.filter(i => i !== r.exact)] : r.ids;
    sel = 0;
    if (!ids.length) { dd.innerHTML = '<div class="dd-foot">no match</div>'; dd.classList.remove('hidden'); return; }
    const re = new RegExp('(^|\\s)(' + q.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + ')', 'i');
    dd.innerHTML = ids.slice(0, 7).map((id, i) => `<div class="dd-item${i === sel ? ' sel' : ''}" data-id="${id}">
        ${esc(stName(id)).replace(re, '$1<mark>$2</mark>')}<span class="code">${NET.stations[id].code}</span></div>`).join('') +
      `<div class="dd-foot">trie prefix search in C · ${r.us.toFixed(1)} µs</div>`;
    dd.querySelectorAll('.dd-item').forEach(it => it.onmousedown = e => { e.preventDefault(); choose(+it.dataset.id); });
    dd.classList.remove('hidden');
  }
  function choose(id) { set(id); inp.value = stName(id); dd.classList.add('hidden'); f.dispatchEvent(new Event('picked')); }
  inp.oninput = update;
  inp.onfocus = () => inp.select();
  inp.onblur = () => setTimeout(() => dd.classList.add('hidden'), 120);
  inp.onkeydown = e => {
    if (e.key === 'Enter' && ids.length) { choose(ids[sel]); e.preventDefault(); }
    if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      sel = clamp(sel + (e.key === 'ArrowDown' ? 1 : -1), 0, Math.min(ids.length, 7) - 1);
      dd.querySelectorAll('.dd-item').forEach((it, i) => it.classList.toggle('sel', i === sel));
      e.preventDefault();
    }
  };
  return f;
}

function stationPair(onSwap, onPick) {
  const wrap = html('<div class="pair-box"></div>');
  const f1 = stationField('From', 'A', '#38e1ff', () => S.from, v => (S.from = v));
  const f2 = stationField('To', 'B', '#b07cff', () => S.to, v => (S.to = v));
  const swap = html('<button type="button" class="swap-btn" title="Swap origin and destination">⇅</button>');
  swap.onclick = () => {
    const tmp = S.from; S.from = S.to; S.to = tmp;
    $('input', f1).value = S.from != null ? stName(S.from) : '';
    $('input', f2).value = S.to != null ? stName(S.to) : '';
    if (S.from != null && S.to != null) onSwap();
  };
  [f1, f2].forEach(f => f.addEventListener('picked', () => { if (S.from != null && S.to != null) onPick(); }));
  wrap.append(f1, swap, f2);
  return wrap;
}

/* ======================= tools ======================= */
const Panel = $('#panel');
function head(title, tags, desc) {
  return `<div class="p-head"><h2>${title}</h2><div class="algo">${tags.map(t => `<span class="tag">${t}</span>`).join('')}</div></div><p class="p-desc">${desc}</p>`;
}

const Tools = {};

/* ---------- home ---------- */
Tools.home = {
  group: 'Overview', label: 'Overview', sub: 'what is running', icon: 'home',
  open() {
    const e = NET.engine;
    Panel.innerHTML = `<div class="home-hero"><h2>Nova City Metro</h2>
      <p>A transit planner and emergency simulator. All the data structures and algorithms run in a <b>C engine</b>;
      this page just draws what it sends back.</p></div>
      <div class="card"><h3>Engine boot log</h3><div class="boot">
        <div><span class="ok">✓</span>graph: ${NET.stations.length} stations, ${NET.edges.length} tracks, ${e.adjEntries} adjacency entries</div>
        <div><span class="ok">✓</span>hash table: ${e.hashBuckets} buckets, ${e.hashUsed} used, longest chain ${e.hashLongest}</div>
        <div><span class="ok">✓</span>trie: ${e.trieNodes} nodes (codes, names, words)</div>
        <div><span class="ok">✓</span>state graph: ${e.states} (station, line) states</div>
        <div><span class="ok">✓</span>segment trees: ${NET.lines.length} × ${e.segNodes} nodes, lazy propagation</div>
        <div><span class="ok">✓</span>A* heuristic: ${e.minPerUnit} min / map unit</div>
      </div></div>
      <div class="card"><h3>Try this</h3><ul class="tips" style="padding-left:18px;margin:0">
        <li><b>Story mode</b> (top right) runs an 11-act guided demo.</li>
        <li><b>Click any station</b> to route from it, set a fire, or close it.</li>
        <li><b>Fire escape</b>: an ordinary planner gets caught, Metro Escape outruns the fire.</li>
        <li>Watch the <b>C engine</b> panel (bottom right of the map) to see each computation and its time.</li>
      </ul></div>`;
    hud('Nova City Metro', `${NET.stations.length} stations · ${NET.edges.length} tracks · clock ${fmtTime(NET.clock)}`);
  },
};

/* ---------- fastest route ---------- */
Tools.route = {
  group: 'Plan', label: 'Fastest route', sub: 'Dijkstra vs A*', icon: 'route',
  open(opts = {}) {
    Panel.innerHTML = head('Fastest route', ['Dijkstra', 'A*', 'binary heap', 'state graph'],
      'Searches (station, line) states so line changes cost a transfer walk. Watch Dijkstra ripple outward, then A* aim straight at the target.');
    Panel.append(stationPair(() => this.run(), () => this.run()));
    Panel.append(html(`<label class="check"><input type="checkbox" id="animChk" ${opts.animate === false ? '' : 'checked'}> Animate the search</label>`));
    const go = html(`<button class="btn primary block">${icon('route')}Find fastest route</button>`);
    go.onclick = () => this.run();
    Panel.append(go, html('<div id="out"></div>'));
    hud('Fastest route', 'pick two stations, or click them on the map');
    if (S.from != null && S.to != null) this.run();
  },
  async run(opts = {}) {
    if (S.from == null || S.to == null) return;
    clearFx();
    const tok = token, d = await api(`route?from=${S.from}&to=${S.to}`);
    if (!alive(tok)) return;
    const out = $('#out');
    const animate = opts.animate ?? ($('#animChk') ? $('#animChk').checked : true);
    if (!d.astar.found) { out.innerHTML = '<div class="card warn">No route - the destination is cut off.</div>'; return; }
    this.last = d.astar;
    out.innerHTML = `<div class="card"><h3>Algorithm race</h3>
      <div class="race-row"><div class="race-top"><b style="color:#4c8dff">Dijkstra</b><span id="rD">0 pops</span></div><div class="bar"><i id="bD" style="background:#4c8dff"></i></div></div>
      <div class="race-row"><div class="race-top"><b style="color:#ffc53d">A* search</b><span id="rA">0 pops</span></div><div class="bar"><i id="bA" style="background:#ffc53d"></i></div></div>
      <div id="verdict"></div></div>
      <div class="card xray"><h3><span class="xr">X-RAY</span> Binary min-heap, live from C</h3><div id="heapBox"></div></div>
      <div id="itinOut"></div>`;

    const nD = d.dijkstra.trace.length, nA = d.astar.trace.length, T = nD + nA + 1, max = nD;
    let lastKey = '', finished = false;
    const bars = (kd, ka) => {
      $('#rD').textContent = `${kd} heap pops`; $('#bD').style.width = (100 * kd / max) + '%';
      $('#rA').textContent = `${ka} heap pops`; $('#bA').style.width = (100 * ka / max) + '%';
    };
    const finish = () => {
      finished = true;
      bars(nD, nA);
      const save = Math.round(100 - 100 * nA / nD);
      $('#rD').textContent = `${nD} pops · ${d.dijkstra.us.toFixed(1)} µs`;
      $('#rA').textContent = `${nA} pops · ${d.astar.us.toFixed(1)} µs`;
      $('#verdict').innerHTML = `<div class="verdict">Same answer (${d.astar.total} min) - A* did ${save}% less work</div>`;
      L.explore.innerHTML = '';
      hud(`${stName(S.from)} → ${stName(S.to)}`, `${d.astar.total} min · ${d.astar.transfers} transfer(s) · computed by A* in C`);
      if (!$('#itinOut').children.length) {
        $('#itinOut').innerHTML = summaryHTML(d.astar);
        const ride = html(`<button class="btn block" style="margin-top:10px">${icon('play')}Ride along</button>`);
        ride.onclick = () => rideRoute(d.astar);
        $('#itinOut').append(ride);
      }
      drawRoute(d.astar, token);
    };
    const unfinish = () => {
      finished = false;
      L.route.innerHTML = ''; L.fx.innerHTML = '';
      map.classList.remove('focus');
      document.querySelectorAll('.st.hl,.lbl.hl').forEach(e => e.classList.remove('hl'));
      document.querySelectorAll('.st .ring').forEach(r => { r.style.stroke = ''; r.style.fill = ''; });
      $('#verdict').innerHTML = '';
    };
    const render = (t, dt) => {
      if (t >= T - 0.001) { if (!finished) finish(); return; }
      if (finished) unfinish();
      const inA = t >= nD + 1, k = inA ? Math.min(nA, Math.floor(t - nD - 1) + 1) : Math.min(nD, Math.floor(t) + 1);
      const key = (inA ? 'A' : 'D') + k;
      if (key === lastKey) return;
      lastKey = key;
      const trace = inA ? d.astar.trace : d.dijkstra.trace, color = inA ? '#ffc53d' : '#4c8dff';
      drawExplored(trace, k, color, dt > 0);
      bars(inA ? nD : k, inA ? k : 0);
      drawHeap($('#heapBox'), (inA ? d.astarHeap : d.dijkstraHeap)[k - 1], color, inA);
      hud(inA ? 'A* search' : 'Dijkstra', inA ? `pop ${k}/${nA} · f = g + h pulls the search toward the target`
        : `pop ${k}/${nD} · expands in every direction, no idea where the target is`);
    };
    if (animate) await playTimeline(T, render, 1);
    else {
      render(T - 0.5, 0);
      render(T, 0);
      const replay = html(`<button class="btn small block" style="margin-top:10px">${icon('play')}Replay step by step</button>`);
      replay.onclick = () => playTimeline(T, render, 1);
      $('#verdict').after(replay);
    }
  },
};

/* explored region after k heap pops */
function drawExplored(trace, k, color, ping) {
  L.explore.innerHTML = '';
  const seen = new Set();
  for (let j = 0; j < k; j++) seen.add(trace[j]);
  NET.edges.forEach(e => {
    if (seen.has(e.u) && seen.has(e.v)) {
      const a = P(e.u), b = P(e.v);
      svgEl('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y, stroke: color, 'stroke-width': 9, opacity: 0.5, 'stroke-linecap': 'round' }, L.explore);
    }
  });
  seen.forEach(s => { const p = P(s); svgEl('circle', { cx: p.x, cy: p.y, r: 24, fill: color, opacity: 0.2, stroke: color, 'stroke-width': 2 }, L.explore); });
  if (k > 0) {
    const p = P(trace[k - 1]);
    svgEl('circle', { cx: p.x, cy: p.y, r: 17, fill: 'none', stroke: '#fff', 'stroke-width': 4 }, L.explore);
    if (ping) {
      const c = svgEl('circle', { cx: p.x, cy: p.y, r: 12, fill: 'none', stroke: '#fff', 'stroke-width': 3 }, L.fx);
      tween(500, q => { c.setAttribute('r', 12 + q * 40); c.setAttribute('opacity', 1 - q); }).then(() => c.remove());
    }
  }
}

/* the binary heap as a tree (first 4 levels) and as the underlying array */
function drawHeap(box, snap, color, astar) {
  if (!snap) { box.innerHTML = '<div class="p-desc" style="margin:0">Press play to watch the heap.</div>'; return; }
  const [pk, ps, pl, items] = snap;
  const W = 330, shown = Math.min(items.length, 15), levels = Math.max(1, Math.ceil(Math.log2(shown + 1))), H = 26 + levels * 50;
  const pos = i => { const lv = Math.floor(Math.log2(i + 1)), n = 2 ** lv; return { x: (i - (n - 1) + 0.5) * W / n, y: 22 + lv * 50 }; };
  const lc = l => (l >= 0 ? colorOf(l) : '#8b97b4');
  let g = '';
  for (let i = 1; i < shown; i++) { const a = pos(i), b = pos((i - 1) >> 1); g += `<line x1="${a.x}" y1="${a.y}" x2="${b.x}" y2="${b.y}" stroke="rgba(148,163,214,.25)" stroke-width="1.5"/>`; }
  for (let i = 0; i < shown; i++) {
    const [key, st, ln, stale] = items[i], p = pos(i), c = lc(ln);
    g += `<g opacity="${stale ? 0.35 : 1}"><circle cx="${p.x}" cy="${p.y}" r="15" fill="${stale ? 'transparent' : c + '33'}" stroke="${c}" stroke-width="2" ${stale ? 'stroke-dasharray="3 3"' : ''}/>
      <text x="${p.x}" y="${p.y + 4}" text-anchor="middle" font-size="11" font-weight="700" fill="#fff" font-family="JetBrains Mono, monospace">${key}</text>
      <text x="${p.x}" y="${p.y + 27}" text-anchor="middle" font-size="9" fill="#8b97b4" font-family="JetBrains Mono, monospace">${NET.stations[st].code}</text></g>`;
  }
  const stale = items.filter(x => x[3]).length;
  const cells = items.slice(0, 14).map(([key, st, ln, s], i) =>
    `<div class="hc2${s ? ' stale' : ''}" style="--c:${lc(ln)}"><i>${i}</i><b>${key}</b><span>${NET.stations[st].code}</span></div>`).join('');
  box.innerHTML = `<div class="heap-pop">popped <b style="color:${color}">${esc(stName(ps))}</b>${pl >= 0 ? ` <span class="pill" style="--c:${colorOf(pl)}">${esc(NET.lines[pl].name)}</span>` : ''}
      <span class="mono">${astar ? 'f' : 'dist'} = ${pk}</span></div>
    <svg viewBox="0 0 ${W} ${H}" width="100%">${g}</svg>
    ${items.length > 15 ? `<div class="p-desc" style="margin:0 0 6px;text-align:center">+${items.length - 15} deeper nodes</div>` : ''}
    <div class="heap-arr">${cells}${items.length > 14 ? '<div class="hc2 more">…</div>' : ''}</div>
    <div class="p-desc" style="margin:8px 0 0">size ${items.length} · <span style="color:var(--warn)">${stale} stale</span> entries (lazy deletion: skipped when popped) · node colour = line the state arrived on</div>`;
}

async function rideRoute(r) {
  clearFx();
  const tok = token;
  await drawRoute(r, tok, { ms: 400 });
  const train = makeTrain('#38e1ff', '');
  $('#simclock').classList.remove('hidden');
  const rows = [...document.querySelectorAll('#itinOut .it-stop')];
  hud('Riding', `${stName(r.stations[0])} → ${stName(r.stations[r.stations.length - 1])}`);
  await playTimeline(r.total, t => {
    const p = posAt(r, t);
    train.move(p.x, p.y);
    const ln = NET.lines[lineOf(r.edges[p.seg])];
    $('#simclock').innerHTML = `<div class="big">${fmtTime(r.arrive[0] + t)}</div><div class="small"><span style="color:${ln.color}">${esc(ln.name)}</span> → ${esc(stName(r.stations[p.seg + 1]))}</div>`;
    const activeIdx = t >= r.total ? r.stations.length - 1
      : rows.reduce((best, row) => (+row.dataset.idx <= p.seg ? +row.dataset.idx : best), 0);
    rows.forEach(row => row.classList.toggle('now', +row.dataset.idx === activeIdx));
  });
  if (alive(tok)) { const e = P(r.stations[r.stations.length - 1]); burst(e.x, e.y, '#38e1ff'); }
}

/* ---------- alternatives ---------- */
Tools.alternatives = {
  group: 'Plan', label: 'Alternative routes', sub: "Yen's K-shortest", icon: 'alt',
  K: 3,
  open() {
    Panel.innerHTML = head('Alternative routes', ["Yen's algorithm", 'spur paths', 'candidate heap'],
      'Finds the K best loopless routes. Each node of the previous best path becomes a spur; used edges are banned and a new shortest path is grown from it.');
    Panel.append(stationPair(() => this.run(), () => this.run()));
    const seg = html(`<div class="seg" style="margin-bottom:12px">${[2, 3, 4, 5].map(k => `<button data-k="${k}" class="${k === this.K ? 'on' : ''}">${k} routes</button>`).join('')}</div>`);
    seg.querySelectorAll('button').forEach(b => b.onclick = () => { this.K = +b.dataset.k; seg.querySelectorAll('button').forEach(x => x.classList.toggle('on', x === b)); this.run(); });
    const go = html(`<button class="btn primary block">${icon('alt')}Find alternatives</button>`);
    go.onclick = () => this.run();
    Panel.append(seg, go, html('<div id="out"></div>'));
    hud('Alternative routes', 'pick two stations');
    if (S.from != null && S.to != null) this.run();
  },
  async run() {
    if (S.from == null || S.to == null) return;
    clearFx();
    const tok = token, d = await api(`alternatives?from=${S.from}&to=${S.to}&k=${this.K}`);
    if (!alive(tok)) return;
    if (!d.routes.length) { $('#out').innerHTML = '<div class="card warn">No route at all.</div>'; return; }
    $('#out').innerHTML = `<div class="card"><h3>${d.routes.length} loopless routes · ${d.us.toFixed(0)} <span class="unit">µs</span> in C</h3><div class="list">${d.routes.map((r, i) => `
      <div class="alt${i === 0 ? ' on' : ''}" data-i="${i}"><div class="alt-top"><span class="rank">#${i + 1}</span><span class="t">${r.total} min</span>
      <span class="x">${r.stations.length - 1} stops · ${r.transfers} transfer${r.transfers === 1 ? '' : 's'}</span></div>
      <div class="alt-path">${segmentsOf(r).map(sg => `<span class="stn">${NET.stations[r.stations[sg.from]].code}</span><span class="pill" style="--c:${colorOf(sg.line)}">${esc(NET.lines[sg.line].name)}</span>`).join('')}<span class="stn">${NET.stations[r.stations[r.stations.length - 1]].code}</span></div></div>`).join('')}</div></div><div id="itinOut"></div>`;
    const show = async i => {
      clearFx();
      const t2 = token;
      document.querySelectorAll('.alt').forEach(a => a.classList.toggle('on', +a.dataset.i === i));
      d.routes.forEach((r, j) => { if (j !== i) drawRoute(r, t2, { dashed: true, color: '#ffffff', width: 3, opacity: 0.35, noPins: true }); });
      hud(`Route #${i + 1} of ${d.routes.length}`, `${d.routes[i].total} min · others shown dotted`);
      $('#itinOut').innerHTML = summaryHTML(d.routes[i], `Route #${i + 1}`);
      await drawRoute(d.routes[i], t2);
    };
    document.querySelectorAll('.alt').forEach(a => a.onclick = () => show(+a.dataset.i));
    this.show = show;
    await show(0);
  },
};

/* ---------- fewest stops ---------- */
Tools.fewest = {
  group: 'Plan', label: 'Fewest stops', sub: 'breadth-first search', icon: 'steps',
  open() {
    Panel.innerHTML = head('Fewest stops', ['BFS', 'queue'], 'Breadth-first search finds the route with the fewest stops. It is not always the fastest one.');
    Panel.append(stationPair(() => this.run(), () => this.run()));
    const go = html(`<button class="btn primary block">${icon('steps')}Find fewest stops</button>`);
    go.onclick = () => this.run();
    Panel.append(go, html('<div id="out"></div>'));
    hud('Fewest stops', 'pick two stations');
    if (S.from != null && S.to != null) this.run();
  },
  async run() {
    clearFx();
    const tok = token, d = await api(`fewest?from=${S.from}&to=${S.to}`);
    if (!alive(tok)) return;
    if (!d.bfs.found) { $('#out').innerHTML = '<div class="card warn">Unreachable.</div>'; return; }
    const same = d.bfs.total === d.fastest.total;
    $('#out').innerHTML = `<div class="card"><h3>BFS vs fastest</h3><div class="kv">
      <span>Fewest stops (BFS)</span><b>${d.bfs.stations.length - 1} stops · ${d.bfs.total} min</b>
      <span>Fastest (A*)</span><b>${d.fastest.stations.length - 1} stops · ${d.fastest.total} min</b></div>
      <div class="verdict" style="color:${same ? 'var(--good)' : 'var(--warn)'}">${same ? 'Here the fewest-stops route is also the fastest.' : `Fewest stops costs ${d.bfs.total - d.fastest.total} extra minutes - hops are not minutes.`}</div></div>
      ${summaryHTML(d.bfs, 'Fewest-stops route')}`;
    if (!same) drawRoute(d.fastest, tok, { dashed: true, color: '#38e1ff', width: 3, opacity: 0.6, noPins: true });
    hud('Fewest stops (BFS)', same ? 'same as the fastest route' : 'dotted cyan = fastest route');
    await drawRoute(d.bfs, tok);
  },
};

/* ---------- station search: trie path + hash buckets ---------- */
Tools.search = {
  group: 'Plan', label: 'Station search', sub: 'trie autocomplete', icon: 'search',
  open() {
    Panel.innerHTML = head('Station search', ['trie', 'hash table'], 'Every station is indexed under its code, its full name and every word, so "gat" finds all the Gates. Exact codes hit the hash table in O(1).');
    const inp = html('<input class="input plain" placeholder="Try: gat, cen, park, s ..." autofocus>');
    Panel.append(inp,
      html('<div id="out" class="card"><h3>Results</h3><div class="p-desc" style="margin:0">Start typing.</div></div>'),
      html('<div class="card xray"><h3><span class="xr">X-RAY</span> Path through the trie</h3><div id="trieBox"><div class="p-desc" style="margin:0">Type to walk the trie, one node per letter.</div></div></div>'),
      html('<div class="card xray"><h3><span class="xr">X-RAY</span> Hash table · djb2 mod 101</h3><div id="hashBox"></div></div>'));
    api('hash').then(h => { this.hash = h; drawHash(h, -1, ''); });
    let seq = 0;
    inp.oninput = async () => {
      const q = inp.value.trim(), my = ++seq;
      clearFx();
      if (!q) { $('#trieBox').innerHTML = '<div class="p-desc" style="margin:0">Type to walk the trie.</div>'; if (this.hash) drawHash(this.hash, -1, ''); return; }
      const r = await api('search?q=' + encodeURIComponent(q));
      if (my !== seq) return;
      const ids = r.exact >= 0 ? [r.exact, ...r.ids.filter(i => i !== r.exact)] : r.ids;
      $('#out').innerHTML = `<h3>${ids.length} match${ids.length === 1 ? '' : 'es'} · ${r.us.toFixed(1)} <span class="unit">µs</span> in C${r.exact >= 0 ? ' · exact code via hash' : ''}</h3><div class="list">${ids.map(id => {
        const s = NET.stations[id];
        return `<div class="inc"><b>${esc(s.name)}</b><span class="ix">${s.code}</span><span style="margin-left:auto;display:flex;gap:4px">${linePills(s.lines)}</span></div>`;
      }).join('')}</div>`;
      drawTrie(r, ids.length);
      if (this.hash) drawHash(this.hash, r.bucket, q);
      focusOn(ids);
      ids.forEach(id => { const p = P(id); svgEl('circle', { cx: p.x, cy: p.y, r: 20, fill: '#38e1ff', opacity: 0.3, class: 'pulse' }, L.fx); });
      hud(`"${q}"`, `${ids.length} station(s) found by prefix search`);
    };
    setTimeout(() => inp.focus(), 50);
    hud('Station search', 'type a prefix');
  },
};

function drawTrie(r, matches) {
  const box = $('#trieBox');
  if (!r.path.length) { box.innerHTML = '<div class="p-desc" style="margin:0;color:var(--bad)">The prefix leaves the trie: no child for one of the letters, so no matches.</div>'; return; }
  const step = 46, nx = r.next.slice(0, 7), W = Math.max(330, r.path.length * step + (nx.length ? 90 : 20)), H = Math.max(110, nx.length * 30 + 20), cy = H / 2;
  let g = '';
  r.path.forEach((n, i) => {
    const x = 22 + i * step;
    if (i) g += `<line x1="${x - step + 15}" y1="${cy}" x2="${x - 15}" y2="${cy}" stroke="#38e1ff" stroke-width="2.5"/>`;
    g += `<circle cx="${x}" cy="${cy}" r="14" fill="${i === r.path.length - 1 ? '#38e1ff' : '#0d1528'}" stroke="#38e1ff" stroke-width="2"/>
      <text x="${x}" y="${cy + 4}" text-anchor="middle" font-size="12" font-weight="800" fill="${i === r.path.length - 1 ? '#04101c' : '#e8edf7'}" font-family="JetBrains Mono">${i ? esc(n.ch) : '·'}</text>
      <text x="${x}" y="${cy + 30}" text-anchor="middle" font-size="9" fill="#8b97b4" font-family="JetBrains Mono">${n.kids.length}↓</text>`;
  });
  const lx = 22 + (r.path.length - 1) * step;
  nx.forEach((c, j) => {
    const y = nx.length === 1 ? cy : 14 + j * ((H - 28) / (nx.length - 1)), x = lx + 74;
    g += `<path d="M${lx + 14} ${cy} C${lx + 40} ${cy} ${lx + 40} ${y} ${x - 11} ${y}" fill="none" stroke="rgba(148,163,214,.35)" stroke-width="1.5"/>
      <circle cx="${x}" cy="${y}" r="11" fill="#0d1528" stroke="#8b97b4" stroke-width="1.5"/>
      <text x="${x}" y="${y + 4}" text-anchor="middle" font-size="11" fill="#e8edf7" font-family="JetBrains Mono">${esc(c.ch)}</text>
      <text x="${x + 16}" y="${y + 4}" font-size="9" fill="#5a6684" font-family="JetBrains Mono">${c.size} nodes</text>`;
  });
  box.innerHTML = `<svg viewBox="0 0 ${W + 50} ${H}" width="100%">${g}</svg>
    <div class="p-desc" style="margin:6px 0 0">Walked ${r.path.length - 1} node${r.path.length === 2 ? '' : 's'} - O(prefix length), whatever the number of stations. The ${matches} match${matches === 1 ? ' lives' : 'es live'} in the highlighted node's subtree.</div>`;
}

function drawHash(h, bucket, q) {
  const cols = 17, cell = 18, gap = 2, rows = Math.ceil(h.size / cols);
  let g = '';
  h.buckets.forEach((b, i) => {
    const x = (i % cols) * (cell + gap), y = Math.floor(i / cols) * (cell + gap);
    const fill = b.length === 0 ? 'rgba(255,255,255,0.05)' : b.length === 1 ? 'rgba(56,225,255,0.45)' : b.length === 2 ? 'rgba(255,197,61,0.7)' : 'rgba(255,90,95,0.8)';
    g += `<rect x="${x}" y="${y}" width="${cell}" height="${cell}" rx="4" fill="${fill}" ${i === bucket ? 'stroke="#fff" stroke-width="2.5"' : ''}><title>bucket ${i}: ${b.join(', ') || 'empty'}</title></rect>`;
  });
  const used = h.buckets.filter(b => b.length).length, longest = Math.max(...h.buckets.map(b => b.length));
  let detail = `<span class="mono">${used}/${h.size}</span> buckets used · longest chain <span class="mono">${longest}</span> · hover a cell to see its chain`;
  if (bucket >= 0) {
    const chain = h.buckets[bucket], code = q.toUpperCase(), hit = chain.includes(code);
    detail = `<span class="mono">djb2("${esc(code)}") mod ${h.size} = ${bucket}</span> → chain [${chain.map(c => c === code ? `<b style="color:var(--good)">${c}</b>` : c).join(' → ') || 'empty'}] · ${hit ? '<span style="color:var(--good)">found in O(1)</span>' : 'not a station code'}`;
  }
  $('#hashBox').innerHTML = `<svg viewBox="0 0 ${cols * (cell + gap)} ${rows * (cell + gap)}" width="100%">${g}</svg><div class="p-desc" style="margin:8px 0 0">${detail}</div>`;
}

/* segment tree nodes (top levels), lazy tags and the nodes the last update touched */
async function drawSegTree(box, lineIdx) {
  const d = await api(`segtree?line=${NET.lines[lineIdx].key}&depth=4`);
  const W = 330, rowH = 36, rows = 5, H = rows * rowH + 22, touched = new Map(d.touched.map(([n, f]) => [n, f]));
  let g = '';
  d.nodes.forEach(([id, lo, hi, mx, lz]) => {
    const dep = Math.floor(Math.log2(id)), x = lo / d.n * W, w = (hi - lo + 1) / d.n * W, y = dep * rowH;
    const t = touched.get(id);
    const fill = mx > 0 ? `rgba(${mx > 6 ? '255,90,95' : '255,197,61'},${(0.25 + Math.min(mx, 12) / 16).toFixed(2)})` : 'rgba(255,255,255,0.04)';
    g += `<rect x="${x + 1}" y="${y + 1}" width="${w - 2}" height="${rowH - 6}" rx="5" fill="${fill}"
      stroke="${t !== undefined ? '#38e1ff' : 'rgba(148,163,214,0.22)'}" stroke-width="${t ? 2.6 : t === 0 ? 1.6 : 1}" ${t === 0 ? 'stroke-dasharray="4 3"' : ''}>
      <title>node ${id} · ${fmtTime(lo)}-${fmtTime(hi + 1)} · max ${mx} · lazy ${lz}</title></rect>`;
    if (w > 15) g += `<text x="${x + w / 2}" y="${y + rowH / 2 + 1}" text-anchor="middle" font-size="10" font-weight="700" fill="${mx ? '#fff' : '#5a6684'}" font-family="JetBrains Mono">${mx}</text>`;
    if (lz) g += `<rect x="${x + w - 24}" y="${y - 3}" width="22" height="13" rx="4" fill="#b07cff"/><text x="${x + w - 13}" y="${y + 7}" text-anchor="middle" font-size="9" font-weight="800" fill="#fff" font-family="JetBrains Mono">+${lz}</text>`;
  });
  const cx = d.clock / d.n * W;
  g += `<line x1="${cx}" y1="0" x2="${cx}" y2="${rows * rowH - 4}" stroke="#fff" stroke-width="1.5" stroke-dasharray="3 3" opacity=".8"/>`;
  [0, 6, 12, 18, 24].forEach(hh => { g += `<text x="${Math.min(W - 8, Math.max(8, hh * 60 / d.n * W))}" y="${rows * rowH + 12}" text-anchor="middle" font-size="9" fill="#5a6684" font-family="JetBrains Mono">${hh}h</text>`; });
  const full = d.touched.filter(t => t[1]).length, part = d.touched.length - full, deep = d.touched.filter(t => t[0] > 31).length;
  box.innerHTML = `<svg viewBox="-4 -6 ${W + 8} ${H + 6}" width="100%">${g}</svg>
    <div class="p-desc" style="margin:8px 0 0">${d.touched.length ? `The last delay on this line touched <b>${d.touched.length}</b> nodes: <span style="color:#38e1ff">${full} fully covered</span> (got a <span style="color:#b07cff">lazy tag</span>, children untouched) and ${part} split in two. ${deep} of them are deeper than shown - that is the O(log n).`
      : 'No delay has been added to this line yet. Add one above and watch which nodes it touches.'}
      Dashed line = now: a point query walks root → leaf along it, pushing lazy tags down.</div>`;
}

/* ---------- incidents ---------- */
Tools.incidents = {
  group: 'Disrupt', label: 'Incidents & delays', sub: 'stack + segment tree', icon: 'alert',
  line: 2,
  open() {
    Panel.innerHTML = head('Incidents & delays', ['stack (undo)', 'lazy segment tree'],
      'Close stations or add a delay to a line for a time window. Each delay is one O(log n) range update on that line\'s segment tree. Incidents form a stack, so undo pops the latest.');
    let st = null;
    const closeF = stationField('Close a station', '×', '#ff5a5f', () => st, v => (st = v));
    const closeBtn = html('<button class="btn small block">Close station</button>');
    closeBtn.onclick = async () => { if (st != null) { await setNetwork(await api(`incident?type=close&station=${st}`)); this.open(); } };
    const c1 = html('<div class="card"><h3>Closure</h3></div>');
    c1.append(closeF, closeBtn);
    const c2 = html(`<div class="card"><h3>Delay window</h3>
      <div class="seg" id="lineSeg" style="margin-bottom:10px">${NET.lines.map((l, i) => `<button data-l="${i}" class="${i === this.line ? 'on' : ''}" style="${i === this.line ? `color:${l.color}` : ''}">${esc(l.name)}</button>`).join('')}</div>
      <div class="row"><div class="field"><label>From</label><input class="input plain" type="time" id="dFrom" value="08:00" style="color-scheme:dark"></div>
      <div class="field"><label>To</label><input class="input plain" type="time" id="dTo" value="10:00" style="color-scheme:dark"></div>
      <div class="field"><label>+ min per hop</label><input class="input plain" type="number" id="dAmt" value="7" min="1" max="120"></div></div>
      <button class="btn small block" id="dGo">Add delay</button></div>`);
    c2.querySelectorAll('#lineSeg button').forEach(b => b.onclick = () => { this.line = +b.dataset.l; this.open(); });
    const toMin = v => { const [h, m] = v.split(':').map(Number); return h * 60 + m; };
    $('#dGo', c2).onclick = async () => {
      const f = toMin($('#dFrom', c2).value), t = toMin($('#dTo', c2).value);
      await setNetwork(await api(`incident?type=delay&line=${NET.lines[this.line].key}&from=${f}&to=${t}&amount=${$('#dAmt', c2).value}`));
      $('#app').dataset.lastDelay = this.line;
      this.open();
    };
    const inc = NET.incidents.slice().reverse();
    const c3 = html(`<div class="card"><h3>Incident stack · ${inc.length}</h3><div class="list">${inc.length ? inc.map((x, i) => `<div class="inc${i === 0 ? ' top' : ''}"><span class="ix">${i === 0 ? 'top' : '#' + (inc.length - i)}</span>${x.type === 'close'
      ? `<span><b style="color:var(--bad)">Closed</b> ${esc(stName(x.station))}</span>`
      : `<span><b style="color:${colorOf(x.line)}">${esc(NET.lines[x.line].name)}</b> +${x.amount} min · ${fmtTime(x.from)}-${fmtTime(x.to)}</span>`}</div>`).join('') : '<div class="p-desc" style="margin:0">No incidents - the network is running normally.</div>'}</div>
      <button class="btn small block" id="undo" style="margin-top:10px" ${inc.length ? '' : 'disabled'}>Undo last (pop)</button></div>`);
    $('#undo', c3).onclick = async () => { await setNetwork(await api('undo')); this.open(); };
    const c4 = html(`<div class="card xray"><h3><span class="xr">X-RAY</span> ${esc(NET.lines[this.line].name)} line's segment tree (top 5 levels)</h3><div id="segBox"></div></div>`);
    Panel.append(c1, c2, c3, c4, html('<div class="card" id="heat"><h3>Delay timeline · segment tree range-max</h3></div>'));
    drawSegTree($('#segBox'), this.line).then(() => this.heat());
    clearFx();
    hud('Incidents & delays', `${NET.incidents.length} active · clock ${fmtTime(NET.clock)}`);
  },
  async heat() {
    const d = await api('timeline'), hour = Math.floor(NET.clock / 60);
    let g = '<div class="heat"><span></span>' + Array.from({ length: 24 }, (_, h) => `<span class="hh">${h % 3 === 0 ? h : ''}</span>`).join('');
    d.lines.forEach((l, i) => {
      g += `<span class="hn" style="color:${colorOf(i)}">${esc(NET.lines[i].name)}</span>` + l.hours.map((v, h) => {
        const a = v ? clamp(0.25 + v / 12, 0, 1) : 0;
        return `<span class="hc${h === hour ? ' now' : ''}" title="${h}:00 - max +${v} min" style="${v ? `background:rgba(${v > 6 ? '255,90,95' : '255,197,61'},${a})` : ''}"></span>`;
      }).join('');
    });
    $('#heat').insertAdjacentHTML('beforeend', g + '</div>');
  },
};

/* ---------- fire escape ---------- */
Tools.fire = {
  group: 'Emergency', label: 'Fire escape', sub: 'deadline Dijkstra', icon: 'flame', hot: true,
  open(opts = {}) {
    Panel.innerHTML = head('Fire escape', ['multi-source Dijkstra', 'deadline-constrained Dijkstra', 'FIFO'],
      'A fire spreads along the tracks. An ordinary planner sends you to the nearest exit. Metro Escape only uses stations it can reach <b>before</b> the fire does.');
    Panel.append(stationField('Fire starts at', '!', '#ff6a2b', () => S.fire, v => (S.fire = v)));
    Panel.append(stationField('You are at', '★', '#2fd27a', () => S.me, v => (S.me = v)));
    const speeds = [[200, 'Slow'], [140, 'Medium'], [100, 'Fast']];
    const seg = html(`<div class="field"><label>Fire speed</label><div class="seg">${speeds.map(([p, n]) => `<button data-p="${p}" class="${p === S.pct ? 'on' : ''}">${n}</button>`).join('')}</div></div>`);
    seg.querySelectorAll('button').forEach(b => b.onclick = () => { S.pct = +b.dataset.p; seg.querySelectorAll('button').forEach(x => x.classList.toggle('on', x === b)); });
    const go = html(`<button class="btn danger block">${icon('flame')}Start the fire</button>`);
    go.onclick = () => this.run();
    const auto = html('<button class="btn block" style="margin-top:8px">Surprise me (C picks a dramatic scenario)</button>');
    auto.onclick = () => { S.fire = S.me = null; this.run(); };
    Panel.append(seg, go, auto, html('<div id="out"></div>'));
    hud('Fire escape', 'choose where the fire starts and where you are');
    if (opts.autorun || (S.fire != null && S.me != null)) this.run();
  },
  async run() {
    clearFx();
    const tok = token;
    const q = S.fire != null && S.me != null && S.fire !== S.me ? `fire=${S.fire}&me=${S.me}&pct=${S.pct}` : `pct=${S.pct}`;
    const d = await api('fire?' + q);
    if (!alive(tok) || d.ok === 0) return;
    S.fire = d.fire; S.me = d.me;
    Panel.querySelectorAll('.field input')[0].value = stName(d.fire);
    Panel.querySelectorAll('.field input')[1].value = stName(d.me);
    const naive = d.naive, smart = d.smart, caught = d.naiveCaught;
    const ex = r => r.found ? stName(r.stations[r.stations.length - 1]) : '-';
    $('#out').innerHTML = `
      <div class="plan" id="planA"><div class="ph"><span class="dotc" style="--c:#ffc53d"></span><b>Plan A · ordinary planner</b></div>
        <div class="tagline">${naive.found ? `nearest exit: ${esc(ex(naive))} in ${naive.total} min, ignores the fire` : 'no exit reachable'}</div><div class="res" id="resA">…</div></div>
      <div class="plan" id="planB"><div class="ph"><span class="dotc" style="--c:#2fd27a"></span><b>Plan B · Metro Escape</b></div>
        <div class="tagline">${smart.found ? `${esc(ex(smart))} in ${smart.total} min, never enters a station after the fire` : 'no safe route exists'}</div><div class="res" id="resB">…</div></div>
      <div class="card"><h3>How the C engine did it · ${d.us.toFixed(0)} <span class="unit">µs</span></h3><div class="p-desc" style="margin:0">
        1. Dijkstra seeded at the fire gives the minute the fire reaches every station.<br>
        2. Dijkstra over (station, line) states to <i>any</i> exit, pruning every move that arrives at or after the fire.
        Arrival times are FIFO, so arriving earlier is never worse and the pruned search is still exact.</div></div>`;

    // static layers: routes
    if (naive.found) drawRoute(naive, tok, { dashed: true, color: '#ffc53d', width: 4, opacity: 0.9, noPins: true });
    if (smart.found) drawRoute(smart, tok, { dashed: true, color: '#2fd27a', width: 4, opacity: 0.9, noPins: true });
    map.classList.remove('focus');
    const fp = P(d.fire);
    burst(fp.x, fp.y, '#ff6a2b', 110);
    hud(`Fire at ${stName(d.fire)}`, `you are at ${stName(d.me)} · the fire reaches you in ${d.burn[d.me]} min`);
    await sleep(900);
    if (!alive(tok)) return;

    // fire geometry: two flame lines per track (one from each end)
    const flames = NET.edges.map(e => {
      const a = P(e.u), b = P(e.v);
      const mk = (p) => [svgEl('line', { x1: p.x, y1: p.y, x2: p.x, y2: p.y, stroke: '#ff5a1f', 'stroke-width': 22, opacity: 0.55, filter: 'url(#glow)', 'stroke-linecap': 'round' }, L.spread),
        svgEl('line', { x1: p.x, y1: p.y, x2: p.x, y2: p.y, stroke: '#ffd27a', 'stroke-width': 6, 'stroke-linecap': 'round' }, L.spread)];
      return { e, a, b, W: Math.floor((e.min * d.pct + 99) / 100), fu: mk(a), fv: mk(b) };
    });
    const glows = NET.stations.map(s => { const p = P(s.id); return svgEl('circle', { cx: p.x, cy: p.y, r: 0, fill: 'url(#fireGlow)' }, L.spread); });
    const embers = [];
    const tA = naive.found ? makeTrain('#ffc53d', 'Plan A') : null, tB = smart.found ? makeTrain('#2fd27a', 'Plan B') : null;
    const endA = naive.found ? (caught >= 0 ? caught : naive.total) : 0, endB = smart.found ? smart.total : 0;
    const T = Math.max(endA, endB) + 3;
    let doneA = false, doneB = false;
    $('#simclock').classList.remove('hidden');

    await playTimeline(T, (t, dt) => {
      let burning = 0;
      flames.forEach(f => {
        const bu = d.burn[f.e.u], bv = d.burn[f.e.v];
        const pu = bu >= 0 && t > bu ? clamp((t - bu) / f.W, 0, 1) : 0, pv = bv >= 0 && t > bv ? clamp((t - bv) / f.W, 0, 1) : 0;
        const flick = 0.85 + 0.15 * Math.sin(t * 9 + f.a.x);
        for (const ln of f.fu) { ln.setAttribute('x2', f.a.x + (f.b.x - f.a.x) * pu); ln.setAttribute('y2', f.a.y + (f.b.y - f.a.y) * pu); }
        for (const ln of f.fv) { ln.setAttribute('x2', f.b.x + (f.a.x - f.b.x) * pv); ln.setAttribute('y2', f.b.y + (f.a.y - f.b.y) * pv); }
        f.fu[0].setAttribute('opacity', 0.55 * flick); f.fv[0].setAttribute('opacity', 0.55 * flick);
      });
      NET.stations.forEach(s => {
        const b = d.burn[s.id], on = b >= 0 && t >= b;
        if (on) burning++;
        glows[s.id].setAttribute('r', on ? 30 + 7 * Math.sin(t * 7 + s.id) : 0);
        ringColor(s.id, on ? '#ff6a2b' : '');
        if (on && dt > 0 && Math.random() < dt / 90) {
          const p = P(s.id);
          embers.push({ el: svgEl('circle', { cx: p.x, cy: p.y, r: 2 + Math.random() * 2.5, fill: Math.random() < 0.5 ? '#ffd27a' : '#ff7a2b' }, L.spread), x: p.x, y: p.y, vx: (Math.random() - 0.5) * 0.04, vy: -0.03 - Math.random() * 0.05, life: 1 });
        }
      });
      for (let i = embers.length - 1; i >= 0; i--) {
        const m = embers[i];
        m.life -= dt / 1400; m.x += m.vx * dt; m.y += m.vy * dt;
        if (m.life <= 0) { m.el.remove(); embers.splice(i, 1); continue; }
        m.el.setAttribute('cx', m.x); m.el.setAttribute('cy', m.y); m.el.setAttribute('opacity', m.life);
      }
      if (tA) {
        const p = posAt(naive, Math.min(t, endA));
        tA.move(p.x, p.y);
        if (caught >= 0 && t >= caught) {
          tA.text.textContent = 'CAUGHT'; tA.text.setAttribute('fill', '#ff5a5f');
          tA.g.children[1].setAttribute('fill', '#ff5a5f');
          if (!doneA && dt > 0) { burst(p.x, p.y, '#ff5a5f', 90); banner('bad', 'Plan A caught', `the fire reached the train after ${caught.toFixed(1)} min`); }
          if (!doneA) { doneA = true; $('#planA').classList.add('bad'); $('#resA').textContent = `Caught by the fire after ${caught.toFixed(1)} min`; }
        } else if (caught < 0 && t >= endA && !doneA) { doneA = true; $('#planA').classList.add('good'); $('#resA').textContent = 'Survives'; }
        else if (t < endA) { tA.text.textContent = 'Plan A'; tA.text.setAttribute('fill', '#ffc53d'); tA.g.children[1].setAttribute('fill', '#ffc53d'); }
      }
      if (tB) {
        const p = posAt(smart, Math.min(t, endB));
        tB.move(p.x, p.y);
        if (t >= endB && !doneB) {
          doneB = true;
          if (dt > 0) { burst(p.x, p.y, '#2fd27a', 120); banner('good', 'Escaped', `through ${esc(ex(smart))} · always at least ${d.margin} min ahead of the fire`); }
          $('#planB').classList.add('good'); $('#resB').textContent = `Escaped · ${d.margin} min ahead of the fire at its closest`;
          tB.text.textContent = 'ESCAPED';
        }
      }
      if (!smart.found && t >= T - 0.1 && !doneB) { doneB = true; $('#planB').classList.add('bad'); $('#resB').textContent = `No safe route · shelter at ${stName(d.shelter)}`; }
      $('#simclock').innerHTML = `<div class="big">${fmtTime(NET.clock + t)}</div><div class="small"><span style="color:#ff6a2b">${burning}/${NET.stations.length} stations burning</span> · T+${t.toFixed(1)}′</div>`;
    });
  },
};

/* ---------- evacuation ---------- */
Tools.evacuate = {
  group: 'Emergency', label: 'Evacuate the city', sub: 'multi-source Dijkstra', icon: 'siren',
  async open() {
    Panel.innerHTML = head('Evacuate the city', ['multi-source Dijkstra', 'shortest-path forest', 'heap ordering'],
      'One Dijkstra seeded at every exit at once. The wave it sweeps out gives every station its nearest exit and the time to reach it. The same code spreads the fire.');
    Panel.append(html('<div id="out"></div>'));
    clearFx();
    const tok = token, d = await api('evacuate');
    if (!alive(tok)) return;
    const order = NET.stations.map(s => s.id).filter(i => !NET.stations[i].closed && !NET.stations[i].exit && d.time[i] >= 0).sort((a, b) => d.time[a] - d.time[b]);
    const trapped = NET.stations.filter(s => !s.closed && d.time[s.id] < 0);
    const times = d.time.filter(v => v >= 0);
    const maxT = times.length ? Math.max(...times) : 0;
    const col = t => t <= 8 ? '#2fd27a' : t <= 16 ? '#ffc53d' : '#ff5a5f';
    $('#out').innerHTML = `<div class="card"><div class="kv"><span>Slowest station</span><b>${order.length ? esc(stName(order[order.length - 1])) + ' · ' + maxT + ' min' : '-'}</b>
      <span>Trapped (no exit reachable)</span><b style="color:${trapped.length ? 'var(--bad)' : 'var(--good)'}">${trapped.length ? trapped.map(s => s.code).join(' ') : 'none'}</b>
      <span>Computed in C</span><b>${d.us.toFixed(1)} µs</b></div></div>
      <div class="card"><h3>Evacuation order (fastest first)</h3><div class="evac-list">${order.map(i => `<div class="ev" data-id="${i}" style="opacity:.25"><b style="color:${col(d.time[i])}">${d.time[i]}′</b>${NET.stations[i].code}<span>→ ${NET.stations[d.exitOf[i]].code}</span></div>`).join('')}</div></div>`;
    hud('Rescue wave', 'spreading from every exit at once');
    const waves = NET.edges.map(e => {
      const a = P(e.u), b = P(e.v), skip = NET.stations[e.u].closed || NET.stations[e.v].closed;
      const mk = p => [svgEl('line', { x1: p.x, y1: p.y, x2: p.x, y2: p.y, stroke: '#38e1ff', 'stroke-width': 20, opacity: 0.35, filter: 'url(#glow)', 'stroke-linecap': 'round' }, L.spread),
        svgEl('line', { x1: p.x, y1: p.y, x2: p.x, y2: p.y, stroke: '#bff8ff', 'stroke-width': 4.5, 'stroke-linecap': 'round' }, L.spread)];
      return { e, a, b, skip, fu: mk(a), fv: mk(b) };
    });
    const rows = new Map([...document.querySelectorAll('.ev')].map(r => [+r.dataset.id, r]));
    const shown = new Set();
    $('#simclock').classList.remove('hidden');
    await playTimeline(maxT + 1, (t, dt) => {
      waves.forEach(w => {
        if (w.skip) return;
        const bu = d.time[w.e.u], bv = d.time[w.e.v];
        const pu = bu >= 0 && t > bu ? clamp((t - bu) / w.e.min, 0, 1) : 0, pv = bv >= 0 && t > bv ? clamp((t - bv) / w.e.min, 0, 1) : 0;
        for (const ln of w.fu) { ln.setAttribute('x2', w.a.x + (w.b.x - w.a.x) * pu); ln.setAttribute('y2', w.a.y + (w.b.y - w.a.y) * pu); }
        for (const ln of w.fv) { ln.setAttribute('x2', w.b.x + (w.a.x - w.b.x) * pv); ln.setAttribute('y2', w.b.y + (w.a.y - w.b.y) * pv); }
      });
      let reached = 0;
      NET.stations.forEach(s => {
        const tt = d.time[s.id];
        if (tt >= 0 && t >= tt) {
          reached++;
          ringColor(s.id, col(tt), col(tt));
          if (!shown.has(s.id) && dt > 0) { shown.add(s.id); const p = P(s.id); burst(p.x, p.y, col(tt), 30); }
          const row = rows.get(s.id);
          if (row) row.style.opacity = 1;
        } else { ringColor(s.id, s.closed ? '#ff5a5f' : '#ffffff', '#060a13'); const row = rows.get(s.id); if (row) row.style.opacity = 0.25; }
      });
      $('#simclock').innerHTML = `<div class="big">+${t.toFixed(1)} min</div><div class="small">${reached}/${NET.stations.length} stations reached</div>`;
    });
    if (!alive(tok)) return;
    // final: the escape tree (parent pointers from C)
    L.spread.innerHTML = '';
    d.parentEdge.forEach((e, v) => {
      if (e < 0) return;
      const a = P(NET.edges[e].u), b = P(NET.edges[e].v);
      svgEl('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y, stroke: '#38e1ff', 'stroke-width': 10, 'stroke-linecap': 'round', opacity: 0.85 }, L.spread);
    });
    trapped.forEach(s => { const p = P(s.id); svgEl('circle', { cx: p.x, cy: p.y, r: 22, fill: '#ff5a5f', opacity: 0.35, class: 'pulse' }, L.fx); });
    hud('Escape tree', 'cyan = the shortest path from every station to its nearest exit');
  },
};

/* ---------- critical stations: Tarjan DFS replay ---------- */
Tools.critical = {
  group: 'Emergency', label: 'Critical stations', sub: 'Tarjan articulation points', icon: 'shield',
  async open() {
    Panel.innerHTML = head('Critical stations', ['Tarjan DFS', 'low-link values', 'union-find impact', 'heap ranking'],
      'A station is critical if closing it splits the network. One DFS finds them all: <b>u</b> is critical when a DFS child <b>v</b> has <b>low[v] ≥ disc[u]</b> - nothing below v can climb back above u.');
    Panel.append(html('<div id="out"></div>'));
    clearFx();
    const tok = token, d = await api('critical');
    if (!alive(tok)) return;
    const max = Math.max(1, ...d.impact.map(x => x.stranded));
    $('#out').innerHTML = `<div class="card xray"><h3><span class="xr">X-RAY</span> Tarjan's DFS, replayed from C</h3>
        <div class="dfs-legend"><span><i class="lg-badge">d|l</i> disc | low</span><span><i style="background:#fff"></i>tree edge</span><span><i style="background:#ffc53d"></i>back edge</span><span><i style="background:#ff5a5f"></i>bridge</span></div>
        <div id="dfsLog" class="dfs-log"></div></div>
      <div class="card"><h3>${d.impact.length} single points of failure · ${d.us.toFixed(1)} <span class="unit">µs</span></h3>${d.impact.map(x => `
        <div class="impact"><span>${esc(stName(x.station))}</span><div class="bar"><i style="width:${100 * x.stranded / max}%;background:linear-gradient(90deg,#ff5a5f,#ff9a5f)"></i></div><b>${x.stranded}</b></div>`).join('')}
        <div class="p-desc" style="margin:10px 0 0">Bar = stations cut off if it closes (union-find on the network without it).</div></div>`;
    map.classList.add('focus');
    const ev = d.dfs, n = ev.length;
    const say = e => {
      const [t, a, b, c] = e, N = i => `<b>${esc(stName(i))}</b>`;
      if (t === 0) return `visit ${N(a)} · disc = ${c}`;
      if (t === 1) return `<span style="color:#ffc53d">back edge</span> ${N(a)} → ${N(b)} · low[${NET.stations[a].code}] = ${c}`;
      if (t === 2) return `return to ${N(a)} from ${N(b)} · low[${NET.stations[a].code}] = ${c}`;
      if (t === 3) return b < 0 ? `<span style="color:#ff5a5f">${N(a)} is critical</span>: DFS root with ${c} children`
        : `<span style="color:#ff5a5f">${N(a)} is critical</span>: low[${NET.stations[b].code}] = ${c} ≥ disc[${NET.stations[a].code}]`;
      return `<span style="color:#ff5a5f">bridge</span> ${N(b)}–${N(c)}: no back edge around it`;
    };
    let last = -1;
    const render = (t, dt) => {
      const k = Math.min(n, Math.floor(t * 3));
      if (k === last) return;
      last = k;
      const disc = {}, low = {}, art = new Set(), br = new Set(), tree = [], back = [];
      for (let i = 0; i < k; i++) {
        const [ty, a, b, c] = ev[i];
        if (ty === 0) { disc[a] = c; low[a] = c; if (b >= 0) tree.push(b); }
        else if (ty === 1) { low[a] = c; back.push([a, b]); }
        else if (ty === 2) low[a] = c;
        else if (ty === 3) art.add(a);
        else br.add(a);
      }
      L.explore.innerHTML = ''; L.fx.innerHTML = '';
      tree.forEach(e => {
        const a = P(NET.edges[e].u), b = P(NET.edges[e].v), isB = br.has(e);
        if (isB) svgEl('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y, stroke: '#ff5a5f', 'stroke-width': 22, opacity: 0.45, filter: 'url(#glow)' }, L.explore);
        svgEl('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y, stroke: isB ? '#ff5a5f' : '#ffffff', 'stroke-width': 7, opacity: 0.9, 'stroke-linecap': 'round' }, L.explore);
      });
      back.forEach(([u, v]) => {
        const a = P(u), b = P(v), mx = (a.x + b.x) / 2 + (b.y - a.y) * 0.18, my = (a.y + b.y) / 2 - (b.x - a.x) * 0.18;
        svgEl('path', { d: `M${a.x} ${a.y} Q${mx} ${my} ${b.x} ${b.y}`, fill: 'none', stroke: '#ffc53d', 'stroke-width': 4, 'stroke-dasharray': '7 7', opacity: 0.95 }, L.explore);
      });
      NET.stations.forEach(s => {
        const on = disc[s.id] !== undefined;
        map.querySelector(`.st[data-id="${s.id}"]`).classList.toggle('hl', on);
        map.querySelector(`.lbl[data-id="${s.id}"]`).classList.toggle('hl', on);
        ringColor(s.id, art.has(s.id) ? '#ff5a5f' : on ? '#ffffff' : '', art.has(s.id) ? '#ff5a5f' : '');
        if (!on) return;
        const p = P(s.id), txt = `${disc[s.id]}|${low[s.id]}`, w = txt.length * 12.5 + 14;
        const g = svgEl('g', {}, L.fx);
        svgEl('rect', { x: p.x - w / 2, y: p.y + 16, width: w, height: 27, rx: 8, fill: art.has(s.id) ? '#ff5a5f' : '#0d1528', stroke: art.has(s.id) ? '#ff5a5f' : '#38e1ff', 'stroke-width': 1.5 }, g);
        const tx = svgEl('text', { x: p.x, y: p.y + 35, 'text-anchor': 'middle', 'font-size': 19, 'font-weight': 700, fill: art.has(s.id) ? '#fff' : '#bff8ff', 'font-family': 'JetBrains Mono, monospace' }, g);
        tx.textContent = txt;
        if (art.has(s.id)) svgEl('circle', { cx: p.x, cy: p.y, r: 26, fill: '#ff5a5f', opacity: 0.3, class: 'pulse' }, L.fx);
      });
      if (k > 0) {
        const [ty, a] = ev[k - 1], who = ty === 4 ? NET.edges[a].u : a, p = P(who);
        if (dt > 0) burst(p.x, p.y, ty >= 3 ? '#ff5a5f' : '#38e1ff', 40);
      }
      const logs = [];
      for (let i = k - 1; i >= Math.max(0, k - 7); i--) logs.push(`<div class="${i === k - 1 ? 'now' : ''}"><span class="ix">${i + 1}</span>${say(ev[i])}</div>`);
      $('#dfsLog').innerHTML = logs.join('') || '<div>press play</div>';
      hud("Tarjan's DFS", `event ${k}/${n} · badge = disc | low`);
    };
    await playTimeline(n / 3 + 0.01, render, 1);
    if (alive(tok)) hud('Single points of failure', `${d.impact.length} critical stations · ${d.bridges.length} bridges`);
  },
};

/* ---------- islands ---------- */
Tools.islands = {
  group: 'Emergency', label: 'Network islands', sub: 'union-find', icon: 'islands',
  async open() {
    Panel.innerHTML = head('Network islands', ['union-find', 'path compression', 'union by rank'], 'Connected components of the open network. Close a critical station and watch it fall apart.');
    Panel.append(html('<div id="out"></div>'));
    clearFx();
    const d = await api('islands'), pal = ['#38e1ff', '#2fd27a', '#ffc53d', '#b07cff', '#ff5a5f', '#4c8dff'];
    const roots = [...new Set(d.comp.filter(c => c >= 0))];
    roots.forEach((r, k) => d.comp.forEach((c, i) => { if (c === r) ringColor(i, pal[k % 6], pal[k % 6]); }));
    $('#out').innerHTML = `<div class="card"><div class="bigstat"><span class="n">${d.count}</span><span class="u">island${d.count === 1 ? '' : 's'}</span></div>
      <div class="substats">${d.count === 1 ? 'Every open station can reach every other.' : 'The network has split.'} · ${d.us.toFixed(1)} µs</div>
      ${roots.map((r, k) => `<div class="alt-path" style="margin-top:10px"><span class="dotc" style="--c:${pal[k % 6]}"></span>${d.comp.map((c, i) => c === r ? `<span class="stn">${NET.stations[i].code}</span>` : '').join(' ')}</div>`).join('')}</div>`;
    hud('Network islands', `${d.count} connected component(s)`);
  },
};

/* ---------- maintenance: Kruskal + union-find replay ---------- */
Tools.mst = {
  group: 'Emergency', label: 'Night maintenance', sub: "Kruskal's MST", icon: 'wrench',
  async open() {
    Panel.innerHTML = head('Night maintenance plan', ['Kruskal', 'merge sort', 'union-find'],
      'The cheapest set of tracks that keeps every station connected. Tracks are tried cheapest-first (merge sort); union-find rejects any track whose ends are already in the same set.');
    Panel.append(html('<div id="out"></div>'));
    clearFx();
    const tok = token, d = await api('mst');
    if (!alive(tok)) return;
    const keptN = d.inMst.filter(Boolean).length;
    const cutN = NET.edges.filter(e => !NET.stations[e.u].closed && !NET.stations[e.v].closed).length - keptN;
    const pctSaved = d.all ? Math.round(100 * (d.all - d.total) / d.all) : 0;
    if (!d.steps.length) {
      $('#out').innerHTML = `<div class="card"><div class="bigstat"><span class="n">${pctSaved}%</span><span class="u">of track-minutes powered down</span></div>
        <div class="substats">keep ${keptN} tracks (${d.total} min) · close ${cutN} · ${d.us.toFixed(1)} µs in C</div>
        <div class="p-desc" style="margin:10px 0 0">Step-by-step X-ray replay is enabled for maps up to 64 stations.</div></div>`;
      document.querySelectorAll('.layer-edges .edge').forEach(el => { el.style.opacity = 0.15; });
      d.inMst.forEach((ok, e) => {
        if (!ok) return;
        const a = P(NET.edges[e].u), b = P(NET.edges[e].v), c = colorOf(lineOf(e));
        svgEl('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y, stroke: c, 'stroke-width': 22, opacity: 0.3, filter: 'url(#glow)' }, L.route);
        svgEl('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y, stroke: c, 'stroke-width': 11, 'stroke-linecap': 'round' }, L.route);
      });
      hud("Kruskal's MST", `${keptN} tracks kept (${d.total} min)`);
      return;
    }
    $('#out').innerHTML = `<div class="card"><div class="bigstat"><span class="n">${pctSaved}%</span><span class="u">of track-minutes powered down</span></div>
      <div class="substats">keep ${keptN} tracks (${d.total} min) · close ${cutN} · ${d.us.toFixed(1)} µs in C</div></div>
      <div class="card xray"><h3><span class="xr">X-RAY</span> Kruskal step by step</h3><div id="sets" class="sets"></div><div id="kru" class="kru"></div></div>`;
    const pal = ['#38e1ff', '#2fd27a', '#ffc53d', '#b07cff', '#ff5a5f', '#4c8dff', '#ff9a5f', '#7df9a8'];
    const nm = e => `${NET.stations[NET.edges[e].u].code}–${NET.stations[NET.edges[e].v].code}`;
    $('#kru').innerHTML = d.steps.map(([e, ok], i) => `<div class="kr" data-i="${i}"><span class="pill" style="--c:${colorOf(lineOf(e))}">${nm(e)}</span>
      <span class="mono">${NET.edges[e].min}′</span><span class="res ${ok ? 'ok' : 'no'}">${ok ? 'union ✓' : 'cycle ✗'}</span></div>`).join('');
    document.querySelectorAll('.layer-edges .edge').forEach(el => { el.style.opacity = 0.15; });
    let last = -1;
    const find = (p, x) => { while (p[x] !== x) x = p[x]; return x; };
    const render = (t, dt) => {
      const k = Math.min(d.steps.length, Math.floor(t * 2));
      if (k === last) return;
      last = k;
      L.route.innerHTML = '';
      for (let i = 0; i < k; i++) {
        const [e, ok] = d.steps[i], a = P(NET.edges[e].u), b = P(NET.edges[e].v), cur = i === k - 1;
        if (ok) {
          svgEl('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y, stroke: colorOf(lineOf(e)), 'stroke-width': 22, opacity: 0.3, filter: 'url(#glow)' }, L.route);
          svgEl('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y, stroke: colorOf(lineOf(e)), 'stroke-width': 11, 'stroke-linecap': 'round' }, L.route);
        } else if (cur) {
          svgEl('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y, stroke: '#ff5a5f', 'stroke-width': 8, 'stroke-dasharray': '6 8', 'stroke-linecap': 'round' }, L.route);
          const mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
          svgEl('circle', { cx: mx, cy: my, r: 14, fill: '#ff5a5f' }, L.route);
          svgEl('path', { d: `M${mx - 5} ${my - 5}L${mx + 5} ${my + 5}M${mx + 5} ${my - 5}L${mx - 5} ${my + 5}`, stroke: '#fff', 'stroke-width': 3, 'stroke-linecap': 'round' }, L.route);
        }
        if (cur && dt > 0) burst((a.x + b.x) / 2, (a.y + b.y) / 2, ok ? '#2fd27a' : '#ff5a5f', 45);
      }
      // colour stations by their union-find set
      const par = k ? d.steps[k - 1][2] : NET.stations.map(s => s.id), roots = new Map();
      NET.stations.forEach(s => {
        const r = find(par, s.id), size = par.filter((_, j) => find(par, j) === r).length;
        if (size > 1 && !roots.has(r)) roots.set(r, pal[roots.size % pal.length]);
        ringColor(s.id, size > 1 ? roots.get(r) : '', size > 1 ? roots.get(r) : '');
      });
      $('#sets').innerHTML = `<b>${NET.stations.length - k + d.steps.slice(0, k).filter(s => !s[1]).length}</b> sets · ` +
        [...roots.entries()].map(([r, c]) => `<span class="dotc" style="--c:${c}"></span>`).join('') + ' <span class="mono">root = find(x)</span>';
      document.querySelectorAll('.kr').forEach(r => { const i = +r.dataset.i; r.classList.toggle('done', i < k); r.classList.toggle('now', i === k - 1); });
      const now = document.querySelector('.kr.now');
      if (now) now.scrollIntoView({ block: 'nearest' });
      hud("Kruskal's MST", k ? `track ${k}/${d.steps.length}: ${nm(d.steps[k - 1][0])} ${d.steps[k - 1][1] ? 'joins two sets' : 'would form a cycle - rejected'}` : 'sorted cheapest first');
    };
    await playTimeline(d.steps.length / 2 + 0.01, render, 1);
  },
};

/* ---------- benchmark ---------- */
Tools.bench = {
  group: 'Lab', label: 'Scale benchmark', sub: '900-station city', icon: 'gauge',
  open(opts = {}) {
    Panel.innerHTML = head('Scale benchmark', ['heap vs array', 'Dijkstra vs A*', 'real timings'],
      'The C engine generates a 30×30 grid city (900 stations, 8,100 search states) and times the same code paths the planner uses.');
    const go = html(`<button class="btn primary block">${icon('gauge')}Run benchmark in C</button>`);
    go.onclick = () => this.run();
    Panel.append(go, html('<div id="out"></div>'));
    clearFx();
    hud('Scale benchmark', 'same algorithms, 40x bigger city');
    if (opts.autorun) this.run();
  },
  async run() {
    $('#out').innerHTML = '<div class="card row" style="gap:12px"><div class="spinner" style="flex:none"></div><span style="color:var(--muted)">C engine is building the city and timing everything…</span></div>';
    const d = await api('bench');
    const bar = (label, v, max, unit, color) => `<div class="bench-row"><div class="race-top"><b>${label}</b><span>${v.toFixed(v < 10 ? 2 : 1)} ${unit}</span></div><div class="bar"><i data-w="${100 * v / max}" style="background:${color}"></i></div></div>`;
    $('#out').innerHTML = `
      <div class="card"><h3>Routing · 300 random trips</h3>
        ${bar('Dijkstra · avg states', d.expD, d.expD, '', '#4c8dff')}${bar('A* · avg states', d.expA, d.expD, '', '#ffc53d')}
        <div class="verdict">A* explores ${Math.round(100 - 100 * d.expA / d.expD)}% fewer states · ${d.mismatch ? 'MISMATCH' : 'identical answers'}</div></div>
      <div class="card"><h3>Why the heap matters · 100 full Dijkstras</h3>
        ${bar('Array scan  O(V²)', d.msArray, d.msArray, 'ms', '#ff5a5f')}${bar('Binary heap  O(E log V)', d.msHeap, d.msArray, 'ms', '#2fd27a')}
        <div class="verdict">Heap is ${(d.msArray / d.msHeap).toFixed(0)}× faster · ${d.sameDist ? 'identical distances' : 'DIFFERENT'}</div></div>
      <div class="card"><h3>Everything else · ${d.stations} stations</h3><div class="kv">
        <span>Yen top-5 routes</span><b>${d.msYen.toFixed(2)} ms</b>
        <span>Tarjan (${d.nArt} critical)</span><b>${(d.msTarjan * 1000).toFixed(1)} µs</b>
        <span>Kruskal MST</span><b>${(d.msKruskal * 1000).toFixed(1)} µs</b>
        <span>Union-find islands</span><b>${(d.msDsu * 1000).toFixed(1)} µs</b>
        <span>Hash lookup</span><b>${d.nsHash.toFixed(0)} ns</b>
        <span>Trie prefix search</span><b>${d.nsTrie.toFixed(0)} ns</b></div></div>`;
    await sleep(50);
    document.querySelectorAll('#out .bar i').forEach(i => { i.style.transition = 'width 1.1s cubic-bezier(.2,.8,.2,1)'; i.style.width = i.dataset.w + '%'; });
  },
};

/* ======================= navigation ======================= */
function buildNav() {
  const nav = $('#nav');
  let group = '';
  nav.innerHTML = '';
  for (const [id, t] of Object.entries(Tools)) {
    if (t.group !== group) { group = t.group; nav.append(html(`<div class="nav-group">${group}</div>`)); }
    const it = html(`<div class="nav-item${t.hot ? ' hot' : ''}" data-tool="${id}">${icon(t.icon)}<div class="nav-text"><span>${t.label}</span><small>${t.sub}</small></div></div>`);
    it.onclick = () => openTool(id);
    nav.append(it);
  }

}
function openTool(id, opts) {
  closePop();
  S.tool = id;
  document.querySelectorAll('.nav-item').forEach(n => n.classList.toggle('active', n.dataset.tool === id));
  clearFx();
  Panel.scrollTop = 0;
  return Tools[id].open(opts || {});
}

/* ======================= story mode ======================= */
const ACTS = [
  { title: 'Meet Nova City', algo: 'graph · hash table · trie', text: 'It is 08:30. A tourist lands at the Airport and wants to reach the Beach. Over the next hour the city throws everything at them.',
    run: async () => { await setNetwork(await api('reset')); S.from = 13; S.to = 17; openTool('home'); } },
  { title: 'Watch the algorithms think', algo: 'Dijkstra · A* · binary heap', text: 'Replayed from the C engine\'s own trace. Dijkstra ripples outward; A* aims at the Beach. On the right, the X-ray shows the real binary heap after every pop - stale entries and all.',
    run: async () => { await openTool('route'); } },
  { title: 'Ride along', algo: 'time-dependent state graph', text: 'The route is replayed in simulated time. Drag the timeline to scrub.',
    run: async () => { if (Tools.route.last) await rideRoute(Tools.route.last); } },
  { title: 'Rush hour hits the Green line', algo: 'lazy segment tree · O(log n) range update', text: '+7 min per hop on Green, 08:00-10:00: one range update. The X-ray shows the handful of tree nodes it touched and the lazy tags it left behind.',
    run: async () => { await setNetwork(await api(`incident?type=delay&line=G&from=480&to=600&amount=7`)); Tools.incidents.line = 2; await openTool('incidents'); } },
  { title: 'The planner adapts', algo: 'time-dependent edges · FIFO waiting', text: 'Every edge cost is a live point query on the segment tree. The planner leaves the delayed Green line where it can.',
    run: async () => { await openTool('route', { animate: false }); } },
  { title: 'Flood at Docklands', algo: "Yen's K-shortest paths", text: 'Docklands closes. Yen\'s algorithm lists the three best loopless detours; click any card to compare.',
    run: async () => { await setNetwork(await api(`incident?type=close&station=DOC`)); await openTool('alternatives'); } },
  { title: 'What could break the city?', algo: 'Tarjan · low-link · union-find', text: 'Tarjan\'s DFS replayed on the flooded network: each station gets disc | low. When a child cannot climb above its parent (low ≥ disc), the parent is critical.',
    run: async () => { await openTool('critical'); } },
  { title: 'Night maintenance', algo: "Kruskal · merge sort · union-find", text: 'Which tracks can close tonight? Kruskal tries them cheapest-first; union-find rejects every track that would close a cycle. Station colours are the merging sets.',
    run: async () => { await openTool('mst'); } },
  { title: 'FIRE', algo: 'Dijkstra fire spread · deadline-constrained Dijkstra', text: 'All clear on the trains (we pop the incident stack), but a fire breaks out at Fort while our tourist is on the Beach. Plan A takes the nearest exit. Plan B refuses to enter any station after the fire.',
    run: async () => { await setNetwork(await api('reset')); S.fire = 20; S.me = 17; S.pct = 140; await openTool('fire'); } },
  { title: 'Evacuate everyone', algo: 'multi-source Dijkstra', text: 'The same spreading algorithm, seeded at all five exits at once, gives every station its escape route.',
    run: async () => { await openTool('evacuate'); } },
  { title: 'Does it scale?', algo: 'heap vs array · A* vs Dijkstra', text: 'The C engine builds a 900-station city and times the same code. The binary heap beats the O(V²) array scan by an order of magnitude.',
    run: async () => { await openTool('bench', { autorun: true }); } },
];
let act = -1;
function showCaption() {
  const a = ACTS[act], c = $('#caption');
  c.innerHTML = `<div class="cap-top"><span class="cap-act">ACT ${act + 1}/${ACTS.length}</span><div class="cap-title">${a.title}</div>
    <div class="cap-btns"><button class="btn small" id="capPrev" ${act ? '' : 'disabled'}>◀</button><button class="btn small primary" id="capNext">${act === ACTS.length - 1 ? 'Finish' : 'Next ▶'}</button><button class="btn small" id="capX">✕</button></div></div>
    <div class="cap-text">${a.text}</div><div class="cap-algo">${a.algo}</div><div class="cap-engine" id="capEngine"></div>
    <div class="cap-dots">${ACTS.map((_, i) => `<i class="${i <= act ? 'on' : ''}"></i>`).join('')}</div>`;
  c.classList.remove('hidden');
  $('#stage').classList.add('story');
  $('#capNext').onclick = () => goAct(act + 1);
  $('#capPrev').onclick = () => goAct(act - 1);
  $('#capX').onclick = endStory;
}
async function goAct(i) {
  if (i >= ACTS.length) return endStory();
  if (i < 0) return;
  act = i;
  showCaption();
  await ACTS[i].run();
}
function endStory() { act = -1; $('#caption').classList.add('hidden'); $('#stage').classList.remove('story'); }
$('#storyBtn').onclick = () => goAct(0);
document.addEventListener('keydown', e => {
  if (e.key === 'Escape' && !$('#pop').classList.contains('hidden')) { closePop(); return; }
  if (act < 0 || /input|select/i.test(document.activeElement.tagName)) return;
  if (e.key === 'ArrowRight' || e.key === ' ') { e.preventDefault(); goAct(act + 1); }
  if (e.key === 'ArrowLeft') goAct(act - 1);
  if (e.key === 'Escape') endStory();
});

/* ======================= clock & reset ======================= */
$('#resetBtn').onclick = async () => {
  await setNetwork(await api('reset'));
  openTool(S.tool);
};
$('#clockInput').onchange = async e => {
  const [h, m] = e.target.value.split(':').map(Number);
  if (isNaN(h)) return;
  await setNetwork(await api(`settings?clock=${h * 60 + m}`));
  openTool(S.tool);
};

/* ======================= boot ======================= */
(async () => {
  buildNav();
  await setNetwork();
  const code = c => NET.stations.find(s => s.code === c);
  // story mode refers to stations by id; resolve them from codes so edited maps still work
  const air = code('AIR'), bch = code('BCH'), frt = code('FRT');
  if (air && bch) { ACTS[0].run = async () => { await setNetwork(await api('reset')); S.from = air.id; S.to = bch.id; openTool('home'); }; }
  const fireAct = ACTS.find(a => a.title === 'FIRE');
  if (frt && bch) { fireAct.run = async () => { await setNetwork(await api('reset')); S.fire = frt.id; S.me = bch.id; S.pct = 140; await openTool('fire'); }; }
  // deep links: #route, #fire, #evacuate, #story ... (handy for demos)
  const h = location.hash.slice(1);
  if (h === 'story') goAct(0);
  else if (Tools[h]) { if (air && bch && S.from == null) { S.from = air.id; S.to = bch.id; } openTool(h, { autorun: true }); }
  else openTool('home');
})();
