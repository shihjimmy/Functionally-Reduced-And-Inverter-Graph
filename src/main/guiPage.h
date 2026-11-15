/****************************************************************************
  FileName     [ guiPage.h ]
  PackageName  [ main ]
  Synopsis     [ The single web page of the GUI (HTML + CSS + JavaScript) ]
****************************************************************************/

// The page is embedded in the executable so "fraig -Gui" works from any
// directory. It talks to guiServer.cpp only through /api/state, /api/files,
// /api/cmd (starts a command as a background job) and /api/job (its
// progress and result). Sections of the script:
//   1. server API         4. drawing (canvas)
//   2. state and diff     5. mouse: pan, zoom, hover, select
//   3. layout             6. panels, buttons, log

#ifndef GUI_PAGE_H
#define GUI_PAGE_H

static const char kGuiPage[] = R"FRAIGPAGE(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>fraig — AIG viewer</title>
<style>
  :root {
    --bg: #f6f7f9; --panel: #ffffff; --ink: #1d2433; --muted: #6b7385;
    --line: #dde1e8; --accent: #2f6fdf; --accent-ink: #ffffff;
    --pi: #2e9e6b; --po: #2f6fdf; --aig: #4a5468; --const: #1d2433;
    --undef: #d0453a; --removed: #d0453a; --changed: #e8912d;
    --edge: #9aa3b5; --edge-sel: #2f6fdf; --canvas: #fbfcfd;
  }
  @media (prefers-color-scheme: dark) {
    :root {
      --bg: #14171d; --panel: #1c2028; --ink: #e5e8ee; --muted: #9098a8;
      --line: #2c323d; --accent: #5b8ff0; --accent-ink: #0d1117;
      --pi: #4cc38a; --po: #6f9ff5; --aig: #b4bccb; --const: #e5e8ee;
      --undef: #f0695e; --removed: #f0695e; --changed: #f2a54a;
      --edge: #4d5566; --edge-sel: #6f9ff5; --canvas: #171a21;
    }
  }
  * { box-sizing: border-box; }
  body { margin: 0; font: 14px/1.45 system-ui, -apple-system, "Segoe UI", sans-serif;
         background: var(--bg); color: var(--ink); height: 100vh; display: flex;
         flex-direction: column; }
  header { display: flex; flex-wrap: wrap; gap: 8px; align-items: center;
           padding: 10px 14px; background: var(--panel); border-bottom: 1px solid var(--line); }
  header h1 { font-size: 15px; margin: 0 10px 0 0; letter-spacing: .2px; }
  input[type=text] { font: inherit; padding: 6px 8px; border: 1px solid var(--line);
         border-radius: 6px; background: var(--bg); color: var(--ink); }
  #file { width: 260px; }
  button { font: inherit; padding: 6px 12px; border-radius: 6px; cursor: pointer;
           border: 1px solid var(--line); background: var(--panel); color: var(--ink); }
  button.op { background: var(--accent); color: var(--accent-ink); border-color: var(--accent);
              font-weight: 600; }
  button:disabled { opacity: .38; cursor: not-allowed; }
  button.op.check { background: var(--pi); border-color: var(--pi); }
  #job { display: flex; align-items: center; justify-content: flex-end; gap: 8px;
         flex: 1 1 0; min-width: 0; font-size: 12px; color: var(--muted); }
  #job[hidden] { display: none; }
  #jobbar { flex: 0 1 140px; min-width: 40px; height: 8px; border-radius: 4px; background: var(--line); overflow: hidden;
            flex: none; }
  #jobbar i { display: block; height: 100%; width: 0; background: var(--accent); transition: width .2s; }
  #jobbar.busy i { width: 30%; animation: slide 1.2s ease-in-out infinite; }
  @keyframes slide { from { transform: translateX(-100%); } to { transform: translateX(340%); } }
  #jobtext { flex: 0 1 auto; min-width: 0; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; font-variant-numeric: tabular-nums; }
  #log .ok { color: var(--pi); font-weight: 600; }
  .sep { width: 1px; align-self: stretch; background: var(--line); margin: 0 4px; }
  main { flex: 1; display: flex; min-height: 0; }
  #view { flex: 1; position: relative; min-width: 0; }
  canvas { position: absolute; inset: 0; width: 100%; height: 100%; background: var(--canvas);
           cursor: grab; }
  canvas.dragging { cursor: grabbing; }
  #hint { position: absolute; left: 12px; bottom: 10px; color: var(--muted); font-size: 12px;
          pointer-events: none; }
  #tip { position: absolute; pointer-events: none; background: var(--panel); border: 1px solid var(--line);
         border-radius: 6px; padding: 4px 8px; font-size: 12px; display: none; white-space: nowrap; }
  #empty { position: absolute; inset: 0; display: flex; align-items: center; justify-content: center;
           color: var(--muted); }
  aside { width: 380px; display: flex; flex-direction: column; border-left: 1px solid var(--line);
          background: var(--panel); min-height: 0; }
  section { padding: 12px 14px; border-bottom: 1px solid var(--line); }
  section h2 { font-size: 12px; text-transform: uppercase; letter-spacing: .6px; color: var(--muted);
               margin: 0 0 8px; font-weight: 600; }
  .flow { display: flex; gap: 4px; flex-wrap: wrap; }
  .flow span { padding: 2px 8px; border-radius: 10px; border: 1px solid var(--line); font-size: 12px;
               color: var(--muted); }
  .flow span.on { background: var(--accent); color: var(--accent-ink); border-color: var(--accent); }
  .stats { display: grid; grid-template-columns: repeat(4, 1fr); gap: 6px; }
  .stat { border: 1px solid var(--line); border-radius: 8px; padding: 6px 8px; }
  .stat b { display: block; font-size: 18px; font-variant-numeric: tabular-nums; }
  .stat small { color: var(--muted); }
  .delta { font-size: 12px; margin-left: 4px; }
  .delta.neg { color: var(--pi); } .delta.pos { color: var(--removed); }
  .legend { display: flex; flex-wrap: wrap; gap: 6px 14px; font-size: 12px; color: var(--muted); }
  .legend i { display: inline-block; width: 10px; height: 10px; border-radius: 50%; margin-right: 5px;
              vertical-align: -1px; }
  pre { margin: 0; font: 12px/1.45 ui-monospace, "Cascadia Mono", Consolas, monospace;
        white-space: pre-wrap; word-break: break-word; }
  #gate pre { max-height: 160px; overflow: auto; white-space: pre; }
  #logbox { flex: 1; display: flex; flex-direction: column; min-height: 0; }
  #log { flex: 1; overflow: auto; }
  #log .cmd { color: var(--accent); font-weight: 600; margin-top: 6px; }
  #log .err { color: var(--removed); }
  #log .note { color: var(--muted); }
  #cmdline { width: 100%; margin-top: 8px; font-family: ui-monospace, Consolas, monospace; }
