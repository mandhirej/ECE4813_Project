#!/usr/bin/env python3
import argparse, re
from pathlib import Path
from typing import Dict
import numpy as np
from PIL import Image

OUT_SIZE=64
NUM_PIXELS=4096

def parse_hex_token(token:str)->int:
    token=token.strip().rstrip(',')
    if token.lower().endswith('h'):
        return int(token[:-1],16)
    if re.fullmatch(r'[0-9A-Fa-f]+',token):
        return int(token,16) if re.search(r'[A-Fa-f]',token) else int(token,10)
    raise ValueError(token)

def choose_signal_key(legend:Dict[int,str], kind:str)->int:
    pats={
        'we':[r'cropper_bram:u_image_ram\|we$',r'u_image_ram\|we$',r'\|we$'],
        'addr':[r'cropper_bram:u_image_ram\|write_addr(?:\[[^\]]+\])?$',r'u_image_ram\|write_addr(?:\[[^\]]+\])?$',r'\|write_addr(?:\[[^\]]+\])?$'],
        'data':[r'cropper_bram:u_image_ram\|write_data(?:\[[^\]]+\])?$',r'u_image_ram\|write_data(?:\[[^\]]+\])?$',r'\|write_data(?:\[[^\]]+\])?$']
    }
    for pat in pats[kind]:
        rx=re.compile(pat,re.I)
        m=[k for k,v in legend.items() if rx.search(v)]
        if m: return min(m)
    raise RuntimeError(f'Could not find SignalTap {kind} signal')

def parse_block(text):
    legend={}
    for line in text.splitlines():
        m=re.match(r'^\s*(\d+)\s*=\s*(.+?)\s*$',line)
        if m: legend[int(m.group(1))]=m.group(2).strip()
    if not legend: raise RuntimeError('No Signal Legend found')
    wk=choose_signal_key(legend,'we'); ak=choose_signal_key(legend,'addr'); dk=choose_signal_key(legend,'data'); mk=max(legend)
    rows=[]
    for line in text.splitlines():
        p=line.split()
        if len(p)<mk+2: continue
        try: sample=int(p[0])
        except: continue
        vals=p[1:mk+2]
        try:
            rows.append((sample,parse_hex_token(vals[wk]),parse_hex_token(vals[ak]),parse_hex_token(vals[dk])))
        except: pass
    return rows

def extract_frames(rows):
    writes=[(s,a,d) for s,w,a,d in rows if w==1]
    frames=[]; meta=[]; i=0
    while i<len(writes):
        if writes[i][1]!=0: i+=1; continue
        c=writes[i:i+NUM_PIXELS]
        if len(c)<NUM_PIXELS: break
        if [x[1] for x in c]==list(range(NUM_PIXELS)):
            arr=np.array([x[2] for x in c],dtype=np.uint8).reshape(64,64)
            frames.append(arr); meta.append((c[0][0],c[-1][0])); i+=NUM_PIXELS
        else: i+=1
    return frames,meta

def split_blocks(text):
    rx=re.compile(r'^\s*(?:@label|label)\s*:?\s+(.+?)\s*$',re.I|re.M)
    ms=list(rx.finditer(text))
    if not ms:
        raise RuntimeError('No labels found. Add a line like: @label 10Clubs before each pasted capture.')
    out=[]
    for i,m in enumerate(ms):
        end=ms[i+1].start() if i+1<len(ms) else len(text)
        out.append((m.group(1).strip(),text[m.end():end].strip()))
    return out

def safe_label(s):
    s=re.sub(r'[<>:"/\\|?*]','_',s).strip()
    if not s: raise RuntimeError('Invalid empty label')
    return s

def next_idx(folder,prefix):
    rx=re.compile(re.escape(prefix)+r'_(\d+)\.png$',re.I)
    best=-1
    for p in folder.glob(f'{prefix}_*.png'):
        m=rx.match(p.name)
        if m: best=max(best,int(m.group(1)))
    return best+1

def main():
    ap=argparse.ArgumentParser(description='Convert multiple labeled SignalTap captures in one text file into 64x64 dataset images.')
    ap.add_argument('--input',required=True,type=Path)
    ap.add_argument('--dataset-root',required=True,type=Path)
    ap.add_argument('--preview',action='store_true')
    args=ap.parse_args()
    text=args.input.read_text(encoding='utf-8',errors='ignore')
    blocks=split_blocks(text)
    total=0
    print(f'Found {len(blocks)} labeled capture block(s).\n')
    for n,(raw,block) in enumerate(blocks,1):
        label=safe_label(raw)
        print('='*60); print(f'Block {n}: {label}'); print('='*60)
        try:
            rows=parse_block(block); frames,meta=extract_frames(rows)
        except Exception as e:
            print(f'SKIPPED: {e}\n'); continue
        print(f'Parsed rows    : {len(rows)}')
        print(f'WE=1 writes    : {sum(1 for _,w,_,_ in rows if w==1)}')
        print(f'Complete crops : {len(frames)}')
        if not frames:
            print('No complete 000..FFF frame found.\n'); continue
        cdir=args.dataset_root/label; cdir.mkdir(parents=True,exist_ok=True)
        prefix=f'{label}_fpga'; idx=next_idx(cdir,prefix)
        if args.preview:
            pdir=args.dataset_root/'_previews'/label; pdir.mkdir(parents=True,exist_ok=True)
        for frame,(ss,es) in zip(frames,meta):
            fn=f'{prefix}_{idx:04d}.png'; out=cdir/fn
            Image.fromarray(frame,'L').save(out)
            print(f'  saved {out} (samples {ss}..{es}, mean={frame.mean():.1f})')
            if args.preview:
                Image.fromarray(frame,'L').resize((512,512),Image.Resampling.NEAREST).save(pdir/f'{prefix}_{idx:04d}_preview.png')
            idx+=1; total+=1
        print()
    print(f'DONE: saved {total} image(s) into {args.dataset_root}')

if __name__=='__main__': main()
