#pragma once
#include <Arduino.h>

static const char UI_SETTINGS_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>Wax Axis - Settings</title>
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
    <h2>Routine Settings</h2>
    <form method="POST" action="/savesettings">
      <div class="grid2">
        <div class="field">
          <label>Preheat Speed Multiplier</label>
          <input name="preheat_mult" id="preheat_mult" type="number" min="1.0" max="5.0" step="0.1" value="2.0">
          <div class="hint">Preheat forward speed = operating speed × this multiplier.</div>
        </div>

        <div class="field">
          <label>Return-to-Park Speed (%)</label>
          <input name="return_pct" id="return_pct" type="number" min="100" max="400" step="5" value="200">
          <div class="hint">Return speed = operating speed × (percent/100).</div>
        </div>

        <div class="field">
          <label>Heater On Delay (ms)</label>
          <input name="heater_delay" id="heater_delay" type="number" min="0" max="5000" step="50" value="500">
          <div class="hint">Wait after heater ON before motion starts.</div>
        </div>

        <div class="field">
          <label>Home Side</label>
          <select name="home_side" id="home_side">
            <option value="min">Min End (lower encoder count)</option>
            <option value="max">Max End (higher encoder count)</option>
          </select>
          <div class="hint">Pick which physical end is treated as “home”.</div>
        </div>

        <div class="field">
          <label>Homing Speed (units)</label>
          <input name="homing_speed" id="homing_speed" type="number" min="20" max="300" step="1" value="150">
          <div class="hint">Base speed used during homing seek passes for tuning/testing.</div>
        </div>

        <div class="field">
          <label>Homing Timeout (ms)</label>
          <input name="homing_timeout" id="homing_timeout" type="number" min="10000" max="600000" step="1000" value="120000">
          <div class="hint">If your homing takes ~30s… set this to 60000–120000+.</div>
        </div>

        <div class="field">
          <label>Edge Keep-off (counts)</label>
          <input name="edge_keepoff" id="edge_keepoff" type="number" min="0" max="50000" step="10" value="800">
          <div class="hint">How far from EACH hard stop the operating ends stay.</div>
        </div>

        <div class="field">
          <label>Park Offset from Home Edge (counts)</label>
          <input name="park_offset" id="park_offset" type="number" min="0" max="50000" step="10" value="1000">
          <div class="hint">Where the gantry rests so it doesn’t sit on the stop.</div>
        </div>

        <div class="field">
          <label>Rail Length (read-only)</label>
          <input id="rail_len" type="text" readonly value="...">
          <div class="hint">Measured during homing.</div>
        </div>

        <div class="field">
          <label>Park Position (read-only)</label>
          <input id="park_pos" type="text" readonly value="...">
          <div class="hint">Where Return-to-Park and routines begin.</div>
        </div>
      </div>

      <div class="row">
        <button class="btn" type="submit">Save</button>
        <a class="btn ghost" href="/">Back</a>
      </div>
    </form>
  </section>
</div>

<script>
async function load(){
  const s = await fetch('/settingsjson').then(r=>r.json());
  preheat_mult.value = s.preheatMult;
  return_pct.value = s.returnSpeedPct;
  heater_delay.value = s.heaterDelayMs;

  home_side.value = s.homeSide;
  homing_speed.value = s.homingSpeedUnits;
  homing_timeout.value = s.homingTimeoutMs;
  edge_keepoff.value = s.edgeKeepoffCounts;
  park_offset.value = s.parkOffsetCounts;

  rail_len.value = s.railLengthCounts;
  park_pos.value = s.parkPos;
}
load();
</script>
</body></html>
)HTML";
