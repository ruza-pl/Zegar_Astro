#pragma once
#include <Arduino.h>

const char htmlTemplate[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>Zegar Astronomiczny</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet/dist/leaflet.css"/>
<script src="https://unpkg.com/leaflet/dist/leaflet.js" onerror="window._noLeaflet=true"></script>
<style>
:root{
  --bg:#070b11;--s1:#0d1320;--s2:#111a2a;--s3:#162033;
  --bd:rgba(255,255,255,0.07);--bd2:rgba(255,255,255,0.13);
  --t:#dde4f0;--tm:#5a6a87;--td:#2e3d52;
  --amber:#f5a828;--amber-bg:rgba(245,168,40,0.10);--amber-bd:rgba(245,168,40,0.22);
  --blue:#4a9eff;--blue-bg:rgba(74,158,255,0.09);--blue-bd:rgba(74,158,255,0.22);
  --red:#f06060;--red-bg:rgba(240,96,96,0.09);--red-bd:rgba(240,96,96,0.22);
  --green:#3ecf8e;--green-bg:rgba(62,207,142,0.09);--green-bd:rgba(62,207,142,0.22);
  --r6:6px;--r10:10px;--r14:14px;
  --mono:ui-monospace,'Cascadia Code','SF Mono',Consolas,monospace;
  --sans:-apple-system,BlinkMacSystemFont,'Segoe UI Variable','Segoe UI',system-ui,sans-serif;
}
*{box-sizing:border-box;margin:0;padding:0;}
body{font-family:var(--sans);background:var(--bg);color:var(--t);min-height:100vh;font-size:15px;line-height:1.6;}
a{color:inherit;text-decoration:none;}

/* ── Header ── */
.hdr{display:flex;align-items:center;justify-content:space-between;padding:14px 22px;background:var(--s1);border-bottom:1px solid var(--bd);position:sticky;top:0;z-index:200;}
.hdr-brand{display:flex;align-items:center;gap:10px;}
.sun-icon{width:30px;height:30px;background:var(--amber);border-radius:50%;display:flex;align-items:center;justify-content:center;font-size:15px;flex-shrink:0;box-shadow:0 0 16px var(--amber-bg);}
.hdr h1{font-size:15px;font-weight:600;letter-spacing:.02em;}
.ver-pill{font-family:var(--mono);font-size:9px;color:var(--tm);background:var(--s2);border:1px solid var(--bd);padding:3px 8px;border-radius:20px;white-space:nowrap;}

/* ── Layout ── */
.wrap{max-width:700px;margin:0 auto;padding:24px 16px 60px;}

/* ── Alert ── */
.alert{padding:14px 16px;border-radius:var(--r10);margin-bottom:18px;font-size:13px;line-height:1.6;}
.alert-warning{background:var(--amber-bg);border:1px solid var(--amber-bd);color:#f5c842;}
.alert-warning strong{color:var(--amber);}
.alert button{margin-top:10px;padding:7px 14px;background:var(--green);color:#000;border:none;border-radius:var(--r6);cursor:pointer;font-size:12px;font-weight:600;font-family:var(--sans);letter-spacing:.01em;}

/* ── Card ── */
.card{background:var(--s1);border:1px solid var(--bd);border-radius:var(--r14);padding:22px;margin-bottom:14px;}
.card-title{font-size:10px;font-weight:600;text-transform:uppercase;letter-spacing:.12em;color:var(--tm);margin-bottom:18px;}

/* ── Status rows ── */
.srow{display:flex;align-items:center;justify-content:space-between;padding:10px 0;border-bottom:1px solid var(--bd);}
.srow:last-of-type{border-bottom:none;}
.srow-lbl{font-size:13px;color:var(--tm);}

/* ── Badge ── */
.badge{font-family:var(--mono);font-size:11px;font-weight:500;padding:4px 11px;border-radius:20px;display:inline-block;}
.badge-auto{background:var(--blue-bg);color:var(--blue);border:1px solid var(--blue-bd);}
.badge-on{background:var(--green-bg);color:var(--green);border:1px solid var(--green-bd);}
.badge-off{background:var(--red-bg);color:var(--red);border:1px solid var(--red-bd);}
.badge-warning{background:var(--amber-bg);color:var(--amber);border:1px solid var(--amber-bd);}

/* ── Pin status ── */
.pin-pill{display:inline-flex;align-items:center;gap:6px;font-family:var(--mono);font-size:12px;color:var(--tm);background:var(--s2);padding:5px 12px;border-radius:20px;border:1px solid var(--bd);}
.pin-dot{width:6px;height:6px;border-radius:50%;background:var(--td);flex-shrink:0;}
.pin-dot.on{background:var(--green);box-shadow:0 0 7px var(--green);}

/* ── Time boxes ── */
.time-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:18px 0;}
.time-box{background:var(--s2);border:1px solid var(--bd);border-radius:var(--r10);padding:14px 16px;}
.time-box-lbl{font-size:10px;text-transform:uppercase;letter-spacing:.1em;color:var(--tm);margin-bottom:6px;}
.time-val{font-family:var(--mono);font-size:22px;font-weight:500;}
.time-val.on{color:var(--green);}
.time-val.off{color:var(--red);}

/* ── Buttons ── */
.btn-row{display:flex;gap:8px;flex-wrap:wrap;margin-top:18px;justify-content:center;}
.btn{padding:9px 17px;border:1px solid var(--bd);border-radius:var(--r6);cursor:pointer;font-family:var(--sans);font-weight:500;font-size:13px;display:inline-flex;align-items:center;justify-content:center;gap:6px;transition:background .15s,border-color .15s;background:var(--s2);color:var(--t);}
.btn:hover{border-color:var(--bd2);background:var(--s3);}
.btn-on{background:var(--green-bg);color:var(--green);border-color:var(--green-bd);}
.btn-on:hover{background:rgba(62,207,142,0.16);}
.btn-off{background:var(--red-bg);color:var(--red);border-color:var(--red-bd);}
.btn-off:hover{background:rgba(240,96,96,0.16);}
.btn-auto{background:var(--blue-bg);color:var(--blue);border-color:var(--blue-bd);}
.btn-auto:hover{background:rgba(74,158,255,0.16);}
.btn-save{background:var(--amber);color:#000;border-color:var(--amber);font-weight:600;width:100%;padding:12px;font-size:14px;margin-top:18px;}
.btn-save:hover{background:#e09a20;}
.btn-warn{background:var(--amber-bg);color:var(--amber);border-color:var(--amber-bd);}
.btn-warn:hover{background:rgba(245,168,40,0.18);}
.btn-flex{flex:1;min-width:140px;}

/* ── Map ── */
#map{height:250px;border-radius:var(--r10);overflow:hidden;border:1px solid var(--bd);margin-bottom:18px;}

/* ── Form ── */
.form-row{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:12px;}
.fg{display:flex;flex-direction:column;gap:5px;}
.fg label{font-size:10px;font-weight:600;text-transform:uppercase;letter-spacing:.1em;color:var(--tm);}
input[type="number"],input[type="text"]{background:var(--s2);border:1px solid var(--bd);border-radius:var(--r6);padding:10px 12px;color:var(--t);font-family:var(--mono);font-size:13px;width:100%;transition:border-color .2s;}
input[type="number"]:focus,input[type="text"]:focus{outline:none;border-color:rgba(74,158,255,.4);}
input[type="text"][readonly]{color:var(--tm);cursor:default;}
.inv-row{display:flex;align-items:center;gap:10px;padding:14px 0;border-top:1px solid var(--bd);margin-top:6px;}
.inv-row span{font-size:13px;color:var(--t);}
input[type="checkbox"]{width:17px;height:17px;accent-color:var(--amber);cursor:pointer;flex-shrink:0;}

/* ── Footer ── */
footer{border-top:1px solid var(--bd);padding:22px 16px;text-align:center;color:var(--td);font-size:12px;}

@media(max-width:480px){
  .form-row{grid-template-columns:1fr;}
  .time-val{font-size:18px;}
  .hdr h1{font-size:14px;}
}
</style>
</head>
<body>
<header class="hdr">
  <div class="hdr-brand">
    <div class="sun-icon">&#9728;</div>
    <h1>Zegar Astronomiczny</h1>
  </div>
  <span class="ver-pill">{{VERSION}}</span>
</header>

<div class="wrap">
  {{TIME_STATUS_MSG}}

  <div class="card">
    <div class="card-title">Status systemu</div>
    <div class="srow">
      <span class="srow-lbl">Tryb pracy</span>
      <span class="badge {{BADGE_CLASS}}">{{MODE_TEXT}}</span>
    </div>
    <div class="srow">
      <span class="srow-lbl">Stan wyjścia</span>
      <span class="pin-pill"><span class="pin-dot" id="pd"></span><span id="pst">{{PIN_STATE}}</span></span>
    </div>
    <div class="time-grid">
      <div class="time-box">
        <div class="time-box-lbl">Włączenie</div>
        <div class="time-val on">{{ON_TIME}}</div>
      </div>
      <div class="time-box">
        <div class="time-box-lbl">Wyłączenie</div>
        <div class="time-val off">{{OFF_TIME}}</div>
      </div>
    </div>
    <div class="btn-row">
      <a href="/api/on" class="btn btn-on">Wymuś ON</a>
      <a href="/api/off" class="btn btn-off">Wymuś OFF</a>
      <a href="/api/auto" class="btn btn-auto">Tryb AUTO</a>
    </div>
  </div>

  <div class="card">
    <div class="card-title">Lokalizacja i offsety</div>
    <div id="map"></div>
    <div id="map-off" style="display:none;height:60px;border:1px solid var(--bd);border-radius:var(--r10);background:var(--s2);display:none;align-items:center;justify-content:center;gap:8px;margin-bottom:18px;font-size:13px;color:var(--tm);">
      <span>&#9651;</span> Mapa niedostępna offline &mdash; wpisz współrzędne ręcznie
    </div>
    <form action="/save" method="POST">
      <div class="form-row">
        <div class="fg"><label>Szerokość (Lat)</label><input type="text" id="lat" name="lat" value="{{LAT}}" readonly></div>
        <div class="fg"><label>Długość (Lng)</label><input type="text" id="lng" name="lng" value="{{LNG}}" readonly></div>
      </div>
      <div class="form-row">
        <div class="fg"><label>Offset wschód (min)</label><input type="number" name="osr" value="{{OSR}}"></div>
        <div class="fg"><label>Offset zachód (min)</label><input type="number" name="oss" value="{{OSS}}"></div>
      </div>
      <div class="inv-row">
        <input type="checkbox" name="inv" id="inv" {{INV}}>
        <label for="inv"><span>Inwersja &mdash; praca w dzień zamiast w nocy</span></label>
      </div>
      <input type="submit" value="Zapisz ustawienia" class="btn btn-save">
    </form>
  </div>

  <div class="card">
    <div class="card-title">Zarządzanie urządzeniem</div>
    <div class="btn-row">
      <a href="/api/resetwifi" class="btn btn-warn btn-flex">Zmień sieć Wi&#8209;Fi</a>
      <a href="/update" class="btn btn-flex">Aktualizacja OTA</a>
    </div>
  </div>
</div>

<footer>&copy; 2026 Piotr Rużański</footer>

<script>
if(document.getElementById('pst').textContent.indexOf('WYSOKI')>=0)
  document.getElementById('pd').classList.add('on');

function upd(l){document.getElementById('lat').value=l.lat.toFixed(5);document.getElementById('lng').value=l.lng.toFixed(5);}

var lat={{LAT}},lng={{LNG}};
if(typeof L!=='undefined'&&!window._noLeaflet){
  var map=L.map('map').setView([lat,lng],6);
  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{
    attribution:'&copy; OpenStreetMap contributors',maxZoom:19
  }).addTo(map);
  var mk=L.marker([lat,lng],{draggable:true}).addTo(map);
  mk.on('dragend',function(){upd(mk.getLatLng());});
  map.on('click',function(e){mk.setLatLng(e.latlng);upd(e.latlng);});
}else{
  document.getElementById('map').style.display='none';
  var mo=document.getElementById('map-off');mo.style.display='flex';
  document.getElementById('lat').readOnly=false;
  document.getElementById('lng').readOnly=false;
  document.getElementById('lat').style.color='var(--t)';
  document.getElementById('lng').style.color='var(--t)';
}
</script>
</body>
</html>
)rawliteral";


const char otaHtmlTemplate[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>Aktualizacja OTA</title>
<style>
:root{
  --bg:#070b11;--s1:#0d1320;--s2:#111a2a;
  --bd:rgba(255,255,255,0.07);--t:#dde4f0;--tm:#5a6a87;
  --amber:#f5a828;--green:#3ecf8e;--red:#f06060;
  --mono:ui-monospace,'Cascadia Code','SF Mono',Consolas,monospace;--sans:-apple-system,BlinkMacSystemFont,'Segoe UI Variable','Segoe UI',system-ui,sans-serif;
}
*{box-sizing:border-box;margin:0;padding:0;}
body{font-family:var(--sans);background:var(--bg);color:var(--t);min-height:100vh;display:flex;flex-direction:column;}
.hdr{display:flex;align-items:center;justify-content:space-between;padding:14px 22px;background:var(--s1);border-bottom:1px solid var(--bd);}
.hdr-brand{display:flex;align-items:center;gap:10px;}
.sun-icon{width:30px;height:30px;background:var(--amber);border-radius:50%;display:flex;align-items:center;justify-content:center;font-size:15px;}
.hdr h1{font-size:15px;font-weight:600;}
.ver-pill{font-family:var(--mono);font-size:9px;color:var(--tm);background:var(--s2);border:1px solid var(--bd);padding:3px 8px;border-radius:20px;white-space:nowrap;}
.wrap{max-width:500px;margin:0 auto;padding:40px 16px;flex:1;width:100%;}
.card{background:var(--s1);border:1px solid var(--bd);border-radius:14px;padding:30px;text-align:center;}
.card-title{font-size:10px;font-weight:600;text-transform:uppercase;letter-spacing:.12em;color:var(--tm);margin-bottom:6px;}
.card h2{font-size:20px;font-weight:600;margin-bottom:24px;}
input[type="file"]{background:var(--s2);border:1px solid var(--bd);border-radius:8px;padding:12px;color:var(--t);font-size:13px;width:100%;cursor:pointer;margin-bottom:16px;font-family:var(--sans);}
.btn-up{width:100%;padding:13px;background:var(--amber);color:#000;border:none;border-radius:8px;cursor:pointer;font-family:var(--sans);font-weight:600;font-size:15px;transition:background .15s;}
.btn-up:hover{background:#e09a20;}
.btn-up:disabled{background:#3a4a5a;color:var(--tm);cursor:not-allowed;}
.progress-wrap{display:none;margin-top:22px;}
.progress-track{height:6px;background:var(--s2);border-radius:3px;overflow:hidden;border:1px solid var(--bd);}
.progress-bar{height:100%;width:0%;background:var(--amber);transition:width .2s;border-radius:3px;}
#status{margin-top:14px;font-family:var(--mono);font-size:22px;font-weight:500;color:var(--amber);}
.back{display:inline-block;margin-top:24px;color:var(--tm);font-size:13px;text-decoration:none;}
.back:hover{color:var(--t);}
footer{border-top:1px solid var(--bd);padding:20px;text-align:center;color:var(--tm);font-size:12px;font-family:var(--mono);}
</style>
</head>
<body>
<header class="hdr">
  <div class="hdr-brand">
    <div class="sun-icon">&#9728;</div>
    <h1>Zegar Astronomiczny</h1>
  </div>
  <span class="ver-pill">{{VERSION}}</span>
</header>

<div class="wrap">
  <div class="card">
    <div class="card-title">Firmware update</div>
    <h2>Wgraj nowy plik .bin</h2>
    <form id="uf">
      <input type="file" id="file" name="update" accept=".bin" required>
      <button type="submit" id="sbtn" class="btn-up">Aktualizuj system</button>
    </form>
    <div class="progress-wrap" id="pw">
      <div class="progress-track"><div class="progress-bar" id="pb"></div></div>
      <div id="status">0%</div>
    </div>
    <a href="/" class="back">&#8592; Powrót do strony głównej</a>
  </div>
</div>

<footer>Obecna wersja: {{VERSION}}</footer>

<script>
document.getElementById('uf').onsubmit=function(e){
  e.preventDefault();
  var f=document.getElementById('file').files[0];
  if(!f)return;
  var btn=document.getElementById('sbtn');
  var st=document.getElementById('status');
  btn.disabled=true;btn.textContent='Wgrywanie\u2026';
  document.getElementById('pw').style.display='block';
  var xhr=new XMLHttpRequest();
  xhr.upload.addEventListener('progress',function(e){
    if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);document.getElementById('pb').style.width=p+'%';st.textContent=p+'%';}
  });
  xhr.onreadystatechange=function(){
    if(xhr.readyState==4){
      if(xhr.status==200){st.style.color='#3ecf8e';st.textContent=xhr.responseText;setTimeout(function(){window.location.href='/';},4000);}
      else{st.style.color='#f06060';st.textContent='Błąd aktualizacji!';}
    }
  };
  xhr.open('POST','/update',true);
  var fd=new FormData();fd.append('update',f);xhr.send(fd);
};
</script>
</body>
</html>
)rawliteral";