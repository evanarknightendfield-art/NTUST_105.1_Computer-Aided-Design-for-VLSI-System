#!/usr/bin/env python3
"""Small-design certificate: enumerate ALL legal positions below incumbent cap.

Unlike restricted-column infeasibility, infeasibility of this complete model
can certify a lower bound. Requires NumPy/SciPy; never used by the C++ placer.
"""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import sys
import time
import numpy as np
from scipy.optimize import milp, LinearConstraint, Bounds
from scipy.sparse import coo_matrix

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT.parents[1] / 'Grader/tools'
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location('reference_model', TOOLS / 'exact_bound.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def complete_mip(m, keys, time_limit):
    count=len(keys)
    ci=np.array([k[0] for k in keys])
    cr=np.array([k[1] for k in keys])
    ck=np.array([k[2] for k in keys])
    widths=m.W[ci]
    rep=np.repeat(np.arange(count), widths)
    offsets=np.concatenate([np.arange(w) for w in widths])
    _,inverse=np.unique(cr[rep]*m.ns+ck[rep]+offsets,return_inverse=True)
    eq=coo_matrix((np.ones(count),(ci,np.arange(count))),shape=(len(m.mov),count)).tocsr()
    ub=coo_matrix((np.ones(len(rep)),(inverse,rep))).tocsr()
    # HiGHS' default relative gap permits status=0 before the objective bound
    # equals the incumbent. Request zero gap and retain the actual dual bound.
    return milp(np.array([m.cols[k] for k in keys]),integrality=np.ones(count),bounds=Bounds(0,1),
                constraints=[LinearConstraint(eq,1,1),LinearConstraint(ub,-np.inf,1)],
                options={'time_limit':time_limit,'mip_rel_gap':0.0})


def rebuild_free_sites(m, nodes, gp, rows):
    # The legacy Model clamps only one end of each fixed-object interval.
    # A pad left of the die can yield free[r,0:-1]=False in NumPy, blocking
    # almost an entire row. Clip BOTH ends to [0,ns] before slicing.
    m.free[:]=False
    row_index={y:i for i,y in enumerate(m.ys)}
    for y,h,x,spacing,count in rows:
        a=int(round((x-m.X0)/m.sp))
        m.free[row_index[y],a:a+count]=True
    for name,(w,h,terminal) in nodes.items():
        if not (terminal or gp[name][3]) or w<=0 or h<=0:continue
        x,y=gp[name][:2]
        a=max(0,min(m.ns,int(math.floor((x-m.X0)/m.sp+1e-9))))
        b=max(0,min(m.ns,int(math.ceil((x+w-m.X0)/m.sp-1e-9))))
        if a>=b:continue
        for r,ry in enumerate(m.ys):
            if y<ry+rows[0][1]-1e-9 and ry<y+h-1e-9:m.free[r,a:b]=False
    cumulative=np.concatenate([np.zeros((len(m.ys),1),dtype=np.int64),np.cumsum(m.free,axis=1)],axis=1)
    for w in np.unique(m.W):
        valid=np.zeros((len(m.ys),m.ns),dtype=bool)
        if w<=m.ns:valid[:,:m.ns-w+1]=(cumulative[:,w:]-cumulative[:,:m.ns-w+1])==w
        m.valid[w]=valid
    for i in range(len(m.mov)):
        if not m.valid[m.W[i]][m.cur_r[i],m.cur_k[i]]:
            raise RuntimeError('incumbent position excluded from complete model: '+m.mov[i])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('bench')
    ap.add_argument('placement')
    ap.add_argument('--output', required=True)
    ap.add_argument('--time-limit', type=float, default=120)
    ap.add_argument('--max', action='store_true')
    args = ap.parse_args()
    start = time.perf_counter()
    files = module.read_aux(args.bench)
    nodes = module.read_nodes(files['nodes'])
    rows = module.read_scl(files['scl'])
    spacing = rows[0][3]
    # Guard the reference model's uniform grid / integral width assumptions.
    origin = min(r[2] for r in rows)
    if any(abs(r[1]-rows[0][1])>1e-9 or abs(r[3]-spacing)>1e-9 or abs((r[2]-origin)/spacing-round((r[2]-origin)/spacing))>1e-7 for r in rows):
        raise SystemExit('certificate requires a common aligned site grid')
    m = module.Model(args.bench, args.placement)
    if len(m.mov)>1000 or any(abs(nodes[n][0]/spacing-round(nodes[n][0]/spacing))>1e-7 or nodes[n][0]<=0 or abs(nodes[n][1]-rows[0][1])>1e-7 for n in m.mov):
        raise SystemExit('certificate requires <=1000 positive integral-site-width, single-height movable cells')
    rebuild_free_sites(m,nodes,module.read_pl(files['pl']),rows)
    cap = float(m.cur.max())
    for i in range(len(m.mov)):
        m.add_window(i, 0, len(m.ys)-1, 0, m.ns-1, limit=cap+1e-7)
    print(f'Complete enumeration: {len(m.mov)} cells, {len(m.cols)} positions, cap={cap}', flush=True)
    lp, keys, _ = m.solve_lp()
    if lp.status != 0:
        raise RuntimeError('incumbent must make full LP feasible: '+lp.message)
    mip = complete_mip(m, keys, args.time_limit)
    result = dict(bench=Path(args.bench).name, cells=len(m.mov), columns=len(keys), cap=cap,
                  incumbent_total=float(m.cur.sum()), lp_lower_bound=float(lp.fun),
                  mip_status=int(mip.status), mip_message=str(mip.message),
                  mip_value=None if mip.fun is None else float(mip.fun),
                  mip_dual_bound=None if getattr(mip,'mip_dual_bound',None) is None else float(mip.mip_dual_bound),
                  scope='all legal grid positions with displacement <= incumbent maximum',
                  mip_gap=None if getattr(mip,'mip_gap',None) is None else float(mip.mip_gap),
                  globally_optimal_under_cap=bool(mip.status==0 and abs(mip.fun-mip.mip_dual_bound)<1e-5))
    if args.max:
        low, high = 0.0, cap
        # All positions below each smaller cap are a subset of our complete
        # enumerated universe. This time infeasibility is a valid certificate.
        for _ in range(18):
            middle=(low+high)/2
            usable=[k for k in keys if m.cols[k]<=middle]
            if len({k[0] for k in usable})<len(m.mov):
                low=middle
                continue
            test,_,_=m.solve_lp(keys=usable)
            if test.status==2: low=middle
            elif test.status==0: high=middle
            else: raise RuntimeError(test.message)
        result['maximum_lp_lower_bound']=low
        result['maximum_lp_feasible_upper_endpoint']=high
    result['seconds']=time.perf_counter()-start
    Path(args.output).parent.mkdir(parents=True,exist_ok=True)
    Path(args.output).write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