</style>
</head>
<body>
<header>
  <h1>fraig</h1>
  <input id="file" type="text" list="files" placeholder="path/to/circuit.aag">
  <datalist id="files"></datalist>
  <button id="read">Read</button>
  <div class="sep"></div>
  <button class="op" data-cmd="CIRSWeep">Sweep</button>
  <button class="op" data-cmd="CIROPTimize">Optimize</button>
  <button class="op" data-cmd="CIRSTRash">Strash</button>
  <button class="op" data-cmd="CIRSIMulate -Random">Simulate</button>
  <button class="op" data-cmd="CIRFraig">Fraig</button>
  <div class="sep"></div>
  <button class="op check" data-cmd="CIRVerify">Verify</button>
  <div class="sep"></div>
  <button id="fit" title="Fit the whole circuit in view">Fit</button>
  <div id="job" hidden><div id="jobbar"><i></i></div><span id="jobtext"></span></div>
</header>
<main>
  <div id="view">
    <canvas id="canvas"></canvas>
    <div id="empty">Read a circuit to start (e.g. tests.fraig/opt07.aag).</div>
    <div id="tip"></div>
    <div id="hint">Drag to pan · wheel to zoom · click a gate for details</div>
  </div>
  <aside>
    <section>
      <h2>Flow</h2>
      <div class="flow" id="flow"></div>
    </section>
    <section>
      <h2>Circuit</h2>
      <div class="stats" id="stats"></div>
    </section>
    <section>
      <h2>Legend</h2>
      <div class="legend">
        <span><i style="background:var(--pi)"></i>PI</span>
        <span><i style="background:var(--po)"></i>PO</span>
        <span><i style="background:var(--aig)"></i>AND</span>
        <span><i style="background:var(--const)"></i>CONST 0</span>
        <span><i style="border:2px solid var(--undef)"></i>UNDEF</span>
        <span>- - - inverted edge</span>
        <span><i style="background:var(--aig);opacity:.3"></i>unreachable from POs</span>
        <span><i style="background:var(--removed)"></i>removed</span>
        <span><i style="background:var(--changed)"></i>rewired</span>
        <span>colors after Simulate = FEC groups</span>
      </div>
    </section>
    <section id="gate">
      <h2>Selected gate</h2>
      <pre id="gateinfo">Click a gate in the drawing.</pre>
    </section>
    <section id="logbox">
      <h2>Log</h2>
      <div id="log"></div>
      <input id="cmdline" type="text" placeholder="Any fraig command, e.g. cirp -fec  (Enter)">
    </section>
  </aside>
