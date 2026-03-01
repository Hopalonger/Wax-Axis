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
    </div>

    <div class="row">
      <button class="btn" onclick="post('/preheat')">Preheat Pass (1)</button>
      <button class="btn" onclick="startRun()">Run Passes</button>
    </div>

    <div class="grid2">
      <div class="field">
        <label>Operating Speed</label>
        <div class="sliderrow">
          <input id="speed" type="range" min="100" max="3000" step="10" value="600" oninput="speedOut.textContent=this.value" />
          <div class="pill"><span id="speedOut">600</span></div>
        </div>
        <div class="hint">Preheat + return speeds are derived from Settings…</div>
      </div>

      <div class="field">
        <label>Absorption Pass Count</label>
        <input id="passes" type="number" min="1" max="25" value="2" />
        <div class="hint">Typical: 1–3… more passes = more heat/time.</div>
      </div>
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

async function post(url, body){
  const opts = { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'} };
  if(body){ opts.body = body; }
  const r = await fetch(url, opts);
  return r.text();
}

async function startRun(){
  const sp = document.getElementById('speed').value;
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
    document.getElementById('speed').value = s.routineSpeedUnits;
    speedOut.textContent = s.routineSpeedUnits;
  }catch(e){}
  setInterval(poll, 300);
}
load();
</script>
</body></html>
)HTML";