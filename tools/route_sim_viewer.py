"""Build the route-drive playback page from build/route-sims/*/result*.json.

Usage: python tools/route_sim_viewer.py [--output build/route-sims/route-coach-playback.html]
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

PAGE = r"""<title>Route Coach Playback</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600;700&family=Atkinson+Hyperlegible:wght@400;700&family=JetBrains+Mono:wght@400;600&display=swap">
<style>
/* Layout: signage-style header, then map (playback) beside the coach's voice log; tables below. */
:root {
  --bg: #eef1ee; --panel: #ffffff; --fg: #1c2320; --muted: #5d6a64; --line: #d4dbd6;
  --road: #39433f; --road-casing: #ffffff; --accent: #d99a00; --ok: #2e8b57; --bad: #c0412c; --info: #2c6db0;
  --display: "Barlow Condensed", "Arial Narrow", sans-serif; --body: "Atkinson Hyperlegible", system-ui, sans-serif;
  --mono: "JetBrains Mono", ui-monospace, monospace;
}
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
  --bg: #111513; --panel: #1a201d; --fg: #e6ece8; --muted: #9aa8a1; --line: #2c3530;
  --road: #a9b5ae; --road-casing: #111513; --accent: #f0b429; --ok: #4fbf82; --bad: #ef6a52; --info: #6aa8ea; color-scheme: dark } }
:root[data-theme="dark"] {
  --bg: #111513; --panel: #1a201d; --fg: #e6ece8; --muted: #9aa8a1; --line: #2c3530;
  --road: #a9b5ae; --road-casing: #111513; --accent: #f0b429; --ok: #4fbf82; --bad: #ef6a52; --info: #6aa8ea; color-scheme: dark }
* { box-sizing: border-box }
body { background: var(--bg); color: var(--fg); font: 15px/1.5 var(--body); margin: 0;
  padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px)); padding-block: 20px 40px }
.wrap { max-width: 1180px; margin: 0 auto; display: grid; gap: 18px }
header { display: grid; gap: 6px }
h1 { font: 700 34px/1.05 var(--display); letter-spacing: .01em; margin: 0; text-wrap: balance }
.lede { color: var(--muted); margin: 0; max-width: 70ch }
.routes { display: flex; flex-wrap: wrap; gap: 8px }
.routes button { font: 600 15px var(--display); letter-spacing: .04em; text-transform: uppercase; padding: 7px 12px;
  border: 1.5px solid var(--line); background: var(--panel); color: var(--fg); border-radius: 6px; cursor: pointer }
.routes button[aria-pressed="true"], .modes button[aria-pressed="true"] { border-color: var(--accent); box-shadow: inset 0 -3px 0 var(--accent) }
.pick { display: grid; gap: 8px }
.modes { display: flex; flex-wrap: wrap; gap: 0 }
.modes button { font: 600 14px var(--body); padding: 6px 12px; border: 1.5px solid var(--line); background: var(--panel); color: var(--fg); cursor: pointer }
.modes button:first-child { border-radius: 6px 0 0 6px } .modes button:last-child { border-radius: 0 6px 6px 0; border-left: 0 }
.modes button:focus-visible { outline: 2px solid var(--info); outline-offset: 2px }
.routename { margin: 0; color: var(--muted); font-size: 14px }
.totals { display: grid; grid-template-columns: repeat(auto-fit, minmax(170px, 1fr)); gap: 10px }
.stat { background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 10px 12px; font-size: 13.5px; color: var(--muted) }
.stat b { display: block; font: 700 28px/1.1 var(--display); color: var(--fg); font-variant-numeric: tabular-nums }
.routes button:focus-visible, .controls button:focus-visible, .controls select:focus-visible { outline: 2px solid var(--info); outline-offset: 2px }
.chips { display: flex; flex-wrap: wrap; gap: 8px }
.chip { font: 600 13px var(--mono); padding: 4px 9px; border-radius: 999px; border: 1px solid var(--line); background: var(--panel) }
.chip.ok { color: var(--ok); border-color: color-mix(in srgb, var(--ok) 45%, var(--line)) }
.chip.bad { color: var(--bad); border-color: color-mix(in srgb, var(--bad) 45%, var(--line)) }
.stage { display: grid; grid-template-columns: minmax(0, 1.6fr) minmax(0, 1fr); gap: 18px }
@media (max-width: 820px) { .stage { grid-template-columns: minmax(0, 1fr) } }
.panel { background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 14px; min-width: 0 }
.mapbox { position: relative }
svg.map { width: 100%; height: auto; display: block; max-width: 100%; max-height: 78vh; margin: 0 auto }
.bubble { position: absolute; left: 12px; right: 12px; bottom: 12px; background: var(--fg); color: var(--bg);
  border-radius: 10px; padding: 10px 14px; font: 600 16px/1.35 var(--body); box-shadow: 0 6px 20px rgb(0 0 0 / .25) }