</main>
<script>
"use strict";

/* ---------- 1. server API ------------------------------------------- */
async function api(path, body) {
  const res = await fetch(path, body === undefined ? {} : { method: "POST", body });
  return res.json();
}
const sleep = ms => new Promise(r => setTimeout(r, ms));

// Every command runs as a background job on the server: POST /api/cmd starts
// it, GET /api/job reports progress and, when done, {status, output, state}.
// Returns that result, or {busy: true} / {error} if it could not start.
async function exec(cmd) {
  const res = await fetch("/api/cmd", { method: "POST", body: cmd });
  if (res.status === 409) return { busy: true };
  if (res.status === 400) return { error: (await res.json()).error };
  const start = Date.now();
  try {
    for (let delay = 20; ; delay = Math.min(delay * 2, 200)) {
      await sleep(delay);
      const j = await api("/api/job");
      if (j.done) return j;
      if (Date.now() - start > 300) showProgress(cmd, j.progress, Date.now() - start);
    }
  } finally { hideProgress(); }
}

// A "done/total" in the progress text fills the bar; otherwise it just moves.
function showProgress(cmd, text, ms) {
  const box = document.getElementById("job"), bar = document.getElementById("jobbar");
  box.hidden = false;
  box.title = `${cmd}: ${text || "running"}`;
  const m = /(\d+)\s*\/\s*(\d+)/.exec(text || "");
  bar.classList.toggle("busy", !m);
  bar.firstChild.style.width = m ? (100 * Number(m[1]) / Math.max(1, Number(m[2]))).toFixed(1) + "%" : "";
  document.getElementById("jobtext").textContent =
    `${text || cmd + " running"} · ${(ms / 1000).toFixed(0)} s`;
}
function hideProgress() { document.getElementById("job").hidden = true; }

/* ---------- 2. state and diff ---------------------------------------- */
// state = { stage, circuit } from CIRJson. "gates" maps id -> gate object.
let state = { stage: "INIT", circuit: null };
let gates = new Map();
let currentFile = "";
let selected = null;                 // selected gate id
let changes = { removed: new Map(), rewired: new Set(), time: 0 };

function indexGates(circuit) {
  const m = new Map();
  if (circuit) for (const g of circuit.gates) m.set(g.id, g);
  return m;
}

// Compare the old and the new netlist: which gates disappeared, and which
// surviving gates now have different fanins.
function diff(oldGates, newGates) {
  const removed = new Map(), rewired = new Set();
  for (const [id, g] of oldGates) {
    const n = newGates.get(id);
    if (!n) removed.set(id, g);
    else if (JSON.stringify(n.in) !== JSON.stringify(g.in)) rewired.add(id);
  }
  return { removed, rewired, time: performance.now() };
}

function applyState(newState, animate) {
  const oldGates = gates, oldPos = pos;
  state = newState;
  gates = indexGates(state.circuit);
  layout();
  if (animate && oldGates.size) {
    changes = diff(oldGates, gates);
    changes.oldPos = oldPos;           // where removed gates used to be
    startAnimation(gates.size > 20000 ? pos : oldPos);   // huge: no gliding
  } else {
    changes = { removed: new Map(), rewired: new Set(), time: 0 };
    fitView();
  }
  if (selected !== null && !gates.has(selected)) selected = null;
  updatePanels();
  draw();
}

/* ---------- 3. layout ------------------------------------------------- */
// Layered drawing: column = logic depth (PIs / CONST / UNDEF at 0, an AND
// gate one more than its deepest fanin, all POs in the last column).
// Inside a column gates are sorted by the average row of their fanins
// (barycenter), which keeps most edges short. O(V + E) plus sorting.
const DX = 120, DY = 42;
let pos = new Map();                 // id -> {x, y}

