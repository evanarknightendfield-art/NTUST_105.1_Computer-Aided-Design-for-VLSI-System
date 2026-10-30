#!/usr/bin/env python3
import importlib.util,json
from pathlib import Path
root=Path(__file__).resolve().parents[2]
s=importlib.util.spec_from_file_location('gen',root/'tools/gen_input.py');g=importlib.util.module_from_spec(s);s.loader.exec_module(g)
base=Path(__file__).resolve().parents[1]/'experiments'
settings=[(80,300,1,'default','uniform',1,2),(250,450,1.5,'wide','cluster',5,4),(800,1000,2,'default','uniform',2,2),(2000,2000,3,'wide','uniform',1,2),(5000,3000,4,'default','cluster',10,3),(100,1000,1,'default','uniform',1,1),(300,140,1,'default','uniform',1,2),(10000,4500,5,'default','cluster',1,2)]
folder=base/'tuning_inputs';folder.mkdir(parents=True,exist_ok=True)
for i,(n,dim,ls,lib,dist,w,f) in enumerate(settings,1):
 text=g.make(n,dim,dim,ls,g.LIBS[lib],f,max(20,int(dim*.7)),'edge' if i%2 else 'random',dist,w,612000+i)
 (folder/f'input{i:03d}.cbi').write_text(text)
(base/'tuning_config.json').write_text(json.dumps(settings,indent=2))