.bubble small { display: block; font: 400 12px var(--mono); opacity: .75; margin-bottom: 2px }
.controls { display: flex; flex-wrap: wrap; align-items: center; gap: 10px; margin-top: 10px }
.controls button { font: 700 15px var(--display); letter-spacing: .05em; text-transform: uppercase; padding: 8px 16px; border-radius: 6px;
  border: 0; background: var(--accent); color: #1c1600; cursor: pointer }
.controls button.secondary { background: transparent; color: var(--fg); border: 1.5px solid var(--line) }
.controls label { display: flex; align-items: center; gap: 6px; color: var(--muted); font-size: 14px }
.controls select { font: 14px var(--body); padding: 5px; border-radius: 6px; border: 1px solid var(--line); background: var(--panel); color: var(--fg) }
input[type=range] { flex: 1 1 200px; accent-color: var(--accent) }
.clock { font: 600 14px var(--mono); font-variant-numeric: tabular-nums; min-width: 12ch }
.speed { font: 600 13px var(--mono); color: var(--muted); font-variant-numeric: tabular-nums }
h2 { font: 600 20px var(--display); letter-spacing: .03em; text-transform: uppercase; margin: 0 0 8px }
.log { list-style: none; margin: 0; padding: 0; display: grid; gap: 2px; max-height: 520px; overflow-y: auto }
.log li { display: grid; grid-template-columns: 6.5ch minmax(0, 1fr); gap: 8px; padding: 6px 8px; border-radius: 6px; cursor: pointer }
.log li:hover { background: color-mix(in srgb, var(--accent) 10%, transparent) }
.log li.now { background: color-mix(in srgb, var(--accent) 22%, transparent) }
.log time { font: 600 13px var(--mono); color: var(--muted); font-variant-numeric: tabular-nums; padding-top: 2px }
.log .meta { display: block; color: var(--muted); font-size: 12.5px }
.tables { display: grid; gap: 18px }
.scroll { overflow-x: auto }
table { border-collapse: collapse; width: 100%; font-size: 14px }
th, td { text-align: left; padding: 7px 10px; border-bottom: 1px solid var(--line); white-space: nowrap }
th { font: 600 13px var(--display); letter-spacing: .05em; text-transform: uppercase; color: var(--muted) }
td.num { font-family: var(--mono); font-variant-numeric: tabular-nums; text-align: right }
.pill { font: 700 12px var(--mono); padding: 2px 8px; border-radius: 999px; color: var(--panel) }
.pill.ok { background: var(--ok) } .pill.bad { background: var(--bad) } .pill.na { background: var(--muted) }
td.why { color: var(--muted); white-space: normal; min-width: 18ch }
.legend { display: flex; flex-wrap: wrap; gap: 14px; color: var(--muted); font-size: 13px; margin-top: 8px }
.legend span { display: inline-flex; align-items: center; gap: 6px }
.dot { width: 11px; height: 11px; border-radius: 50%; display: inline-block }
.note { color: var(--muted); font-size: 13.5px; max-width: 80ch; margin: 0 }
@media (prefers-reduced-motion: reduce) { * { scroll-behavior: auto } }
</style>

<div class="wrap">
  <header>
    <h1>Route Coach Playback</h1>
    <p class="lede">The LearnDrive brain driving real Kerala roads it has never seen. A simulated learner follows each OpenStreetMap route, and the brain gets what Google Navigation gives on the phone. Press play to watch the car and hear each instruction when the app would speak it.</p>
    <div class="totals">__TOTALS__</div>
    <div class="pick">
      <div class="routes" id="routes" role="group" aria-label="City"></div>
      <div class="modes" id="modes" role="group" aria-label="Navigation">
        <button type="button" data-mode="nav" aria-pressed="true">With destination</button>
        <button type="button" data-mode="nodest" aria-pressed="false">No destination</button>
      </div>
      <p class="routename" id="routename"></p>
    </div>
    <div class="chips" id="chips"></div>
  </header>

  <section class="stage">
    <div class="panel mapbox">
      <svg class="map" id="map" viewBox="0 0 800 600" role="img" aria-label="Route map"></svg>
      <div class="bubble" id="bubble" hidden><small id="bubbleMeta"></small><span id="bubbleText"></span></div>
      <div class="controls">
        <button id="play" type="button">Play</button>
        <button id="restart" class="secondary" type="button">Restart</button>
        <label for="rate">Speed <select id="rate"><option value="4">4×</option><option value="8" selected>8×</option><option value="16">16×</option><option value="1">1× (real time)</option></select></label>
        <label for="voice"><input id="voice" type="checkbox" checked> Speak instructions</label>
        <input id="scrub" type="range" min="0" max="100" step="0.1" value="0" aria-label="Playback position">
        <span class="clock" id="clock">0:00</span><span class="speed" id="kmh">0 km/h</span>
      </div>
      <div class="legend">
        <span><i class="dot" style="background:var(--ok)"></i>coached in time</span>
        <span><i class="dot" style="background:var(--bad)"></i>missed or mistimed</span>
        <span><i class="dot" style="background:var(--muted)"></i>learner's choice (no destination)</span>
        <span><i class="dot" style="background:transparent;border:2px solid var(--accent);border-radius:2px"></i>sharp bend / bump</span>
        <span><i class="dot" style="background:var(--info)"></i>instruction spoken here</span>
        <span><i class="dot" style="background:var(--accent)"></i>car</span>
      </div>
    </div>
    <div class="panel">
      <h2>What the coach said</h2>
      <ol class="log" id="log"></ol>
    </div>
  </section>

  <section class="tables">
    <div class="panel">
      <h2>Every turn on the route</h2>
      <div class="scroll"><table id="turns"><thead><tr><th>Turn</th><th>Road</th><th>At</th><th class="num">“Turn ahead” before</th><th class="num">Entry before</th><th>Result</th><th>Note</th></tr></thead><tbody></tbody></table></div>
      <p class="note">“Turn ahead”: mirrors and signal (or ease off at a junction). Entry: clutch down, look both ways, sound the horn. Your instructor on 5 Nov spoke 4–16 s before turns.</p>
    </div>
    <p class="note">Without a destination the brain reads only the map. It prepares the learner for junctions any instructor could see coming: the road ends, the road bends away at a junction, a main road ahead, or a crossroads. Turning off a through road into a side road is the learner's choice, so those turns are shown but not scored.</p>
    <div class="note">Known gaps these drives found and that are not fixed yet:
      <ul class="note" style="margin:6px 0 0;padding-left:20px">
        <li>With no destination, a slip lane merging onto a highway (Thiruvananthapuram, 2:21) is not seen as a junction, so the cue comes late (50 m) with no "look" step. The checker files this one as the learner's choice, so it is not in the score above.</li>
        <li>On a right-then-left jog (Thiruvananthapuram, 9:49), the left part gets no warning.</li>
        <li>Two turns a few seconds apart get only the "look both ways" cue for the second one. A real instructor would say "right, then left straight after".</li>
      </ul>
    </div>
    <p class="note">Simulation limits: the learner and map are simulated, and Google's own announcements and timing on the phone are not reproduced. No other road users appear; camera coaching is tested on real drive videos. Map data © OpenStreetMap contributors (ODbL); routes from OSRM.</p>
  </section>
</div>

<script type="application/json" id="data">__DATA__</script>
<script>
(() => {
  const DATA = JSON.parse(document.getElementById('data').textContent);
  const $ = id => document.getElementById(id);
  const svg = $('map'), NS = 'http://www.w3.org/2000/svg';
  let route = null, proj = null, t = 0, playing = false, last = null, spokenUpTo = -1, carEl = null;
  const fmt = s => `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, '0')}`;
  const kinds = {1: 'Left turn', 2: 'Right turn', 3: 'Fork', 6: 'U-turn', 7: 'Roundabout'};

  function el(tag, attrs, parent) { const e = document.createElementNS(NS, tag); for (const k in attrs) e.setAttribute(k, attrs[k]); (parent || svg).appendChild(e); return e; }
  function project(points) {
    const lat0 = points.reduce((a, p) => a + p[0], 0) / points.length, k = Math.cos(lat0 * Math.PI / 180);
    const xs = points.map(p => p[1] * k), ys = points.map(p => -p[0]);
    const minX = Math.min(...xs), maxX = Math.max(...xs), minY = Math.min(...ys), maxY = Math.max(...ys);
    // The map takes the route's own shape: 800 wide, as tall as the route needs (450-1000).
    const H = Math.max(450, Math.min(1000, 800 * (maxY - minY) / (maxX - minX || 1)));
    svg.setAttribute('viewBox', `0 0 800 ${H.toFixed(0)}`);
    const scale = Math.min(720 / (maxX - minX || 1), (H - 90) / (maxY - minY || 1));
    const ox = (800 - (maxX - minX) * scale) / 2, oy = (H - (maxY - minY) * scale) / 2;
    return (lat, lon) => [ox + (lon * k - minX) * scale, oy + (-lat - minY) * scale];
  }
  function trackAt(time) {
    const tr = route.track; let lo = 0, hi = tr.length - 1;
    if (time <= tr[0][0]) return tr[0]; if (time >= tr[hi][0]) return tr[hi];
    while (hi - lo > 1) { const mid = (lo + hi) >> 1; (tr[mid][0] <= time ? lo = mid : hi = mid); }
    const a = tr[lo], b = tr[hi], f = (time - a[0]) / (b[0] - a[0] || 1);
    return [time, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f, a[3] + (b[3] - a[3]) * f];
  }
  function draw() {
    svg.replaceChildren();
    proj = project(route.route.polyline);
    const d = route.route.polyline.map((p, i) => (i ? 'L' : 'M') + proj(p[0], p[1]).map(v => v.toFixed(1)).join(' ')).join('');
    el('path', {d, fill: 'none', stroke: 'var(--road-casing)', 'stroke-width': 16, 'stroke-linecap': 'round', 'stroke-linejoin': 'round'});
    el('path', {d, fill: 'none', stroke: 'var(--road)', 'stroke-width': 9, 'stroke-linecap': 'round', 'stroke-linejoin': 'round'});
    for (const e of route.spoken) { const [x, y] = proj(e.lat, e.lon); el('circle', {cx: x, cy: y, r: 6, fill: 'var(--info)', stroke: 'var(--panel)', 'stroke-width': 2}); }
    const turnOk = new Map(route.turns.map(r => [Math.round(r.at_s * 10), r.ok]));
    for (const b of [...(route.bends || []), ...(route.bumps || [])]) {
      const pt = trackAt(b.at_s), [x, y] = proj(pt[1], pt[2]);
      el('rect', {x: x - 9, y: y - 9, width: 18, height: 18, rx: 3, fill: 'var(--panel)', stroke: 'var(--accent)', 'stroke-width': 4});
    }
    for (const m of route.route.maneuvers) {
      if (!kinds[m.code]) continue;
      const pt = trackAt(m.t), [x, y] = proj(pt[1], pt[2]);
      const ok = turnOk.get(Math.round(m.t * 10));
      el('rect', {x: x - 11, y: y - 11, width: 22, height: 22, rx: 4, fill: ok == null ? 'var(--muted)' : ok ? 'var(--ok)' : 'var(--bad)', stroke: 'var(--panel)', 'stroke-width': 3, transform: `rotate(45 ${x} ${y})`});
    }
    const start = proj(...route.route.polyline[0]), end = proj(...route.route.polyline.at(-1));
    el('text', {x: start[0] + (start[0] > 560 ? -16 : 16), y: start[1] - 14, 'text-anchor': start[0] > 560 ? 'end' : 'start', fill: 'var(--fg)', 'font-family': 'var(--display)', 'font-weight': 700, 'font-size': 30}).textContent = 'START';
    el('text', {x: end[0] + (end[0] > 560 ? -16 : 16), y: end[1] - 14, 'text-anchor': end[0] > 560 ? 'end' : 'start', fill: 'var(--fg)', 'font-family': 'var(--display)', 'font-weight': 700, 'font-size': 30}).textContent = 'DESTINATION';
    carEl = el('circle', {r: 13, fill: 'var(--accent)', stroke: 'var(--fg)', 'stroke-width': 3});
  }
  function fillTables() {
    const s = route.summary;
    $('chips').innerHTML = '';
    const chip = (text, cls) => { const c = document.createElement('span'); c.className = 'chip ' + (cls || ''); c.textContent = text; $('chips').appendChild(c); };
    chip(`${s.route_km} km · ${s.drive_minutes} min`);
    chip(`turns coached in time ${s.turns_ok}/${s.turns_checked}`, s.turns_ok === s.turns_checked ? 'ok' : 'bad');
    if (s.turns_learner_choice) chip(`${s.turns_learner_choice} side-road turns: learner's choice`);
    if (s.sharp_bends) chip(`sharp bends ${s.bends_ok}/${s.sharp_bends}`, s.bends_ok === s.sharp_bends ? 'ok' : 'bad');
    chip(s.bumps_on_route ? `mapped bumps ${s.bumps_ok}/${s.bumps_on_route}` : 'no bumps mapped on this route');
    chip(`${s.instructions} instructions · ${s.per_minute}/min`);
    const log = $('log'); log.innerHTML = '';
    route.spoken.forEach((e, i) => {
      const li = document.createElement('li'); li.dataset.i = i;
      li.innerHTML = `<time>${fmt(e.time)}</time><span>${e.text.replace(/</g, '&lt;')}<span class="meta">${Math.round(e.speed_kmh)} km/h${e.next_maneuver_m != null ? ` · next maneuver ${Math.round(e.next_maneuver_m)} m` : ''}</span></span>`;
      li.addEventListener('click', () => { setTime(e.time - 2); });
      log.appendChild(li);
    });
    const tb = $('turns').querySelector('tbody'); tb.innerHTML = '';
    for (const r of route.turns) {
      const tr = document.createElement('tr');
      tr.innerHTML = `<td>${r.kind}</td><td>${(r.street || '—').replace(/</g, '&lt;')}</td><td class="num">${fmt(r.at_s)}</td>` +
        `<td class="num">${r.prepare_lead_s != null ? r.prepare_lead_s + ' s · ' + r.prepare_lead_m + ' m' : r.chained ? 'no room' : 'none'}</td>` +
        `<td class="num">${r.entry_lead_s != null ? r.entry_lead_s + ' s' : (r.kind === 'fork' ? 'not needed' : 'none')}</td>` +
        `<td><span class="pill ${r.ok == null ? 'na' : r.ok ? 'ok' : 'bad'}">${r.ok == null ? 'NOT SCORED' : r.ok ? 'OK' : 'MISSED'}</span></td>` +
        `<td class="why">${r.why ? r.why : r.chained ? 'back-to-back: entry cue only' : ''}</td>`;
      tb.appendChild(tr);
    }
    $('scrub').max = route.track.at(-1)[0];
  }
  function speak(e) {
    $('bubble').hidden = false; $('bubbleText').textContent = e.text;
    $('bubbleMeta').textContent = `${fmt(e.time)} · ${Math.round(e.speed_kmh)} km/h${e.next_maneuver_m != null ? ` · ${Math.round(e.next_maneuver_m)} m to the next maneuver` : ''}`;
    if ($('voice').checked && 'speechSynthesis' in window) {
      try { speechSynthesis.cancel(); const u = new SpeechSynthesisUtterance(e.text); u.lang = 'en-IN'; u.rate = 1.05; speechSynthesis.speak(u); } catch (_) {}
    }
    document.querySelectorAll('.log li.now').forEach(x => x.classList.remove('now'));
    const li = document.querySelector(`.log li[data-i="${route.spoken.indexOf(e)}"]`); if (li) { li.classList.add('now'); li.scrollIntoView({block: 'nearest'}); }
  }
  function render() {
    const p = trackAt(t), [x, y] = proj(p[1], p[2]);
    carEl.setAttribute('cx', x); carEl.setAttribute('cy', y);
    $('clock').textContent = `${fmt(t)} / ${fmt(route.track.at(-1)[0])}`; $('kmh').textContent = `${Math.round(p[3])} km/h`;
    $('scrub').value = t;
  }
  function setTime(v) { t = Math.max(0, Math.min(v, route.track.at(-1)[0])); spokenUpTo = route.spoken.findIndex(e => e.time > t) - 1; if (spokenUpTo < -1) spokenUpTo = route.spoken.length - 1; render(); }
  function frame(now) {
    if (!playing) return;
    const dt = last == null ? 0 : (now - last) / 1000; last = now;
    t += dt * Number($('rate').value);
    while (spokenUpTo + 1 < route.spoken.length && route.spoken[spokenUpTo + 1].time <= t) speak(route.spoken[++spokenUpTo]);
    if (t >= route.track.at(-1)[0]) { t = route.track.at(-1)[0]; playing = false; $('play').textContent = 'Play'; }
    render();
    if (playing) requestAnimationFrame(frame);
  }
  let city = 0, mode = 'nav';
  function choose(i, m) {
    city = i ?? city; mode = m ?? mode;
    const c = DATA[city]; if (!c[mode]) mode = c.nav ? 'nav' : 'nodest';
    route = c[mode]; playing = false; $('play').textContent = 'Play'; $('bubble').hidden = true;
    try { speechSynthesis.cancel(); } catch (_) {}
    document.querySelectorAll('.routes button').forEach((b, j) => b.setAttribute('aria-pressed', String(j === city)));
    document.querySelectorAll('.modes button').forEach(b => { b.setAttribute('aria-pressed', String(b.dataset.mode === mode)); b.disabled = !c[b.dataset.mode]; });
    $('routename').textContent = c.label + (mode === 'nodest'
      ? ' · no destination: the brain reads only the map ahead'
      : ' · navigating: the brain gets the next turn as Google Navigation gives it');
    draw(); fillTables(); setTime(0);
  }
  DATA.forEach((c, i) => { const b = document.createElement('button'); b.type = 'button'; b.textContent = c.city; b.addEventListener('click', () => choose(i)); $('routes').appendChild(b); });
  document.querySelectorAll('.modes button').forEach(b => b.addEventListener('click', () => choose(null, b.dataset.mode)));
  $('play').addEventListener('click', () => { playing = !playing; $('play').textContent = playing ? 'Pause' : 'Play'; last = null; if (playing) requestAnimationFrame(frame); else try { speechSynthesis.cancel(); } catch (_) {} });
  $('restart').addEventListener('click', () => { setTime(0); $('bubble').hidden = true; });
  $('scrub').addEventListener('input', e => setTime(Number(e.target.value)));
  choose(0);
})();
</script>
"""


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/route-sims/route-coach-playback.html")
    args = parser.parse_args(argv)
    cities = [("kochi-kaloor-panampilly", "Kochi", "Kaloor → Panampilly Nagar"),
              ("thrissur-round-ayyanthole", "Thrissur", "Swaraj Round → Ayyanthole"),
              ("kozhikode-mananchira-beach", "Kozhikode", "Mananchira → Beach Road"),
              ("trivandrum-eastfort-kowdiar", "Thiruvananthapuram", "East Fort → Kowdiar"),
              ("guruvayur-temple-chavakkad", "Guruvayur", "Temple → Chavakkad")]
    data, total = [], {"nav": [0, 0], "nodest": [0, 0, 0], "bends": [0, 0], "bumps": 0, "km": 0.0}
    for folder, city, label in cities:
        entry = {"city": city, "label": label}
        for name, mode in (("result.json", "nav"), ("result-nodest.json", "nodest")):
            f = ROOT / "build/route-sims" / folder / name
            if not f.exists():
                continue
            r = json.loads(f.read_text(encoding="utf-8"))
            if "track" not in r:
                continue
            r.pop("scout", None)  # Debug trace; not needed for playback.
            entry[mode] = r
            sm = r["summary"]
            total[mode][0] += sm["turns_ok"]; total[mode][1] += sm["turns_checked"]
            if mode == "nav":
                total["bends"][0] += sm["bends_ok"]; total["bends"][1] += sm["sharp_bends"]
                total["bumps"] += sm["bumps_on_route"]; total["km"] += sm["route_km"]
            else:
                total["nodest"][2] += sm.get("turns_learner_choice", 0)
        if "nav" in entry or "nodest" in entry:
            data.append(entry)
    tiles = [
        (f"{total['nav'][0]}/{total['nav'][1]}", f"turns coached in time on {len(data)} unseen routes ({total['km']:.1f} km), with a destination"),
        (f"{total['nodest'][0]}/{total['nodest'][1]}", f"junctions the map shows, coached with no destination ({total['nodest'][2]} side-road turns left to the learner)"),
        (f"{total['bends'][0]}/{total['bends'][1]}", "sharp bends named ahead, with the horn"),
        (f"{total['bumps']}", "speed bumps mapped in OpenStreetMap on these routes, so bump coaching is untested here"),
    ]
    totals = "".join(f'<div class="stat"><b>{a}</b>{b}</div>' for a, b in tiles)
    page = PAGE.replace("__TOTALS__", totals).replace("__DATA__", json.dumps(data).replace("</", "<\\/"))
    args.output.write_text(page, encoding="utf-8")
    print(args.output, len(data), "routes")


if __name__ == "__main__":
    main()