function layout() {
  pos = new Map();
  if (!state.circuit) return;
  const level = new Map();
  // Iterative DFS (circuits can be deeper than the JS call stack).
  for (const start of gates.keys()) {
    if (level.has(start)) continue;
    const stack = [start];
    while (stack.length) {
      const id = stack[stack.length - 1];
      if (level.has(id)) { stack.pop(); continue; }
      const g = gates.get(id);
      const pending = g.in.map(e => e[0]).filter(f => !level.has(f) && gates.has(f));
      if (pending.length) { stack.push(...pending); continue; }
      stack.pop();
      let l = 0;
      if (g.type === "AIG") for (const [f] of g.in) l = Math.max(l, (level.get(f) || 0) + 1);
      level.set(id, l);
    }
  }
  let maxLevel = 0;
  for (const [id, l] of level) if (gates.get(id).type !== "PO") maxLevel = Math.max(maxLevel, l);
  const columns = [];
  for (const [id, l] of level) {
    const col = gates.get(id).type === "PO" ? maxLevel + 1 : l;
    (columns[col] = columns[col] || []).push(id);
  }
  const row = new Map();
  const piOrder = new Map(state.circuit.pis.map((id, i) => [id, i]));
  columns.forEach((col, c) => {
    if (!col) return;
    if (c === 0) {
      col.sort((a, b) => (piOrder.has(a) ? piOrder.get(a) : 1e9 + a) -
                         (piOrder.has(b) ? piOrder.get(b) : 1e9 + b));
    } else {
      const key = new Map(col.map(id => {
        const ins = gates.get(id).in.filter(e => row.has(e[0]));
        const avg = ins.length ? ins.reduce((s, e) => s + row.get(e[0]), 0) / ins.length : 0;
        return [id, avg];
      }));
      col.sort((a, b) => key.get(a) - key.get(b) || a - b);
    }
    col.forEach((id, i) => {
      row.set(id, i - (col.length - 1) / 2);
      pos.set(id, { x: c * DX, y: (i - (col.length - 1) / 2) * DY });
    });
  });
}

/* ---------- 4. drawing ------------------------------------------------ */
const canvas = document.getElementById("canvas");
const ctx = canvas.getContext("2d");
let view = { x: 0, y: 0, scale: 1 };   // screen = world * scale + (x, y)
let anim = null;                       // { from: Map, start: time }
const ANIM_MS = 700, HIGHLIGHT_MS = 2500;

function css(name) { return getComputedStyle(document.documentElement).getPropertyValue(name).trim(); }

// Gates glide from their old to their new positions while the view glides
// to fit the new circuit.
function startAnimation(oldPos) {
  anim = { from: oldPos, start: performance.now(), fromView: { ...view }, toView: fitTarget() };
  requestAnimationFrame(tick);
}
function tick() {
  if (anim && anim.toView) {
    const t = ease(Math.min(1, (performance.now() - anim.start) / ANIM_MS));
    for (const k of ["x", "y", "scale"])
      view[k] = anim.fromView[k] + (anim.toView[k] - anim.fromView[k]) * t;
  }
  draw();
  const t = performance.now();
  if ((anim && t - anim.start < ANIM_MS) || t - changes.time < HIGHLIGHT_MS)
    requestAnimationFrame(tick);
  else anim = null;
}
function ease(t) { return t < .5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2; }

// Position of a gate at the current animation time.
function at(id) {
  const p = pos.get(id);
  if (!anim) return p;
  const q = anim.from.get(id);
  if (!q || !p) return p || q;
  const t = ease(Math.min(1, (performance.now() - anim.start) / ANIM_MS));
  return { x: q.x + (p.x - q.x) * t, y: q.y + (p.y - q.y) * t };
}

// Golden-angle hues; start at violet so group 0 is not "removed" red.
function fecColor(k) { return `hsl(${(270 + k * 137.508) % 360}, 62%, 52%)`; }

function nodeColor(g, C) {
  if (g.type === "PI") return C.pi;
  if (g.type === "PO") return C.po;
  if (g.type === "CONST") return C.const;
  if (g.type === "UNDEF") return "transparent";
  return g.fec >= 0 ? fecColor(g.fec) : C.aig;
}

