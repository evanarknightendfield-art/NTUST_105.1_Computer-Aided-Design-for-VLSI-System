#!/usr/bin/env python3
"""Fresh, deterministic cases, separate from the repository's existing testcases."""
import importlib.util
from pathlib import Path
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('generator',root/'tools/gen_input.py')
g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
out=Path(__file__).resolve().parents[1]/'experiments/final_holdout_inputs';out.mkdir(parents=True,exist_ok=True)
for i in range(12):
    n=[25,80,250,700][i%4]; dim=[120,300,700,1200][i%4]
    text=g.make(n,dim,dim,dim/110,g.LIBS['wide' if i%2 else 'default'],[2,4,8][i%3],int(dim*0.9),'edge' if i%3==0 else 'random','cluster' if i%2 else 'uniform',[1,5,20][i%3],2026101280+i)
    (out/f'input{i+1:03d}.cbi').write_text(text)
