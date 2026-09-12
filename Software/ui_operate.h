#pragma once
#include <Arduino.h>

static const char UI_OPERATE_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>Wax Axis</title>
<link rel="stylesheet" href="/style.css">
</head><body>
<div class="wrap">
  <header class="topbar">
    <div class="brand">Wax Axis</div>
    <nav class="nav">
      <a class="navlink" href="/">Operate</a>
      <a class="navlink" href="/setup">Setup</a>
      <a class="navlink" href="/settings">Settings</a>
      <a class="navlink" href="/update">Update</a>
    </nav>
  </header>

  <section class="card">
    <h2>Operate</h2>
    <div class="row">
      <button class="btn" onclick="post('/homing')">Home</button>
      <button class="btn" onclick="post('/returnhome')">Return to Park</button>
      <button class="btn danger" onclick="post('/stop')">STOP</button>
      <button class="btn" onclick="post('/reset')">RESET</button>
    </div>

    <div class="row">
      <button class="btn" onclick="post('/preheat')">Preheat Pass (1)</button>
      <button class="btn" onclick="startRun()">Run Passes</button>
    </div>

    <div class="grid2">
      <div class="field">
        <label>Operating Speed</label>
        <div class="sliderrow">
          <input id="speed" type="range" min="5" max="200" step="1" value="100" oninput="syncSpeedFromSlider()" />
          <input id="speedInput" type="number" min="5" max="200" step="1" value="100" oninput="syncSpeedFromInput()" />
          <div class="pill"><span id="speedOut">100</span></div>
        </div>
        <div class="hint">Linear scale: 100 = legacy baseline, 25 = 1/4 speed, 5 = very slow crawl, 200 = 2x speed.</div>
      </div>

      <div class="field">
        <label>Absorption Pass Count</label>
        <input id="passes" type="number" min="1" max="25" value="2" />
        <div class="hint">Typical: 1–3… more passes = more heat/time.</div>
      </div>
    </div>
  </section>

  <section class="card">
    <h2>Manual Control</h2>
    <div class="field">
      <label>Manual Velocity</label>
      <div class="sliderrow">
        <input id="manualSlider" type="range" min="-200" max="200" step="1" value="0" oninput="sendManualSlider()" />
        <div class="pill"><span id="manualOut">0</span></div>
      </div>
      <div class="hint">Center is stop. Left/right moves toward each end.</div>
    </div>

    <div class="row">
      <button class="btn" onclick="jog(1)">◀◀ Large</button>
      <button class="btn" onclick="jog(2)">◀ Small</button>
      <button class="btn" onclick="jog(3)">Small ▶</button>
      <button class="btn" onclick="jog(4)">Large ▶▶</button>
      <button class="btn ghost" onclick="stopManual()">Stop Manual</button>
    </div>
  </section>

  <section class="card">
    <h2>Status</h2>
    <div class="grid3">
      <div class="stat"><div class="k">Power</div><div class="v" id="pg">…</div></div>
      <div class="stat"><div class="k">VBUS</div><div class="v" id="vbus">…</div></div>
      <div class="stat"><div class="k">Position</div><div class="v" id="pos">…</div></div>
      <div class="stat"><div class="k">Driver</div><div class="v" id="tmc">…</div></div>
      <div class="stat"><div class="k">Heater</div><div class="v" id="heater">…</div></div>
      <div class="stat"><div class="k">Routine</div><div class="v" id="routine">…</div></div>
    </div>
    <div class="hint" id="msg">…</div>
  </section>
</div>

<script>
const speedOut = document.getElementById('speedOut');
const speedSlider = document.getElementById('speed');
const speedInput = document.getElementById('speedInput');
const manualSlider = document.getElementById('manualSlider');
const manualOut = document.getElementById('manualOut');

function clampSpeed(v){
  let n = Number(v);
  if (!Number.isFinite(n)) n = 0;
  n = Math.round(n);
  if (n < 5) n = 5;
  if (n > 200) n = 200;
  return n;
}

function applySpeedValue(v){
  const sv = String(clampSpeed(v));
  speedSlider.value = sv;
  speedInput.value = sv;
  speedOut.textContent = sv;
}

function syncSpeedFromSlider(){ applySpeedValue(speedSlider.value); }
function syncSpeedFromInput(){ applySpeedValue(speedInput.value); }

async function post(url, body){
  const opts = { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'} };
  if(body){ opts.body = body; }
  const r = await fetch(url, opts);
  return r.text();
}

async function sendManualSlider(){
  const v = Math.round(Number(manualSlider.value) || 0);
  manualOut.textContent = String(v);
  await post('/manual', 'slider=' + encodeURIComponent(v));
}

async function jog(which){
  await post('/manual', 'positionControl=' + encodeURIComponent(which));
}

async function stopManual(){
  manualSlider.value = '0';
  manualOut.textContent = '0';
  await post('/manual', 'slider=0');
}

async function startRun(){
  const sp = String(clampSpeed(speedInput.value));
  applySpeedValue(sp);
  const ps = document.getElementById('passes').value;
  await post('/setopspeed', 'routinespeed='+encodeURIComponent(sp));
  await post('/run', 'passes='+encodeURIComponent(ps));
}

async function poll(){
  try{
    const [pg,vbus,pos,tmc,wax] = await Promise.all([
      fetch('/powergood').then(r=>r.text()),
      fetch('/voltage').then(r=>r.text()),
      fetch('/position').then(r=>r.text()),
      fetch('/status').then(r=>r.text()),
      fetch('/waxstatus').then(r=>r.json())
    ]);

    document.getElementById('pg').textContent = pg;
    document.getElementById('vbus').textContent = vbus;
    document.getElementById('pos').textContent = pos;
    document.getElementById('tmc').textContent = tmc;

    document.getElementById('heater').textContent = wax.heaterOn ? 'ON' : 'OFF';
    document.getElementById('routine').textContent = wax.state;
    document.getElementById('msg').textContent = wax.msg || '';
  }catch(e){}
}

async function load(){
  try{
    const s = await fetch('/settingsjson').then(r=>r.json());
    applySpeedValue(s.routineSpeedUnits);
  }catch(e){}
  setInterval(poll, 300);
}
load();
</script>
</body></html>
)HTML";