function resize() {
  const r = canvas.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
  canvas.width = Math.round(r.width * dpr);
  canvas.height = Math.round(r.height * dpr);
  draw();
}

function drawEdge(a, b, inv, color, width, simple) {
  const k = 1 / Math.max(view.scale, 1);   // no thicker than this when zoomed in
  ctx.strokeStyle = color;
  ctx.lineWidth = width * k;
  ctx.setLineDash(inv ? [5 * k, 4 * k] : []);
  ctx.beginPath();
  ctx.moveTo(a.x, a.y);
  if (simple) ctx.lineTo(b.x, b.y);
  else {
    const mx = (a.x + b.x) / 2;
    ctx.bezierCurveTo(mx, a.y, mx, b.y, b.x - 9, b.y);
  }
  ctx.stroke();
  if (inv && !simple) {                 // inversion bubble at the input
    ctx.setLineDash([]);
    ctx.beginPath();
    ctx.arc(b.x - 11, b.y, 2.6, 0, 2 * Math.PI);
    ctx.stroke();
  }
}

function drawNode(g, p, C, alpha, detail) {
  const dead = g.dfs < 0 && g.type !== "PI" && g.type !== "CONST";
  ctx.globalAlpha = alpha * (dead ? 0.3 : 1);
  ctx.setLineDash([]);
  ctx.fillStyle = nodeColor(g, C);
  ctx.strokeStyle = g.type === "UNDEF" ? C.undef : C.canvas;
  ctx.lineWidth = g.type === "UNDEF" ? 2 : 1.5;
  ctx.beginPath();
  if (g.type === "AIG") {               // AND symbol: flat back, round front
    ctx.moveTo(p.x - 9, p.y - 8); ctx.lineTo(p.x, p.y - 8);
    ctx.arc(p.x, p.y, 8, -Math.PI / 2, Math.PI / 2);
    ctx.lineTo(p.x - 9, p.y + 8); ctx.closePath();
  } else if (g.type === "PI" || g.type === "PO") {
    ctx.roundRect ? ctx.roundRect(p.x - 10, p.y - 8, 20, 16, 4) : ctx.rect(p.x - 10, p.y - 8, 20, 16);
  } else ctx.rect(p.x - 8, p.y - 8, 16, 16);
  ctx.fill(); ctx.stroke();
  if (detail) {
    ctx.fillStyle = C.ink;
    ctx.font = "11px system-ui, sans-serif";
    ctx.textAlign = "center";
    const label = g.name ? `${g.id} ${g.name}` : String(g.id);
    ctx.fillText(label, p.x, p.y - 12);
  }
  ctx.globalAlpha = 1;
}

// Big circuits use a batched "fast" mode: one path for all normal edges,
// one for all inverted edges, nodes as squares grouped by color. Canvas
// cost is per draw call, so this is 100x faster than one call per item.
// Everything outside the visible area is skipped in both modes.
const FAST_LIMIT = 3000;

function visibleRect() {
  const r = canvas.getBoundingClientRect(), m = 40 / view.scale;
  return { x0: -view.x / view.scale - m, y0: -view.y / view.scale - m,
           x1: (r.width - view.x) / view.scale + m, y1: (r.height - view.y) / view.scale + m };
}

