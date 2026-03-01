#pragma once
#include <Arduino.h>

static const char UI_CONFIG_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>Wax Axis - Setup</title>
<link rel="stylesheet" href="/style.css">
</head><body>
<div class="wrap">
  <header class="topbar">
    <div class="brand">Wax Axis</div>
    <nav class="nav">
      <a class="navlink" href="/">Operate</a>
      <a class="navlink" href="/config">Setup</a>
      <a class="navlink" href="/settings">Settings</a>
      <a class="navlink" href="/update">Update</a>
    </nav>
  </header>

  <section class="card">
    <h2>Driver Setup</h2>
    <form method="POST" action="/save">
      <div class="field">
        <label><input type="checkbox" name="enabled1" id="enabled1"> Enable Driver</label>
      </div>

      <div class="grid2">
        <div class="field">
          <label>Set Voltage</label>
          <select name="setvoltage" id="setvoltage">
            <option value="5">5V</option>
            <option value="9">9V</option>
            <option value="12">12V</option>
            <option value="15">15V</option>
            <option value="20">20V</option>
          </select>
        </div>

        <div class="field">
          <label>Microsteps</label>
          <select name="microsteps" id="microsteps">
            <option>1</option><option>2</option><option>4</option><option>8</option>
            <option>16</option><option selected>32</option><option>64</option>
            <option>128</option><option>256</option>
          </select>
        </div>

        <div class="field">
          <label>Run Current (%)</label>
          <input name="current" id="current" type="number" min="5" max="100" step="1" value="30">
        </div>

        <div class="field">
          <label>Stall Threshold</label>
          <input name="stall_threshold" id="stall_threshold" type="number" min="-64" max="63" step="1" value="10">
        </div>

        <div class="field">
          <label>Standstill Mode</label>
          <select name="standstill_mode" id="standstill_mode">
            <option value="NORMAL">NORMAL</option>
            <option value="FREEWHEELING">FREEWHEELING</option>
            <option value="BRAKING">BRAKING</option>
            <option value="STRONG_BRAKING">STRONG_BRAKING</option>
          </select>
        </div>

        <div class="field">
          <label>Homing Backoff (counts)</label>
          <input name="home_backoff" id="home_backoff" type="number" min="20" max="20000" step="10" value="200">
        </div>

        <div class="field">
          <label>Goto Tolerance (counts)</label>
          <input name="goto_tol" id="goto_tol" type="number" min="1" max="200" step="1" value="3">
        </div>

        <div class="field">
          <label>PingPong Tolerance (counts)</label>
          <input name="pp_tol" id="pp_tol" type="number" min="1" max="400" step="1" value="6">
        </div>
      </div>

      <div class="row">
        <button class="btn" type="submit">Save</button>
        <a class="btn ghost" href="/">Back</a>
      </div>
    </form>
  </section>

  <section class="card">
    <h2>Heater Test</h2>
    <div class="row">
      <button class="btn" onclick="heaterOn()">Heater ON</button>
      <button class="btn" onclick="heaterOff()">Heater OFF</button>
    </div>
    <div class="hint" id="heaterState">…</div>
  </section>
</div>

<script>
async function post(url, body){
  const r = await fetch(url, { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body: body||'' });
  return r.text();
}
async function heaterOn(){ await post('/heater','on=1'); }
async function heaterOff(){ await post('/heater','on=0'); }

async function load(){
  const s = await fetch('/settingsjson').then(r=>r.json());
  enabled1.checked = s.enabled;
  setvoltage.value = s.voltage;
  microsteps.value = s.microsteps;
  current.value = s.current;
  stall_threshold.value = s.stallThreshold;
  standstill_mode.value = s.standstillMode;
  home_backoff.value = s.homingBackoffCounts;
  goto_tol.value = s.gotoTolCounts;
  pp_tol.value = s.ppTolCounts;

  setInterval(async ()=>{
    try{
      const wax = await fetch('/waxstatus').then(r=>r.json());
      heaterState.textContent = wax.heaterReady ? (wax.heaterOn ? 'Heater: ON' : 'Heater: OFF') : 'Heater: I2C missing';
    }catch(e){}
  }, 400);
}
load();
</script>
</body></html>
)HTML";