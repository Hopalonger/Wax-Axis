#pragma once
#include <pgmspace.h>

static const char UI_UPDATE_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>Wax Axis - Firmware Update</title>
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <div class="wrap">
    <h1>WAX AXIS</h1>
    <div class="center small">
      <a href="/">Main</a> &nbsp;|&nbsp; <a href="/config">Config</a>
    </div>

    <hr>

    <h2>Firmware Update</h2>
    <div class="mid">
      <p class="small">
        Upload a compiled <b>.bin</b> file… device will reboot after success.
      </p>

      <form id="uploadForm">
        <div class="row">
          <input id="file" name="update" type="file" accept=".bin" required />
          <button class="btn mini" type="submit">Upload</button>
        </div>
      </form>

      <div style="margin-top:14px;">
        <progress id="prog" value="0" max="100"></progress>
        <div id="status" class="small" style="margin-top:8px;">Idle…</div>
      </div>

      <hr>

      <div class="small">
        Tips:
        <ul>
          <li>Arduino IDE: <b>Sketch → Export Compiled Binary</b></li>
          <li>PlatformIO: <code>.pio/build/&lt;env&gt;/firmware.bin</code></li>
        </ul>
      </div>
    </div>
  </div>

<script>
  const form = document.getElementById('uploadForm');
  const fileInput = document.getElementById('file');
  const prog = document.getElementById('prog');
  const statusEl = document.getElementById('status');

  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    if (!fileInput.files.length) return;

    const file = fileInput.files[0];
    const data = new FormData();
    data.append('update', file);

    statusEl.textContent = 'Uploading…';
    prog.value = 0;

    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/updatefw', true);

    xhr.upload.onprogress = (evt) => {
      if (evt.lengthComputable) {
        const p = Math.round((evt.loaded / evt.total) * 100);
        prog.value = p;
        statusEl.textContent = `Uploading… ${p}%`;
      }
    };

    xhr.onload = () => {
      if (xhr.status === 200) {
        statusEl.textContent = 'Update complete… rebooting.';
      } else {
        statusEl.textContent = `Update failed (HTTP ${xhr.status}): ${xhr.responseText}`;
      }
    };

    xhr.onerror = () => {
      statusEl.textContent = 'Upload error (connection dropped).';
    };

    xhr.send(data);
  });
</script>
</body>
</html>
)HTML";
