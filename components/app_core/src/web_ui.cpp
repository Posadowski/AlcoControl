#include "web_ui.hpp"

namespace alco::web {

namespace {

const char kHead[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AlcoControl</title>
<style>
body{font-family:Arial,sans-serif;margin:20px;max-width:720px}
nav a{display:inline-block;padding:10px 14px;margin:0 6px 6px 0;background:#007cba;color:#fff;text-decoration:none;border-radius:4px}
nav a.on{background:#28a745}
.card{margin:16px 0;padding:14px;border:1px solid #ddd;border-radius:6px}
button{padding:9px 14px;margin:4px;border:0;border-radius:4px;background:#007cba;color:#fff;cursor:pointer}
button.red{background:#dc3545}
input{padding:8px;margin:4px;border:1px solid #ccc;border-radius:4px;width:90%}
.net{padding:8px;margin:6px 0;border:1px solid #ddd;border-radius:4px}
.ok{background:#d4edda}
.bar{height:18px;background:#eee;border-radius:9px;overflow:hidden}
.bar>div{height:100%;background:#007cba;width:0}
</style></head><body>
<h1>AlcoControl</h1>
<nav><a href="/">Dashboard</a><a href="/wifi">WiFi</a><a href="/ota">OTA</a></nav>
<script>
const $=id=>document.getElementById(id);
document.querySelectorAll('nav a').forEach(a=>{if(a.getAttribute('href')===location.pathname)a.className='on'});
async function api(url,data){
 const o=data===undefined?{}:{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)};
 const r=await fetch(url,o);let j={};
 try{j=await r.json()}catch(e){}
 j._status=r.status;return j}
</script>
)HTML";

const char kTail[] = "</body></html>\n";

const char kDashboard[] = R"HTML(
<div class="card"><h2>Heater</h2>
<p>Running: <b id="run">-</b> &nbsp; Fault: <b id="fault">-</b></p>
<p>Temperature: <b id="temp">-</b> &nbsp; Target: <b id="tgt">-</b> &nbsp; Power: <b id="pow">-</b></p>
<button onclick="act('/api/heater/start')">Start</button>
<button class="red" onclick="act('/api/heater/stop')">Stop</button>
<p><input id="nt" type="number" step="0.5" placeholder="new target, C" style="width:150px">
<button onclick="setT()">Set target</button></p>
<p id="msg"></p></div>
<div class="card"><h2>Sensors</h2><ul id="sens"></ul></div>
<script>
const f=(v,u)=>(v===null||v===undefined)?'-':v.toFixed(2)+u;
async function act(u){await api(u,{});load()}
async function setT(){const r=await api('/api/heater/target',{value:$('nt').value});$('msg').textContent=r.error||'';load()}
async function load(){
 try{
  const s=await api('/api/heater/status');
  $('run').textContent=s.running?'YES':'NO';
  $('fault').textContent=s.fault;
  $('temp').textContent=f(s.temperature,' \u00b0C');
  $('tgt').textContent=f(s.target,' \u00b0C');
  $('pow').textContent=f(s.power,' %');
  const q=await api('/api/sensors');
  const ul=$('sens');ul.replaceChildren();
  for(const x of q.sensors||[]){const li=document.createElement('li');li.textContent=x.id+': '+f(x.value,' \u00b0C');ul.appendChild(li)}
 }catch(e){}
}
setInterval(load,2000);load();
</script>
)HTML";

const char kWifi[] = R"HTML(
<div class="card"><h2>WiFi</h2>
<p id="st"></p>
<button onclick="scan()">Scan networks</button>
<div id="nets"></div></div>
<script>
async function scan(){
 $('st').textContent='Scanning...';
 const r=await api('/api/wifi/scan');
 const box=$('nets');box.replaceChildren();
 $('st').textContent=r.networks?r.networks.length+' networks':(r.error||'scan failed');
 for(const n of r.networks||[]){
  const d=document.createElement('div');d.className='net'+(n.connected?' ok':'');
  const t=document.createElement('b');t.textContent=n.ssid||'(hidden)';d.appendChild(t);
  d.appendChild(document.createTextNode(' '+n.rssi+' dBm'+(n.secure?' (secured)':'')+(n.connected?' - connected':'')));
  if(!n.connected){const b=document.createElement('button');b.textContent='Connect';b.onclick=()=>connect(n);d.appendChild(b)}
  box.appendChild(d)}
}
async function connect(n){
 const pw=n.secure?prompt('Password for '+n.ssid):'';
 if(pw===null)return;
 const r=await api('/api/wifi/connect',{ssid:n.ssid,password:pw});
 $('st').textContent=r.message||r.error||'';
}
</script>
)HTML";

const char kOta[] = R"HTML(
<div class="card"><h2>Update server</h2>
<input id="url" type="url" placeholder="http://host/path">
<button onclick="save()">Save</button><p id="smsg"></p></div>
<div class="card"><h2>Firmware</h2>
<p>Status: <b id="stat">-</b></p>
<div class="bar"><div id="bar"></div></div>
<p><span id="pct">0</span> %</p>
<button onclick="chk()">Check</button>
<button class="red" onclick="upd()">Update (stops the heater)</button></div>
<script>
async function save(){const r=await api('/api/ota/server',{server_url:$('url').value});$('smsg').textContent=r.message||r.error||''}
async function chk(){$('stat').textContent='Checking...';const r=await api('/api/ota/check');$('stat').textContent=r.message||r.error||''}
async function upd(){
 if(!confirm('Stop the heater, update the firmware and restart?'))return;
 const r=await api('/api/ota/update',{});
 $('stat').textContent=r.message||r.error||'';
 if(r.success)poll();
}
function poll(){
 const t=setInterval(async()=>{
  try{const p=await api('/api/ota/progress');
   $('pct').textContent=p.progress;$('bar').style.width=p.progress+'%';$('stat').textContent=p.status;
   if(p.progress>=100||/failed/i.test(p.status))clearInterval(t)}catch(e){}
 },1000);
}
api('/api/ota/server').then(r=>{$('url').value=r.server_url||''});
</script>
)HTML";

std::string page(const char* body) {
    std::string out;
    out.reserve(sizeof(kHead) + 1400 + sizeof(kTail));
    out += kHead;
    out += body;
    out += kTail;
    return out;
}

} // namespace

std::string dashboardPage() { return page(kDashboard); }
std::string wifiPage()      { return page(kWifi); }
std::string otaPage()       { return page(kOta); }

} // namespace alco::web
