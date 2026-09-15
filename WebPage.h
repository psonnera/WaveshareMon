/*
  WebPage.h - the setup page served by WebSetup.cpp (single file, no external assets)
  (part of WaveshareMon, GPL v3, see LICENSE)

  Talks to /api/info, /api/config (GET/POST JSON, the BLE setup JSON), /api/cmd?c=,
  /api/scan and /api/log. Thresholds travel in mg/dL and are shown in the chosen units.

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef WEBPAGE_H
#define WEBPAGE_H

#include <pgmspace.h>

static const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>WaveshareMon setup</title>
<style>
body{font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;margin:0;background:#f3f3f0;color:#222}
header{background:#1d3b57;color:#fff;padding:12px 16px}header h1{margin:0;font-size:20px}
#st{font-size:13px;opacity:.9;white-space:pre-line;margin-top:4px}
main{max-width:640px;margin:0 auto;padding:12px 16px 40px}
section{background:#fff;border-radius:10px;padding:12px 16px;margin-bottom:12px;box-shadow:0 1px 3px rgba(0,0,0,.08)}
h2{font-size:16px;margin:0 0 6px}
label{display:block;margin:8px 0 2px;font-size:13px;color:#555}
input,select{width:100%;box-sizing:border-box;padding:8px;border:1px solid #bbb;border-radius:6px;font-size:15px;background:#fff}
.row{display:flex;gap:8px;align-items:flex-end}.row>*{flex:1}.row>button{flex:0 0 auto}
.chk{display:flex;align-items:center;gap:8px;margin:8px 0}.chk input{width:auto;margin:0}.chk label{margin:0;color:#222;font-size:15px}
button{background:#1d3b57;color:#fff;border:0;border-radius:6px;padding:9px 14px;font-size:15px;margin:6px 6px 0 0;cursor:pointer}
button.sec{background:#e4e8ee;color:#1d3b57}button.warn{background:#a33}
#msg{margin:10px 0;font-weight:600;min-height:1.4em}.hint{font-size:13px;color:#666;margin:4px 0}
pre{background:#222;color:#ddd;font-size:12px;padding:8px;border-radius:6px;max-height:240px;overflow:auto;white-space:pre-wrap;margin:0}
.hide{display:none}
</style></head><body>
<header><h1 id="nm">WaveshareMon</h1><div id="st">reading the device&hellip;</div></header>
<main>
<section><h2>Data source</h2>
<select id="src">
<option value="0">xDrip / AAPS through the phone (Bluetooth)</option>
<option value="1">Nightscout (Wi-Fi)</option>
<option value="2">xDrip Mi Band (Bluetooth)</option>
<option value="3">Dexcom Share (Wi-Fi)</option>
<option value="4">LibreLinkUp (Wi-Fi)</option>
<option value="5">xDrip4iOS (Bluetooth, no app needed)</option>
</select>
<div class="hint" id="srchint"></div>
<div id="g-wifi">
<label>Wi-Fi network (2.4 GHz only)</label>
<div class="row"><input id="ssid" list="nets" maxlength="32" autocomplete="off"><button class="sec" type="button" id="bscan">Scan</button></div>
<datalist id="nets"></datalist>
<label>Wi-Fi password</label><input id="pass" type="password" maxlength="63"><div class="hint" id="passhint"></div>
</div>
<div id="g-ns">
<label>Nightscout URL</label><input id="url" maxlength="127" placeholder="https://mysite.example.com">
<label>Access token</label><input id="token" type="password" maxlength="63"><div class="hint" id="tokenhint"></div>
</div>
<div id="g-dx">
<label>Dexcom account (the sensor user's own login)</label><input id="dxuser" maxlength="64">
<label>Dexcom password</label><input id="dxpass" type="password" maxlength="63"><div class="hint" id="dxpasshint"></div>
<label>Dexcom region</label><select id="dxreg"><option value="0">USA</option><option value="1">Outside the USA</option><option value="2">Japan</option></select>
</div>
<div id="g-ll">
<label>LibreLinkUp follower account (e-mail)</label><input id="lluser" maxlength="64">
<label>LibreLinkUp password</label><input id="llpass" type="password" maxlength="63"><div class="hint" id="llpasshint"></div>
<div class="row"><div><label>Region (empty = automatic)</label><input id="llreg" maxlength="7" placeholder="eu, us, de&hellip;"></div>
<div><label>App version to announce</label><input id="llver" maxlength="11"></div></div>
</div>
<div id="g-tls" class="chk"><input type="checkbox" id="tlsv"><label for="tlsv">Verify TLS certificates (switch off only as a last resort)</label></div>
<div id="g-obb" class="chk"><input type="checkbox" id="sline"><label for="sline">Show xDrip's status line on the bottom bar</label></div>
</section>

<section><h2>Display</h2>
<label>Units</label><select id="units"><option value="0">mg/dL</option><option value="1">mmol/L</option></select>
<div class="row"><div><label>Yellow below</label><input id="ylo" inputmode="decimal"></div><div><label>Yellow above</label><input id="yhi" inputmode="decimal"></div></div>
<div class="row"><div><label>Red below</label><input id="rlo" inputmode="decimal"></div><div><label>Red above</label><input id="rhi" inputmode="decimal"></div></div>
<div class="chk"><input type="checkbox" id="t24"><label for="t24">24-hour clock</label></div>
<div class="chk"><input type="checkbox" id="dmy"><label for="dmy">Day.month date format</label></div>
</section>

<section><h2>Alarms</h2>
<div class="chk"><input type="checkbox" id="aen"><label for="aen">Alarms on</label></div>
<div class="row"><div><label>Warning below</label><input id="wlo" inputmode="decimal"></div><div><label>Alarm below</label><input id="alo" inputmode="decimal"></div></div>
<div class="row"><div><label>Warning above</label><input id="whi" inputmode="decimal"></div><div><label>Alarm above</label><input id="ahi" inputmode="decimal"></div></div>
<label>No readings warning after (minutes)</label><input id="nor" type="number" min="5" max="1440">
<div class="row"><div><label>Warning volume (0-100)</label><input id="wvol" type="number" min="0" max="100"></div><div><label>Alarm volume (0-100)</label><input id="avol" type="number" min="0" max="100"></div></div>
<div class="row"><div><label>Repeat every (minutes)</label><input id="arep" type="number" min="1" max="120"></div><div><label>Snooze (minutes)</label><input id="snoz" type="number" min="1" max="240"></div></div>
</section>

<section><h2>Device</h2>
<label>Device name (empty = WaveshareMon-XXXX)</label><input id="name" maxlength="24">
<label>Time zone (POSIX TZ string)</label><input id="tz" list="tzs" maxlength="47">
<datalist id="tzs">
<option value="CET-1CEST,M3.5.0,M10.5.0/3">Central Europe</option>
<option value="GMT0BST,M3.5.0/1,M10.5.0">United Kingdom / Ireland</option>
<option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Eastern Europe</option>
<option value="EST5EDT,M3.2.0,M11.1.0">US Eastern</option>
<option value="CST6CDT,M3.2.0,M11.1.0">US Central</option>
<option value="MST7MDT,M3.2.0,M11.1.0">US Mountain</option>
<option value="PST8PDT,M3.2.0,M11.1.0">US Pacific</option>
<option value="AEST-10AEDT,M10.1.0,M4.1.0/3">Australia East</option>
<option value="NZST-12NZDT,M9.5.0,M4.1.0/3">New Zealand</option>
<option value="JST-9">Japan</option>
</datalist>
</section>

<div id="msg"></div>
<button id="bsave">Save to device</button>

<section><h2>Commands</h2>
<button class="sec" data-c="refresh">Refresh display</button><button class="sec" data-c="snooze">Snooze</button>
<button class="sec" data-c="testwarn">Test warning</button><button class="sec" data-c="testalarm">Test alarm</button>
<button class="sec" data-c="x4iforget" data-q="Forget the xDrip4iOS password? Remove the device in xDrip4iOS and add it again afterwards.">Reset xDrip4iOS password</button>
<button class="sec" data-c="update" data-q="Download the latest firmware from the GitHub repository over Wi-Fi and restart the device?">Update firmware</button>
<button class="sec" data-c="reboot" data-q="Restart the device?">Reboot</button>
<button class="warn" data-c="factory" data-q="Erase the configuration, the Bluetooth pairing and the Mi Band key?">Factory reset</button>
<button class="warn" data-c="setupoff" data-q="End setup mode? This page and the device's Wi-Fi network go away until the next setup mode.">Setup mode off</button>
</section>

<section><h2>Device log</h2><pre id="log"></pre></section>
</main>
<script>
const $=id=>document.getElementById(id);
const KEYS=['src','ssid','pass','url','token','dxuser','dxpass','dxreg','lluser','llpass','llreg','llver','tlsv','sline','units','ylo','yhi','rlo','rhi','t24','dmy','aen','wlo','alo','whi','ahi','nor','wvol','avol','arep','snoz','name','tz'];
const G=['ylo','yhi','rlo','rhi','wlo','alo','whi','ahi'];
const INTS=['src','dxreg','units','nor','wvol','avol','arep','snoz'];
const SECRET={pass:'haspass',token:'hastoken',dxpass:'hasdxpass',llpass:'hasllpass'};
const HINT=['The phone runs the WaveShareMon Android app whose Bluetooth bridge relays xDrip or AndroidAPS readings. This page cannot pair the phone: use the app for this source.',
 'The device polls your Nightscout site every 5 minutes. Enter the site address and an access token (a subject with the readable role).',
 'The device poses as a Mi Band 2. In xDrip enable the Mi Band support with this device\'s Bluetooth address (see the Info below and the wiki); no app needed.',
 'The sensor user\'s own Dexcom account; the Dexcom app must have at least one follower.',
 'A follower account invited from the patient\'s LibreLink app. Best effort: Abbott may retire this API.',
 'In xDrip4iOS add a Bluetooth device of type M5Stack: the app finds "M5Stack " + this device\'s name, pairs by itself and pushes every reading, the time and the units. Reset the password below to pair another iPhone.'];
let cfg=null,curMm=false;
const isWifi=s=>s==1||s==3||s==4;
function showGroups(){const s=+$('src').value;
 $('g-wifi').classList.toggle('hide',!isWifi(s));$('g-ns').classList.toggle('hide',s!=1);$('g-dx').classList.toggle('hide',s!=3);
 $('g-ll').classList.toggle('hide',s!=4);$('g-tls').classList.toggle('hide',!isWifi(s));$('g-obb').classList.toggle('hide',s!=0);
 $('srchint').textContent=HINT[s]||'';}
const g2ui=(mm,v)=>mm?(v/18).toFixed(1):String(v);
const ui2g=(mm,v)=>mm?Math.round(parseFloat(v)*18):parseInt(v);
function fill(c){cfg=c;curMm=c.units==1;
 for(const k of KEYS){const e=$(k);if(!e||c[k]===undefined)continue;
  if(e.type=='checkbox')e.checked=!!c[k];else if(G.includes(k))e.value=g2ui(curMm,c[k]);else e.value=c[k];}
 for(const k in SECRET){$(k).value='';$(k+'hint').textContent=c[SECRET[k]]?'A '+(k=='token'?'token':'password')+' is stored: leave empty to keep it.':'';}
 showGroups();}
function collect(){const o={};const mm=$('units').value==1;
 for(const k of KEYS){const e=$(k);if(!e)continue;let v;
  if(e.type=='checkbox')v=e.checked?1:0;else if(G.includes(k))v=ui2g(mm,e.value);else if(INTS.includes(k))v=parseInt(e.value);else v=e.value.trim();
  if(k in SECRET){if(v!=='')o[k]=v;continue;}
  if(Number.isNaN(v))continue;
  if(cfg[k]!==v)o[k]=v;}
 return o;}
$('units').onchange=()=>{const mm=$('units').value==1;if(mm==curMm)return;
 for(const k of G){const g=ui2g(curMm,$(k).value);if(!Number.isNaN(g))$(k).value=g2ui(mm,g);}curMm=mm;};
$('src').onchange=showGroups;
async function loadConfig(){try{fill(await (await fetch('/api/config')).json());}catch(e){$('msg').textContent='Could not read the configuration: '+e;}}
async function loadInfo(){try{const i=await (await fetch('/api/info')).json();$('nm').textContent=i.name+' setup';
 let s='Firmware '+i.fw+(i.build?' build '+i.build:'')+' · '+(i.bat>=0?'battery '+i.bat+' %':'on USB')+'\n'+(i.stat||'');
 s+='\nWi-Fi: '+i.wifi+(i.ip?' '+i.ip:'')+(i.wifierr?' – '+i.wifierr:'');
 if(i.mac)s+='\nBluetooth address '+i.mac;if(i.src==5)s+='\nxDrip4iOS: '+i.x4i+(i.x4ipw?' \u00b7 password '+i.x4ipw:' \u00b7 no password yet');if(i.ota)s+='\nFirmware update: '+i.ota;$('st').textContent=s;}catch(e){}}
async function loadLog(){try{const t=await (await fetch('/api/log')).text();const p=$('log');const b=p.scrollTop+p.clientHeight>=p.scrollHeight-4;p.textContent=t;if(b)p.scrollTop=p.scrollHeight;}catch(e){}}
$('bsave').onclick=async()=>{const o=collect();if(!Object.keys(o).length){$('msg').textContent='Nothing changed.';return;}
 if(isWifi(o.src??cfg.src)&&!(o.ssid??cfg.ssid)){$('msg').textContent='Enter the Wi-Fi network name.';return;}
 o.now=Math.floor(Date.now()/1000);$('msg').textContent='Saving…';
 try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});
  $('msg').textContent=r.ok?'Saved. The device applies the settings now; watch the Wi-Fi state at the top.':'The device refused the settings ('+r.status+').';
  setTimeout(loadConfig,1500);setTimeout(loadInfo,3000);}catch(e){$('msg').textContent='Save failed: '+e;}};
let scanTries=0;
async function pollScan(){try{const j=await (await fetch('/api/scan')).json();
 if(j.scan=='busy'&&scanTries++<8){setTimeout(pollScan,2000);return;}
 const d=$('nets');d.innerHTML='';for(const n of (j.nets||[])){const o=document.createElement('option');o.value=n.s;o.label=n.r+' dBm, ch '+n.c+(n.e?'':', open');d.appendChild(o);}
 $('bscan').textContent='Scan';$('msg').textContent=(j.nets||[]).length?'Networks the device sees are listed under the Wi-Fi field.':'The device saw no network (it only has a 2.4 GHz radio).';}catch(e){$('bscan').textContent='Scan';}}
$('bscan').onclick=async()=>{$('bscan').textContent='Scanning…';scanTries=0;try{await fetch('/api/scan?start=1');}catch(e){}setTimeout(pollScan,2500);};
document.querySelectorAll('button[data-c]').forEach(b=>b.onclick=async()=>{if(b.dataset.q&&!confirm(b.dataset.q))return;
 try{const r=await fetch('/api/cmd?c='+b.dataset.c,{method:'POST'});$('msg').textContent=r.ok?'Command sent: '+b.textContent:'Command refused.';}catch(e){$('msg').textContent='Command failed: '+e;}});
loadConfig();loadInfo();loadLog();setInterval(loadInfo,5000);setInterval(loadLog,5000);
</script></body></html>
)HTML";

#endif
