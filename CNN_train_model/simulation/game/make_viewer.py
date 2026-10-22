"""Build <run>/game_viewer.html: step-by-step player for the rendered game frames."""
import html
import json
import os
import sys

run = sys.argv[1]
steps = [json.loads(l) for l in open(os.path.join(run, "trace.jsonl"))]
console = open(os.path.join(run, "console.log"), errors="replace").read()

KEYS = [("joker on our board", "Opponent joker"), ("remove the opponent joker", "Remove joker"),
        ("use our own joker", "Use own joker (PEACE)"), ("do 2 move", "Two-card shift"),
        ("game over", "Game finished"), ("type H", "Gestures on")]
marks = []
for s in steps:
    for k, label in KEYS:
        if k in s["what"] and label not in [m[1] for m in marks]:
            marks.append((s["n"], label))
data = [{"n": s["n"], "t": round(s["t"], 1), "what": s["what"]} for s in steps]

page = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Suit Stack Simulation</title>
<style>
:root{--bg:#0b1220;--panel:#141f33;--ink:#e8eef6;--muted:#93a3b8;--accent:#20f0e0;--warm:#ffb347}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,Segoe UI,sans-serif}
main{max-width:1100px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:4px 0 2px}p.sub{color:var(--muted);margin:0 0 12px}
img{width:100%;height:auto;display:block;border-radius:8px;background:#000}
.bar{display:flex;gap:8px;align-items:center;margin:10px 0;flex-wrap:wrap}
button{background:var(--panel);color:var(--ink);border:1px solid #2a3b57;border-radius:6px;padding:6px 12px;font:inherit;cursor:pointer}
button:hover{border-color:var(--accent)}input[type=range]{flex:1;min-width:160px}
.cap{background:var(--panel);border-radius:8px;padding:10px 12px;min-height:44px}
.cap b{color:var(--warm)}.t{color:var(--muted);font-variant-numeric:tabular-nums}
.marks{display:flex;gap:6px;flex-wrap:wrap;margin:8px 0}
.marks button{font-size:13px;padding:4px 10px}
details{margin-top:14px}pre{background:#060b14;color:#c9d6e6;padding:12px;border-radius:8px;max-height:420px;overflow:auto;font-size:12px;white-space:pre-wrap}
</style></head><body><main>
<h1>Suit Stack Race: simulated game</h1>
<p class="sub">Real <code>atlas_main.c</code> + integer CNN (weights from the Quartus project) on a virtual board and camera.
Every action is a hand gesture held 3 s in the HG zone. Use the arrow keys or the slider.</p>
<img id="frame" alt="HDMI screen at this step">
<div class="bar"><button id="prev">&#9664;</button><button id="play">Play</button><button id="next">&#9654;</button>
<input id="slider" type="range" min="0" value="0"><span id="pos" class="t"></span></div>
<div class="cap" id="cap"></div>
<div class="marks" id="marks"></div>
<details><summary>Arm DS console output of the whole game</summary><pre>__CONSOLE__</pre></details>
</main><script>
const S=__DATA__, MARKS=__MARKS__;
const img=document.getElementById('frame'),sl=document.getElementById('slider'),cap=document.getElementById('cap'),pos=document.getElementById('pos');
let i=0,timer=null;sl.max=S.length-1;
function pad(n){return String(n).padStart(3,'0')}
function show(k){i=Math.max(0,Math.min(S.length-1,k));const s=S[i];img.src='frames/step_'+pad(s.n)+'.jpg';sl.value=i;
 pos.textContent=(i+1)+' / '+S.length;
 cap.innerHTML='<span class="t">t = '+s.t.toFixed(1)+' s</span> &nbsp; <b>'+s.what.replace(/&/g,'&amp;').replace(/</g,'&lt;')+'</b>';
 if(i+1<S.length){new Image().src='frames/step_'+pad(S[i+1].n)+'.jpg'}}
function play(){if(timer){clearInterval(timer);timer=null;document.getElementById('play').textContent='Play';return}
 document.getElementById('play').textContent='Pause';timer=setInterval(()=>{if(i>=S.length-1)play();else show(i+1)},1100)}
document.getElementById('prev').onclick=()=>show(i-1);document.getElementById('next').onclick=()=>show(i+1);
document.getElementById('play').onclick=play;sl.oninput=()=>show(+sl.value);
document.addEventListener('keydown',e=>{if(e.key==='ArrowRight')show(i+1);if(e.key==='ArrowLeft')show(i-1);if(e.key===' '){e.preventDefault();play()}});
const mk=document.getElementById('marks');MARKS.forEach(([n,l])=>{const b=document.createElement('button');b.textContent=l;b.onclick=()=>show(S.findIndex(s=>s.n===n));mk.appendChild(b)});
show(0);
</script></body></html>"""
page = page.replace("__DATA__", json.dumps(data)).replace("__MARKS__", json.dumps(marks)).replace("__CONSOLE__", html.escape(console))
open(os.path.join(run, "game_viewer.html"), "w", encoding="utf-8").write(page)
print("viewer written with", len(marks), "highlights")