function draw() {
  const dpr = window.devicePixelRatio || 1;
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  document.getElementById("empty").style.display = state.circuit ? "none" : "flex";
  if (!state.circuit) return;
  const C = { pi: css("--pi"), po: css("--po"), aig: css("--aig"), const: css("--const"),
              undef: css("--undef"), edge: css("--edge"), sel: css("--edge-sel"),
              removed: css("--removed"), changed: css("--changed"), ink: css("--ink"),
              canvas: css("--canvas") };
  ctx.setTransform(view.scale * dpr, 0, 0, view.scale * dpr, view.x * dpr, view.y * dpr);

  const fast = gates.size > FAST_LIMIT;
  const simple = fast || view.scale < 0.45;
  const detail = view.scale > 0.7 && (!fast || view.scale > 1.2);
  const vr = visibleRect();
  const inView = p => p.x >= vr.x0 && p.x <= vr.x1 && p.y >= vr.y0 && p.y <= vr.y1;
  const fade = Math.max(0, 1 - (performance.now() - changes.time) / HIGHLIGHT_MS);

  // edges
  const k = 1 / Math.max(view.scale, 1);
  const normal = new Path2D(), inverted = new Path2D(), hot = [];
  for (const g of gates.values()) {
    const b = at(g.id); if (!b) continue;
    for (const [f, inv] of g.in) {
      const a = at(f); if (!a) continue;
      if (!inView(a) && !inView(b) &&
          (Math.max(a.x, b.x) < vr.x0 || Math.min(a.x, b.x) > vr.x1 ||
           Math.max(a.y, b.y) < vr.y0 || Math.min(a.y, b.y) > vr.y1)) continue;
      if (selected !== null && (selected === g.id || selected === f)) { hot.push([a, b, inv]); continue; }
      if (fast) { const path = inv ? inverted : normal; path.moveTo(a.x, a.y); path.lineTo(b.x, b.y); }
      else drawEdge(a, b, inv, C.edge, 1.1, simple);
    }
  }
  if (fast) {
    ctx.lineWidth = 0.7 * k; ctx.strokeStyle = C.edge;
    ctx.setLineDash([]); ctx.stroke(normal);
    ctx.setLineDash([5 * k, 4 * k]); ctx.stroke(inverted);
    ctx.setLineDash([]);
  }
  for (const [a, b, inv] of hot) drawEdge(a, b, inv, C.sel, 2, false);

  // removed gates fade out where they were
  if (fade > 0 && changes.oldPos) {
    ctx.globalAlpha = fade; ctx.fillStyle = C.removed;
    const path = new Path2D();
    for (const id of changes.removed.keys()) {
      const p = changes.oldPos.get(id);
      if (p && inView(p)) { path.moveTo(p.x + 9, p.y); path.arc(p.x, p.y, 9, 0, 2 * Math.PI); }
    }
    ctx.fill(path); ctx.globalAlpha = 1;
    ctx.strokeStyle = C.changed; ctx.lineWidth = 4 * k;
    ctx.globalAlpha = fade;
    const ring = new Path2D();
    for (const id of changes.rewired) {
      const p = at(id);
      if (p && inView(p)) { ring.moveTo(p.x + 15, p.y); ring.arc(p.x, p.y, 15, 0, 2 * Math.PI); }
    }
    ctx.stroke(ring); ctx.globalAlpha = 1;
  }

  // gates
  if (fast && !detail) {
    const byColor = new Map();
    for (const g of gates.values()) {
      const p = at(g.id); if (!p || !inView(p)) continue;
      const dead = g.dfs < 0 && g.type !== "PI" && g.type !== "CONST";
      const color = (dead ? "~" : "") + (g.type === "UNDEF" ? C.undef : nodeColor(g, C));
      if (!byColor.has(color)) byColor.set(color, new Path2D());
      byColor.get(color).rect(p.x - 7, p.y - 7, 14, 14);
    }
    for (const [color, path] of byColor) {
      ctx.globalAlpha = color.startsWith("~") ? 0.3 : 1;
      ctx.fillStyle = color.replace("~", "");
      ctx.fill(path);
    }
    ctx.globalAlpha = 1;
  } else {
    for (const g of gates.values()) {
      const p = at(g.id); if (!p || !inView(p)) continue;
      drawNode(g, p, C, 1, detail);
    }
  }
  if (selected !== null && pos.has(selected)) {
    const p = at(selected);
    ctx.strokeStyle = C.sel; ctx.lineWidth = 2.5 * k; ctx.setLineDash([]);
    ctx.beginPath(); ctx.arc(p.x, p.y, 14, 0, 2 * Math.PI); ctx.stroke();
  }
}

function fitView() {
  const v = fitTarget();
  if (v) { view = v; draw(); }
}
// The view that shows the whole circuit (null if nothing is drawn).
function fitTarget() {
  if (!pos.size) return null;
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (const p of pos.values()) {
    x0 = Math.min(x0, p.x); x1 = Math.max(x1, p.x); y0 = Math.min(y0, p.y); y1 = Math.max(y1, p.y);
  }
  const r = canvas.getBoundingClientRect(), pad = 60;
  const s = Math.min((r.width - 2 * pad) / Math.max(1, x1 - x0), (r.height - 2 * pad) / Math.max(1, y1 - y0));
  const scale = Math.max(0.02, Math.min(2.2, s));
  return { scale, x: r.width / 2 - (x0 + x1) / 2 * scale, y: r.height / 2 - (y0 + y1) / 2 * scale };
}

