#include "app/api.hh"

namespace app {

	static const char page [ ] = R"html(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Alloy</title>
<style>
:root { --content: #121417; --chrome: #0d0f11; --control: #1b1e22; --line: #2c3137; --divider: #22262b; --text: #e5e8ea; --muted: #949ba3; --faint: #5e656d; --accent: #3db39a; --warning: #d9a441; --bad: #e2614f; }
@media (prefers-color-scheme: light) { :root { --content: #e8eaec; --chrome: #dde0e3; --control: #f1f2f3; --line: #c3c9ce; --divider: #d1d5d9; --text: #16191c; --muted: #515a62; --faint: #878f97; --accent: #0d7c67; --warning: #a36811; --bad: #b83626; } }
* { box-sizing: border-box; }
body { margin: 0; background: var(--content); color: var(--text); font: 14px/1.4 "Segoe UI Variable Text", "Segoe UI", system-ui, sans-serif; }
header { display: flex; align-items: center; justify-content: space-between; gap: 16px; padding: 18px 28px; background: var(--chrome); border-bottom: 1px solid var(--divider); }
h1 { margin: 0; font: 600 18px "Segoe UI Variable Display", "Segoe UI", sans-serif; }
#state { color: var(--muted); font-size: 13px; }
button { font: inherit; color: var(--text); background: var(--control); border: 1px solid var(--line); border-radius: 4px; padding: 6px 14px; cursor: pointer; }
button:hover { border-color: var(--faint); }
button[hidden] { display: none; }
.caption { color: var(--muted); font-size: 11px; font-weight: 600; letter-spacing: 0.06em; text-transform: uppercase; }
.metrics { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); border-bottom: 1px solid var(--divider); }
.metric { padding: 16px 28px; border-right: 1px solid var(--divider); }
.metric .value { font: 600 24px "Segoe UI Variable Display", "Segoe UI", sans-serif; margin-top: 4px; font-variant-numeric: tabular-nums; }
.metric .detail { color: var(--muted); font-size: 12px; }
section { padding: 18px 28px; }
canvas { width: 100%; height: 140px; display: block; margin-top: 10px; }
table { width: 100%; border-collapse: collapse; margin-top: 8px; font-variant-numeric: tabular-nums; }
th { text-align: left; color: var(--faint); font-weight: 400; font-size: 12px; padding: 6px 0; border-bottom: 1px solid var(--divider); }
td { padding: 8px 0; border-bottom: 1px solid var(--divider); }
td.number, th.number { text-align: right; }
#problem { color: var(--warning); padding: 12px 28px; display: none; }
</style>
</head>
<body>
<header>
<div><h1 id="title">Alloy</h1><div id="state">Connecting</div></div>
<div><button id="pause" hidden>Pause</button> <button id="resume" hidden>Resume</button></div>
</header>
<div id="problem"></div>
<div class="metrics">
<div class="metric"><div class="caption">Hashrate</div><div class="value" id="rate">&mdash;</div><div class="detail" id="average"></div></div>
<div class="metric"><div class="caption">Processor</div><div class="value" id="cpu">&mdash;</div><div class="detail" id="threads"></div></div>
<div class="metric"><div class="caption">Graphics</div><div class="value" id="gpu">&mdash;</div><div class="detail" id="cards"></div></div>
<div class="metric"><div class="caption">Shares</div><div class="value" id="accepted">&mdash;</div><div class="detail" id="rejected"></div></div>
<div class="metric"><div class="caption">Uptime</div><div class="value" id="uptime">&mdash;</div><div class="detail" id="hashes"></div></div>
</div>
<section><div class="caption">Hashrate, last ten minutes</div><canvas id="chart"></canvas></section>
<section><div class="caption">Pool</div><table><tbody id="pool"></tbody></table></section>
<section><div class="caption">Graphics cards</div><table><thead><tr><th>Card</th><th class="number">Hashrate</th><th class="number">Temperature</th><th class="number">State</th></tr></thead><tbody id="gpus"></tbody></table></section>
<script>
const token = new URLSearchParams(location.hash.slice(1)).get("token") || "";
const auth = token ? { Authorization: "Bearer " + token } : {};
const points = [];
const $ = id => document.getElementById(id);
const prefixes = ["", "k", "M", "G", "T", "P"];
function scaled(value, unit) { let step = 0; while (value >= 1000 && step < 5) { value /= 1000; step++; } return value.toFixed(value < 10 ? 2 : value < 100 ? 1 : 0) + " " + prefixes[step] + unit; }
function clock(seconds) { seconds = Math.floor(seconds); const h = Math.floor(seconds / 3600), m = Math.floor(seconds / 60) % 60, s = seconds % 60; return (h ? h + ":" + String(m).padStart(2, "0") : m) + ":" + String(s).padStart(2, "0"); }
function row(label, value) { const tr = document.createElement("tr"); const a = document.createElement("td"); const b = document.createElement("td"); a.textContent = label; a.style.color = "var(--muted)"; b.textContent = value; b.className = "number"; tr.append(a, b); return tr; }
function draw() {
  const canvas = $("chart"), scale = devicePixelRatio || 1, w = canvas.clientWidth, h = canvas.clientHeight;
  canvas.width = w * scale; canvas.height = h * scale;
  const g = canvas.getContext("2d"); g.scale(scale, scale); g.clearRect(0, 0, w, h);
  const style = getComputedStyle(document.documentElement);
  g.strokeStyle = style.getPropertyValue("--divider"); g.lineWidth = 1;
  for (let i = 1; i <= 4; i++) { const y = Math.round(h - (h - 2) * i / 4) + 0.5; g.beginPath(); g.moveTo(0, y); g.lineTo(w, y); g.stroke(); }
  if (points.length < 2) return;
  const top = Math.max(...points) * 1.12 || 1, step = w / 299;
  g.beginPath();
  points.forEach((v, i) => { const x = w - (points.length - 1 - i) * step, y = h - v / top * (h - 2); i ? g.lineTo(x, y) : g.moveTo(x, y); });
  const accent = style.getPropertyValue("--accent").trim();
  g.strokeStyle = accent; g.lineWidth = 1.75; g.stroke();
  g.lineTo(w, h); g.lineTo(w - (points.length - 1) * step, h); g.closePath();
  const fill = g.createLinearGradient(0, 0, 0, h); fill.addColorStop(0, accent + "42"); fill.addColorStop(1, accent + "00"); g.fillStyle = fill; g.fill();
}
async function act(path) { await fetch(path, { method: "POST", headers: auth }); refresh(); }
$("pause").onclick = () => act("/1/pause");
$("resume").onclick = () => act("/1/resume");
async function refresh() {
  try {
    const reply = await fetch("/api.json", { headers: auth, cache: "no-store" });
    if (reply.status === 401 || reply.status === 403) { $("problem").style.display = "block"; $("problem").textContent = "This rig needs its access token. Open the page as http://" + location.host + "/#token=YOUR_TOKEN, with the token from Alloy's settings."; return; }
    const s = await reply.json();
    $("problem").style.display = "none";
    document.title = "Alloy on " + s.name;
    $("title").textContent = "Alloy on " + s.name;
    $("state").textContent = !s.running ? "Not mining" : s.paused ? (s.pause_reason || "Paused") : "Mining " + s.variant + " (" + s.mode + ")";
    $("rate").textContent = s.running ? scaled(s.hashrate.total, "H/s") : "—";
    $("average").textContent = "average " + scaled(s.hashrate.average, "H/s");
    $("cpu").textContent = scaled(s.hashrate.cpu, "H/s");
    $("threads").textContent = s.threads + " threads";
    $("gpu").textContent = scaled(s.hashrate.gpu, "H/s");
    $("cards").textContent = s.gpus.length + (s.gpus.length === 1 ? " card" : " cards");
    $("accepted").textContent = s.shares.accepted;
    $("rejected").textContent = s.shares.rejected + " rejected, " + s.shares.stale + " stale";
    $("uptime").textContent = s.running ? clock(s.uptime) : "—";
    $("hashes").textContent = scaled(s.hashes, "hashes").replace(" hashes", "") ;
    $("pause").hidden = !s.control || !s.running || s.paused;
    $("resume").hidden = !s.control || !s.running || !s.paused;
    const pool = $("pool"); pool.replaceChildren(row("Address", s.pool.address + (s.pool.backup ? " (backup)" : "")), row("Connection", s.pool.connected ? "Connected, " + s.pool.latency.toFixed(1) + " ms" : "Offline"), row("Share difficulty", s.difficulty), row("Best share", s.best));
    const gpus = $("gpus"); gpus.replaceChildren(...s.gpus.map(c => { const tr = document.createElement("tr"); [c.name, scaled(c.hashrate, "H/s"), c.temperature ? c.temperature.toFixed(0) + " °C" : "—", c.state].forEach((v, i) => { const td = document.createElement("td"); td.textContent = v; if (i) td.className = "number"; tr.append(td); }); return tr; }));
    points.push(s.running ? s.hashrate.total : 0); if (points.length > 300) points.shift();
    draw();
  } catch (e) { $("state").textContent = "Can't reach Alloy"; }
}
addEventListener("resize", draw);
refresh(); setInterval(refresh, 2000);
</script>
</body>
</html>
)html";

	const char* dashboard_page ( ) {

		return page;

	}

}
