#!/usr/bin/env python3
"""Predetermined independent cases; do not change after seeing final comparison."""
import importlib.util,json
from pathlib import Path
root=Path(__file__).resolve().parents[2]
s=importlib.util.spec_from_file_location('gen',root/'tools/gen_input.py');g=importlib.util.module_from_spec(s);s.loader.exec_module(g)
project=Path(__file__).resolve().parents[1];folder=project/'experiments/holdout_inputs';folder.mkdir(parents=True,exist_ok=True)
settings=[]
for i in range(12):
 n=[30,120,500,1800,4000,8000][i%6];dim=[150,500,800,1800,3000,4000][i%6]
 ls=[1,1,2,3,4,5][i%6];lib='wide' if i%3==0 else 'default';dist='cluster' if i>=6 else 'uniform';w=[1,3,10][i%3];f=[1,2,4][i%3]
 settings.append(dict(n=n,dim=dim,lscale=ls,lib=lib,dist=dist,w=w,srcF=f,seed=612880+i))
 text=g.make(n,dim,dim,ls,g.LIBS[lib],f,max(20,int(dim*.8)),'edge' if i%2 else 'random',dist,w,612880+i)
 (folder/f'input{i+1:03d}.cbi').write_text(text)
(project/'experiments/holdout_config.json').write_text(json.dumps(settings,indent=2))