/* ---------- 5. mouse: pan, zoom, hover, select ------------------------ */
let drag = null;
function toWorld(e) {
  const r = canvas.getBoundingClientRect();
  return { x: (e.clientX - r.left - view.x) / view.scale, y: (e.clientY - r.top - view.y) / view.scale };
}
function gateAt(e) {
  const w = toWorld(e), r2 = Math.pow(Math.max(12, 6 / view.scale), 2);
  let best = null, bestD = r2;
  for (const [id, p] of pos) {
    const d = (p.x - w.x) ** 2 + (p.y - w.y) ** 2;
    if (d < bestD) { bestD = d; best = id; }
  }
  return best;
}
canvas.addEventListener("mousedown", e => {
  if (anim) anim.toView = null;        // the user takes over the view
  drag = { x: e.clientX, y: e.clientY, vx: view.x, vy: view.y, moved: false };
});
window.addEventListener("mousemove", e => {
  if (drag) {
    const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
    if (Math.abs(dx) + Math.abs(dy) > 3) { drag.moved = true; canvas.classList.add("dragging"); }
    view.x = drag.vx + dx; view.y = drag.vy + dy; draw(); return;
  }
  const tip = document.getElementById("tip");
  if (e.target !== canvas) { tip.style.display = "none"; return; }
  const id = gateAt(e);
  if (id === null) { tip.style.display = "none"; return; }
  const g = gates.get(id), r = canvas.getBoundingClientRect();
  tip.textContent = `${g.type} ${g.id}${g.name ? " (" + g.name + ")" : ""}` +
                    (g.dfs < 0 && g.type === "AIG" ? " · unreachable" : "") +
                    (g.fec >= 0 ? " · FEC group " + g.fec : "");
  tip.style.left = (e.clientX - r.left + 14) + "px";
  tip.style.top = (e.clientY - r.top + 10) + "px";
  tip.style.display = "block";
});
window.addEventListener("mouseup", e => {
  if (drag && !drag.moved && e.target === canvas) selectGate(gateAt(e));
  drag = null; canvas.classList.remove("dragging");
});
canvas.addEventListener("wheel", e => {
  e.preventDefault();
  const r = canvas.getBoundingClientRect(), mx = e.clientX - r.left, my = e.clientY - r.top;
  const f = Math.exp(-e.deltaY * 0.0015);
  const s = Math.max(0.01, Math.min(6, view.scale * f));
  view.x = mx - (mx - view.x) * (s / view.scale);
  view.y = my - (my - view.y) * (s / view.scale);
  view.scale = s; draw();
}, { passive: false });

async function selectGate(id) {
  selected = id; draw();
  const info = document.getElementById("gateinfo");
  if (id === null) { info.textContent = "Click a gate in the drawing."; return; }
  if (busy) { info.textContent = "Busy: wait for the running command."; return; }
  const r = await exec(`CIRGate ${id} -FANIn 1`);
  const r2 = await exec(`CIRGate ${id}`);
  if (!r.output || !r2.output) return;
  info.textContent = r2.output.trim() + "\n\nFanin:\n" + r.output.trim();
}

/* ---------- 6. panels, buttons, log ----------------------------------- */
const STAGES = ["READ", "OPT", "STRASH", "SIMULATE", "FRAIG"];
const STAGE_LABEL = { READ: "Read", OPT: "Optimize", STRASH: "Strash", SIMULATE: "Simulate", FRAIG: "Fraig" };
let lastStats = null;

// The flow rules of the project spec (same checks as cirCmd.cpp).
function allowed(cmd) {
  const s = state.stage;
  if (!state.circuit) return false;
  if (busy) return false;
  if (cmd.startsWith("CIRV")) return true;
  if (cmd.startsWith("CIROPT")) return s !== "SIMULATE";
  if (cmd.startsWith("CIRSTR")) return s !== "SIMULATE" && s !== "STRASH";
  if (cmd.startsWith("CIRF")) return s === "SIMULATE";
  return true;
}
const WHY = { CIRVerify: "Prove the circuit still equivalent to the file that was read",
              CIROPTimize: "Not after Simulate: run Fraig first",
              CIRSTRash: "Not after Simulate, and only once until the circuit changes",
              CIRFraig: "Needs Simulate first" };

