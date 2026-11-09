#pragma once

static const char CONTROL_PANEL[] PROGMEM=R"panel(<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>CubicOS Studio</title>
<style>
:root{color-scheme:dark;--bg:#0c101b;--panel:#161c2b;--line:#2b3550;--muted:#bdc8dc;--a:#c5acff;--b:#8fffe2}
*{box-sizing:border-box}
[hidden]{display:none!important}
body{margin:0;background:var(--bg);color:#f8faff;font:16px/1.5 system-ui,sans-serif}
main{max-width:1030px;margin:auto;padding:clamp(18px,4vw,44px)}
header{margin-bottom:24px}h1,h2,p{margin:0}h1{font-size:clamp(34px,6vw,56px);letter-spacing:-.05em;line-height:1.1}
h2{font-size:21px;margin-bottom:12px}p{color:var(--muted);margin:8px 0}
.eyebrow{color:var(--b);letter-spacing:.15em;font-weight:700;font-size:13px}
.grid{display:grid;grid-template-columns:minmax(270px,1fr) minmax(300px,1.15fr);gap:18px}
section{background:var(--panel);border:1px solid var(--line);border-radius:18px;padding:20px;min-width:0}
.stack{display:grid;gap:18px}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.frame{width:min(100%,300px);aspect-ratio:1;margin:14px auto 18px;background:#070a12;border:1px solid var(--line);border-radius:14px;overflow:hidden}
.frame img{width:100%;height:100%;display:block;image-rendering:pixelated}.frame img[hidden]{display:none}
.empty{display:grid;place-items:center;height:100%;color:var(--muted);padding:14px;text-align:center}
.frame img:not([hidden])+.empty{display:none}
button,.button,select,input{font:inherit;color:inherit;background:#222a3d;border:1px solid #3b4966;border-radius:9px;padding:9px 12px}
button,.button{cursor:pointer;text-decoration:none;display:inline-block}
button:hover,.button:hover{border-color:var(--a)}
button[aria-pressed="true"],.primary{background:var(--a);border-color:var(--a);color:#111325;font-weight:700}
button:disabled{opacity:.6;cursor:wait}:focus-visible{outline:2px solid var(--b);outline-offset:3px}
label{display:grid;gap:4px;color:var(--muted)}label input,label select{width:100%;min-width:0}
.fields{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:14px}
.status{font-size:14px;min-height:21px;overflow-wrap:anywhere}.status[data-error="true"]{color:#ffb4ba}
audio{width:100%;margin-top:12px}input[type=file]{max-width:100%}
@media(max-width:690px){.grid{grid-template-columns:1fr}}
@media(max-width:350px){.fields{grid-template-columns:1fr}}
</style>
</head>
<body><main>
<header><div class="eyebrow">GEEKMAGIC HELLOCUBIC-LITE</div><h1>CubicOS Studio</h1><p id="device" class="status" role="status">Подключаюсь к кубику…</p></header>
<div class="grid">
  <section>
    <h2>Дисплей</h2>
    <div class="row" id="pages" aria-label="Экран кубика"></div>
    <div class="frame"><img id="preview" alt="Кадр дисплея кубика" hidden><div class="empty">Загрузите кадр с кубика</div></div>
    <button id="capture">Снять кадр</button>
    <p id="screen-status" class="status" role="status"></p>
  </section>
  <div class="stack">
    <section>
      <h2>Конструктор оформления</h2>
      <p id="designer-intro">Выберите палитру, циферблат, яркость и автосмену экранов.</p>
      <a id="designer-link" class="button primary" href="/studio">Открыть конструктор</a>
      <form id="settings" hidden>
        <div class="fields">
          <label>Палитра<select name="palette"><option value="0">Аврора</option><option value="1">Янтарь</option><option value="2">Лёд</option><option value="3">Монохром</option></select></label>
          <label>Часы<select name="clock"><option value="0">Карточка</option><option value="1">Крупный циферблат</option></select></label>
          <label>Яркость, %<input name="brightness" type="number" min="15" max="100" required></label>
          <label>Автосмена, сек.<select name="cycle_seconds"><option value="0">Выкл.</option><option value="5">5</option><option value="10">10</option><option value="20">20</option><option value="30">30</option><option value="60">60</option><option value="120">120</option></select></label>
          <label>Смещение от UTC, минут<input name="utc_minutes" type="number" min="-720" max="840" required></label>
        </div>
        <button class="primary" type="submit">Применить и сохранить</button>
      </form>
      <p id="settings-status" class="status" role="status"></p>
    </section>
    <section>
      <h2>Музыкальный спектр</h2>
      <p>Откройте аудиофайл и нажмите воспроизведение. Кубик покажет его спектр.</p>
      <input id="file" type="file" accept="audio/*" aria-label="Аудиофайл">
      <audio id="player" controls></audio>
      <p id="audio-status" class="status" role="status"></p>
    </section>
    <section><h2>Устройство</h2><div class="row"><a class="button" href="/wifi">Wi-Fi и поиск сетей</a><a class="button" href="/update">Обновить прошивку</a></div></section>
  </div>
</div></main>
<script>
const el=id=>document.getElementById(id);
const setupHeaders={'X-CubicOS-Setup':'1','Content-Type':'application/json'};
const screenNames=['Часы','Компьютер','Музыка','Погода','О кубике'];
const pages=screenNames.map((name,page)=>{
  const button=document.createElement('button');
  button.textContent=name;
  button.setAttribute('aria-pressed','false');
  button.onclick=()=>choose(page);
  el('pages').append(button);
  return button;
});
function report(id,message,error=false){
  el(id).textContent=message;
  el(id).dataset.error=error;
}
async function request(url,init){
  const reply=await fetch(url,{cache:'no-store',signal:AbortSignal.timeout(12000),...init});
  if(!reply.ok)throw Error('HTTP '+reply.status);
  return reply;
}
function showPage(page){pages.forEach((button,i)=>button.setAttribute('aria-pressed',i===page));}
async function refresh(){
  try{
    const device=await (await request('/api/status')).json();
    showPage(device.page);
    report('device','Версия '+device.version+' · '+device.ip);
  }catch{report('device','Кубик не отвечает',true);}
}
async function choose(page){
  try{
    await request('/api/page',{method:'POST',body:new URLSearchParams({page})});
    showPage(page);
    return true;
  }catch{report('screen-status','Экран не переключился',true);return false;}
}
let previewURL;
async function capture(){
  el('capture').disabled=true;
  report('screen-status','Получаю кадр…');
  try{
    const frame=await (await request('/debug/screen.bmp')).blob();
    const url=URL.createObjectURL(frame);
    if(previewURL)URL.revokeObjectURL(previewURL);
    previewURL=url;
    el('preview').src=url;
    el('preview').hidden=false;
    report('screen-status','Кадр дисплея 240 × 240');
  }catch{report('screen-status','Кадр не загрузился. Попробуйте ещё раз.',true);}
  finally{el('capture').disabled=false;}
}
el('capture').onclick=capture;

const settings=el('settings');
async function openDesigner(){
  el('designer-link').hidden=true;
  try{
    const values=await (await request('/api/display')).json();
    for(const key of ['palette','clock','brightness','cycle_seconds','utc_minutes']){
      settings.elements[key].value=values[key];
    }
    settings.hidden=false;
  }catch{report('settings-status','Не удалось загрузить настройки',true);}
}
settings.onsubmit=async event=>{
  event.preventDefault();
  const button=settings.querySelector('button');
  button.disabled=true;
  try{
    const values=Object.fromEntries(Array.from(new FormData(settings),([key,value])=>[key,Number(value)]));
    await request('/api/display',{method:'POST',headers:setupHeaders,body:JSON.stringify(values)});
    report('settings-status','Настройки сохранены');
    await capture();
  }catch{report('settings-status','Настройки не сохранены',true);}
  finally{button.disabled=false;}
};
if(location.pathname==='/studio')openDesigner();

const player=el('player');
let fileURL, context, analyser, bins, timer, sending=false, generation=0;
el('file').onchange=()=>{
  const file=el('file').files[0];
  if(!file)return;
  player.pause();
  if(fileURL)URL.revokeObjectURL(fileURL);
  fileURL=URL.createObjectURL(file);
  player.src=fileURL;
};
function stopAudio(){
  clearInterval(timer);
  generation++;
  report('audio-status','');
}
player.onpause=stopAudio;
player.onended=stopAudio;
player.onplay=async()=>{
  stopAudio();
  const current=generation;
  try{
    if(!context){
      context=new AudioContext();
      analyser=context.createAnalyser();
      analyser.fftSize=2048;
      bins=new Uint8Array(analyser.frequencyBinCount);
      context.createMediaElementSource(player).connect(analyser).connect(context.destination);
    }
    await context.resume();
    if(current!==generation||player.paused)return;
    if(!await choose(2)||current!==generation||player.paused)return;
    timer=setInterval(async()=>{
      if(sending||current!==generation)return;
      sending=true;
      analyser.getByteFrequencyData(bins);
      const bands=Array.from({length:16},(_,i)=>{
        const first=Math.floor(2**(i*0.56));
        const end=Math.min(bins.length,Math.max(first+1,Math.floor(2**((i+1)*0.56))));
        let level=0;
        for(let bin=first;bin<end;bin++)level=Math.max(level,bins[bin]);
        return Math.round(level*100/255);
      });
      try{
        await request('/api/spectrum',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({bands})});
        if(current===generation)report('audio-status','Спектр передаётся на кубик');
      }catch{if(current===generation)report('audio-status','Кубик не принимает спектр',true);}
      finally{sending=false;}
    },220);
  }catch{report('audio-status','Не удалось запустить анализ звука',true);}
};
refresh();
setInterval(refresh,5000);
</script></body></html>)panel";
