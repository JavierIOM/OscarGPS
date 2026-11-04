// The status page served at http://192.168.4.1 while the access point is up. It polls
// /api once a second and draws everything from that. Dark by default, with a light theme.
#pragma once

static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark light">
<meta name="robots" content="noindex">
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'%3E%3Ccircle cx='8' cy='8' r='6.5' fill='none' stroke='%23F56400' stroke-width='2'/%3E%3Ccircle cx='8' cy='8' r='2' fill='%23F56400'/%3E%3C/svg%3E">
<title>OscarGPS</title>
<style>
:root{--bg:#121212;--card:#1a1a1a;--tx:#e6e6e6;--mu:#9a9a9a;--bd:#2e2e2e;--ac:#F56400;--ok:#5fd38d;
--gp:#4ea1ff;--gl:#ff6b6b;--ga:#5fd38d;--bds:#e0b341}
[data-theme=light]{--bg:#FAF7F4;--card:#FFFFFF;--tx:#222222;--mu:#595959;--bd:#D9D9D9;--ac:#C24F00;--ok:#1e7a44;
--gp:#1f6fd1;--gl:#c62828;--ga:#1e8a4c;--bds:#9a6b00}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--tx);font:14px/1.45 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
header{display:flex;align-items:baseline;gap:10px;padding:12px 16px;border-bottom:1px solid var(--bd)}
h1{font-size:16px;margin:0;font-weight:700}
#t{margin-left:auto;background:none;border:1px solid var(--bd);color:var(--tx);font:inherit;padding:3px 10px;border-radius:4px;cursor:pointer}
#st{display:flex;flex-wrap:wrap;gap:4px 14px;padding:8px 16px;border-bottom:1px solid var(--bd)}
#st .lk{margin-left:auto}
main{display:grid;gap:12px;padding:12px 16px 24px;grid-template-columns:repeat(auto-fit,minmax(min(100%,320px),1fr));max-width:1120px;margin:0 auto}
section{background:var(--card);border:1px solid var(--bd);border-radius:6px;padding:12px;min-width:0}
.tw{overflow-x:auto}
h2{font-size:11px;text-transform:uppercase;letter-spacing:.09em;color:var(--mu);margin:0 0 8px;font-weight:600}
dl{display:grid;grid-template-columns:max-content 1fr;gap:3px 14px;margin:0}
dt,.mu{color:var(--mu)}
dd{margin:0;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}
.big{font-size:20px;letter-spacing:.04em}
.ok{color:var(--ok)}.warn{color:var(--ac)}
svg{width:100%;height:auto;display:block}
#sky{max-width:360px;margin:0 auto}
#leg{display:flex;flex-wrap:wrap;gap:4px 14px;font-size:12px;color:var(--mu);margin-top:8px}
#leg i{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:5px}
table{width:100%;border-collapse:collapse;font-variant-numeric:tabular-nums}
th,td{text-align:right;padding:2px 5px;border-bottom:1px solid var(--bd)}
th{color:var(--mu);font-weight:600}
th:first-child,td:first-child{text-align:left}
</style></head><body>
<header><h1>OscarGPS</h1><span class="mu" id="fw"></span><button id="t" type="button"></button></header>
<div id="st"><span class="mu">connecting to the board...</span></div>
<main>
<section><h2>Position</h2><dl id="pos"></dl></section>
<section><h2>Fix</h2><dl id="fix"></dl></section>
<section><h2>Sky</h2><svg id="sky" viewBox="-116 -116 232 232" role="img" aria-label="Sky plot"></svg><div id="leg"></div></section>
<section><h2>Signal</h2><svg id="snr" viewBox="0 0 300 130" role="img" aria-label="Signal strength per satellite"></svg></section>
<section><h2>Satellites</h2><div class="tw"><table><thead><tr><th>sys</th><th>prn</th><th>el</th><th>az</th><th>snr</th><th>in fix</th></tr></thead><tbody id="tb"></tbody></table></div></section>
<section><h2>Link to the PC</h2><dl id="lnk"></dl></section>
<section><h2>Board</h2><dl id="brd"></dl></section>
</main>
<script>
const $=i=>document.getElementById(i);
const SYS={P:['GPS','GPS','--gp'],L:['GLONASS','GLO','--gl'],A:['Galileo','GAL','--ga'],B:['BeiDou','BDS','--bds']};
const sys=k=>SYS[k]||['GNSS','GNS','--mu'];
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
let th='dark';try{th=localStorage.getItem('th')||'dark'}catch(e){}
function setTh(t){th=t;document.documentElement.dataset.theme=t;$('t').textContent=t=='dark'?'light':'dark';try{localStorage.setItem('th',t)}catch(e){}}
setTh(th);$('t').onclick=()=>setTh(th=='dark'?'light':'dark');
$('leg').innerHTML=Object.values(SYS).map(v=>`<span><i style="background:var(${v[2]})"></i>${v[0]}</span>`).join('');
function dl(id,rows){$(id).innerHTML=rows.map(r=>`<dt>${r[0]}</dt><dd${r[2]?` class="${r[2]}"`:''}>${r[1]}</dd>`).join('')}
function deg(v,p,n){const a=Math.abs(v),d=Math.floor(a);return `${a.toFixed(6)}&deg; ${v<0?n:p} <span class="mu">${d}&deg; ${((a-d)*60).toFixed(3)}&prime;</span>`}
function grid(lat,lon){const L='abcdefghijklmnopqrstuvwx';let x=lon+180,y=lat+90;
const fx=Math.floor(x/20),fy=Math.floor(y/10);x-=fx*20;y-=fy*10;
const sx=Math.floor(x/2),sy=Math.floor(y);x=(x-sx*2)*12;y=(y-sy)*24;
const ux=Math.floor(x),uy=Math.floor(y);x=(x-ux)*10;y=(y-uy)*10;
const ex=Math.floor(x),ey=Math.floor(y);x=(x-ex)*24;y=(y-ey)*24;
return L[fx].toUpperCase()+L[fy].toUpperCase()+sx+sy+L[ux]+L[uy]+ex+ey+L[Math.floor(x)]+L[Math.floor(y)]}
function dop(v){const n=parseFloat(v);if(!(n>0))return '-';
const q=n<=1?'ideal':n<=2?'excellent':n<=5?'good':n<=10?'moderate':n<=20?'fair':'poor';return `${v} <span class="mu">${q}</span>`}
function up(s){return `${Math.floor(s/86400)?Math.floor(s/86400)+'d ':''}${Math.floor(s/3600)%24}h ${Math.floor(s/60)%60}m ${s%60}s`}
function order(s){return s.slice().sort((a,b)=>a[0]<b[0]?-1:a[0]>b[0]?1:a[1]-b[1])}
function sky(s){let g='';for(const r of[100,66.67,33.33])g+=`<circle r="${r}" fill="none" stroke="var(--bd)"/>`;
g+='<path d="M-100 0H100M0-100V100" stroke="var(--bd)"/>';
for(const a of[['N',0,-104],['E',107,3],['S',0,112],['W',-107,3]])g+=`<text x="${a[1]}" y="${a[2]}" text-anchor="middle" font-size="9" fill="var(--mu)">${a[0]}</text>`;
for(const t of s){if(t[2]<0||t[3]<0)continue;const r=100*(90-t[2])/90,a=t[3]*Math.PI/180,x=(r*Math.sin(a)).toFixed(1),y=(-r*Math.cos(a)).toFixed(1),c=`var(${sys(t[0])[2]})`;
g+=`<circle cx="${x}" cy="${y}" r="8.5" fill="${t[5]?c:'var(--card)'}" stroke="${c}" stroke-width="1.5"/><text x="${x}" y="${(+y+3).toFixed(1)}" text-anchor="middle" font-size="8" fill="${t[5]?'var(--card)':'var(--tx)'}">${t[1]}</text>`}
$('sky').innerHTML=g}
function snr(s){const v=order(s.filter(t=>t[4]>0)),w=300/Math.max(v.length,1),bw=Math.min(18,w-3);
let g=v.length?'':'<text x="150" y="60" text-anchor="middle" font-size="10" fill="var(--mu)">no signals yet</text>';
v.forEach((t,i)=>{const h=Math.min(t[4],50)*2,x=i*w+(w-bw)/2,c=`var(${sys(t[0])[2]})`;
g+=`<rect x="${x.toFixed(1)}" y="${110-h}" width="${bw.toFixed(1)}" height="${h}" fill="${t[5]?c:'none'}" stroke="${c}"/>`;
g+=`<text x="${(x+bw/2).toFixed(1)}" y="124" text-anchor="middle" font-size="${Math.min(11,w*.75).toFixed(1)}" fill="var(--mu)">${t[1]}</text>`});
g+='<path d="M0 110.5H300" stroke="var(--bd)"/>';for(const d of[20,40])g+=`<text x="0" y="${113-d*2}" font-size="9" fill="var(--mu)">${d}</text>`;
$('snr').innerHTML=g}
function show(d){$('fw').textContent='v'+d.fw;const b=d.bt,good=b.label=='Oscar linked';
$('st').innerHTML=`<span><b>${d.utc||'--:--:--'}</b> UTC</span><span class="mu">${d.date||''}</span><span class="lk ${good?'ok':'warn'}">${esc(b.label)}</span>`;
const has=d.fix>0&&d.lat!=null;
dl('pos',has?[['lat',deg(d.lat,'N','S')],['lon',deg(d.lon,'E','W')],['grid',`<span class="big">${grid(d.lat,d.lon)}</span>`],
['altitude',`${d.alt.toFixed(1)} m <span class="mu">above sea level</span>`],['speed',`${(d.spd*1.852).toFixed(1)} km/h`],['course',d.spd>1?`${d.crs.toFixed(0)}&deg;`:'-']]
:[['','waiting for a fix','warn']]);
const mode=['','none','2D','3D'][d.mode]||'none';
dl('fix',[['fix',d.fix>0?mode+(d.fix==2?' DGPS':''):'none',d.fix>0?'ok':'warn'],['satellites',`${d.used} used, ${d.sats.length} in view`],
['HDOP',dop(d.hdop)],['PDOP',dop(d.pdop)],['VDOP',dop(d.vdop)],['last fix data',d.age==null?'-':`${d.age.toFixed(1)} s ago`]]);
sky(d.sats);snr(d.sats);
$('tb').innerHTML=order(d.sats).map(t=>`<tr><td style="color:var(${sys(t[0])[2]})">${sys(t[0])[1]}</td><td>${t[1]}</td><td>${t[2]<0?'-':t[2]+'&deg;'}</td><td>${t[3]<0?'-':t[3]+'&deg;'}</td><td>${t[4]||'-'}</td><td>${t[5]?'yes':''}</td></tr>`).join('')||'<tr><td colspan="6" class="mu">none in view</td></tr>';
dl('lnk',[['status',esc(b.label),good?'ok':'warn'],['PC port',b.port?esc(b.port):'not learned yet'],
['last dial',b.res?`${b.res} <span class="mu">${b.sdp} ms</span>`:good||b.label=='awaiting sats'?'connected':'-'],['failed dials',b.fails],['paired PCs',b.paired]]);
const p=d.pwr,s=d.sys;
dl('brd',[['power',p.usb?`USB <span class="mu">${p.v.toFixed(2)} V</span>`:p.v>0?`battery ${p.pct}% <span class="mu">${p.v.toFixed(2)} V</span>`:'no battery sense'],
['uptime',up(d.up)],['free memory',`${Math.round(s.heap/1024)} KB <span class="mu">lowest ${Math.round(s.minheap/1024)} KB</span>`],
['Wi-Fi',`${s.clients} connected`],['NMEA',`${s.ok} good <span class="${s.bad?'warn':'mu'}">${s.bad} bad checksum</span>`]])}
async function tick(){const c=new AbortController(),k=setTimeout(()=>c.abort(),3000);
try{const r=await fetch('/api',{cache:'no-store',signal:c.signal});show(await r.json())}
catch(e){$('st').innerHTML='<span class="warn">no answer from the board</span>'}
clearTimeout(k);setTimeout(tick,1000)}
tick();
</script></body></html>)HTML";