function updatePanels() {
  document.getElementById("flow").innerHTML = STAGES.map(s =>
    `<span class="${s === state.stage ? "on" : ""}">${STAGE_LABEL[s]}</span>`).join("");
  for (const b of document.querySelectorAll("button.op")) {
    b.disabled = !allowed(b.dataset.cmd);
    b.title = b.disabled ? (WHY[b.dataset.cmd] || "Read a circuit first")
                         : (b.dataset.cmd === "CIRVerify" ? WHY.CIRVerify : b.dataset.cmd);
  }
  const c = state.circuit, el = document.getElementById("stats");
  if (!c) { el.innerHTML = ""; lastStats = null; return; }
  const reach = c.gates.filter(g => g.type === "AIG" && g.dfs >= 0).length;
  const now = { PI: c.stats.pi, PO: c.stats.po, AIG: c.stats.aig, "In DFS": reach, "FEC grp": c.fec.length };
  el.innerHTML = Object.entries(now).map(([k, v]) => {
    const d = lastStats ? v - lastStats[k] : 0;
    const delta = d ? `<span class="delta ${d < 0 ? "neg" : "pos"}">${d > 0 ? "+" : ""}${d}</span>` : "";
    return `<div class="stat"><small>${k}</small><b>${v}${delta}</b></div>`;
  }).join("");
  lastStats = now;
}

function log(text, cls) {
  const box = document.getElementById("log");
  const div = document.createElement("pre");
  if (cls) div.className = cls;
  div.textContent = text;
  box.appendChild(div);
  box.scrollTop = box.scrollHeight;
}

// Keep only what a terminal would show of "\r"-overwritten progress lines.
function visible(output) {
  return output.split("\n").map(l => l.slice(l.lastIndexOf("\r") + 1)).join("\n").trim();
}

let busy = false;
async function run(cmd, animate = true) {
  if (busy) return;
  busy = true;
  updatePanels();                       // disables the buttons while it runs
  document.body.style.cursor = "progress";
  log("fraig> " + cmd, "cmd");
  try {
    const r = await exec(cmd);
    if (r.busy || r.error) {
      log(r.error || "Busy: wait for the running command to finish.", "err");
      return;
    }
    const out = visible(r.output);
    if (out) log(out, /Error|Illegal|Cannot|Missing|NOT EQUIVALENT/.test(out) ? "err"
                     : /Result: EQUIVALENT/.test(out) ? "ok" : "");
    if (animate) {
      applyState(r.state, true);
      if (changes.removed.size || changes.rewired.size)
        log(`${changes.removed.size} gate(s) removed, ${changes.rewired.size} rewired` +
            (changes.removed.size && changes.removed.size <= 30
               ? ": " + [...changes.removed.keys()].join(" ") : ""), "note");
      else if (/^CIR(SW|OPT|STR|F)/i.test(cmd)) log("No change to the netlist.", "note");
    } else applyState(r.state, false);
  } catch (err) {
    log("Cannot reach the fraig server: " + err, "err");
  } finally {
    busy = false;
    document.body.style.cursor = "";
    updatePanels();
  }
}

document.getElementById("read").onclick = async () => {
  const f = document.getElementById("file").value.trim();
  if (!f) return;
  currentFile = f;
  lastStats = null;
  selected = null;
  document.getElementById("log").innerHTML = "";
  await run(`CIRRead ${f} -Replace`, false);
};
document.getElementById("file").addEventListener("keydown", e => {
  if (e.key === "Enter") document.getElementById("read").click();
});
for (const b of document.querySelectorAll("button.op")) b.onclick = () => run(b.dataset.cmd);
document.getElementById("fit").onclick = fitView;
document.getElementById("cmdline").addEventListener("keydown", e => {
  if (e.key !== "Enter") return;
  const cmd = e.target.value.trim();
  if (!cmd) return;
  e.target.value = "";
  const reads = /^cirr/i.test(cmd);
  if (reads) lastStats = null;
  run(cmd, !reads);
});
window.addEventListener("resize", resize);

(async function init() {
  resize();
  const files = await api("/api/files");
  document.getElementById("files").innerHTML = files.map(f => `<option value="${f}">`).join("");
  if (files.length) document.getElementById("file").value =
    files.find(f => /opt07\.aag$/.test(f)) || files[0];
  applyState(await api("/api/state"), false);
})();
</script>
</body>
</html>
)FRAIGPAGE";

#endif // GUI_PAGE_H
