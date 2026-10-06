#pragma once

// The lamp's web page, served as-is by handleRoot(). Its JavaScript (bottom of
// this file) asks the board for /state once a second and updates the controls,
// so the page follows the knob and the remote. Moving a control sends a POST to
// /set; the Learn buttons send a POST to /learn. The colours and layout come
// from /style.css (see eltech_wifi.h).
const char PAGE_TEMPLATE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ElTech-Online Mood Lamp</title>
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <h1>ElTech-Online</h1>
  <div class="sub">ESP32 Mood Lamp &mdash; stays in step with the knob and the remote</div>
  <div class="panel">
    <div class="head"><h2>Lamp</h2><span class="badge" id="power">--</span></div>
    <div class="buttons"><button id="onoff" onclick="set('on=' + (state.on ? 0 : 1))">On / Off</button></div>
    <div class="row"><span class="name">Colour</span></div>
    <input type="color" id="color" onchange="set('color=' + encodeURIComponent(this.value))">
    <div class="row"><span class="name">Brightness</span><span id="brightText">--</span></div>
    <input type="range" id="bright" min="2" max="100" onchange="set('bright=' + this.value)">
    <div class="row"><span class="name">Effect</span></div>
    <div class="buttons" id="effects"></div>
  </div>
  <div class="panel">
    <div class="head"><h2>Teach it your remote</h2><span class="badge" id="learnstate">--</span></div>
    <ol class="hint">
      <li>Press <b>Learn</b> next to an action.</li>
      <li>Point any TV or media remote at the receiver and press the button you want to use.</li>
    </ol>
    <div id="actions"></div>
    <div class="row"><span class="name">Last code received</span><span id="lastir">--</span></div>
    <div class="buttons"><button class="quiet" onclick="learn('forget=1')">Forget all buttons</button></div>
    <div class="msg">Nothing happens when you press a button? The remote may not use the NEC code. Try another one.</div>
  </div>
  <div class="footer">ESP32 + WS2812B ring + rotary encoder + IR receiver &middot; <a href="https://github.com/eltech-online/eltech-esp32-mood-lamp" target="_blank">github.com/eltech-online/eltech-esp32-mood-lamp</a></div>
  <script>
    const el = (id) => document.getElementById(id);
    let state = {};
    // Puts the board's answer on the page.
    function show(d) {
      state = d;
      el('power').textContent = d.on ? 'ON' : 'OFF';
      el('power').className = 'badge ' + (d.on ? 'ok' : 'warn');
      // Don't move a control the user is touching right now.
      if (document.activeElement !== el('color'))  el('color').value = d.color;
      if (document.activeElement !== el('bright')) el('bright').value = d.bright;
      el('brightText').textContent = d.bright + ' %';
      const effects = el('effects');
      effects.textContent = '';
      d.effects.forEach((name, i) => {
        const b = document.createElement('button');
        b.textContent = name;
        b.className = i === d.effect ? 'on' : 'quiet';
        b.onclick = () => set('effect=' + i);
        effects.append(b);
      });
      const actions = el('actions');
      actions.textContent = '';
      d.ir.forEach(([name, learned], i) => {
        const row = document.createElement('div');
        row.className = 'row';
        const label = document.createElement('span');
        label.className = 'name';
        label.textContent = name + (learned ? ' (learned)' : '');
        const b = document.createElement('button');
        b.textContent = d.learning === i ? 'Waiting... cancel' : 'Learn';
        b.className = d.learning === i ? 'on' : 'quiet';
        b.onclick = () => learn('action=' + (d.learning === i ? -1 : i));
        row.append(label, b);
        actions.append(row);
      });
      el('learnstate').textContent = d.learning >= 0 ? 'PRESS A BUTTON' : 'READY';
      el('learnstate').className = 'badge ' + (d.learning >= 0 ? 'warn' : 'ok');
      el('lastir').textContent = d.lastir;
    }
    async function post(path, query) {
      try {
        const r = await fetch(path + '?' + query, { method: 'POST' });
        show(await r.json());
      } catch (e) { /* board busy or out of range: the next refresh catches up */ }
    }
    const set = (query) => post('/set', query);
    const learn = (query) => post('/learn', query);
    async function refresh() {
      try { show(await (await fetch('/state')).json()); } catch (e) {}
    }
    refresh();
    setInterval(refresh, 1000);
  </script>
</body>
</html>
)HTML";
