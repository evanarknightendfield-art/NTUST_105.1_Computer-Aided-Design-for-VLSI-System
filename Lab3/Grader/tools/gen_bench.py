#!/usr/bin/env python3
"""Generate GSRC Bookshelf placement benchmarks (for holdout testing).

usage:
  gen_bench.py NAME OUTDIR [--cells N] [--util U] [--macros K] [--pads P] [--hotspots H]
                           [--split R] [--geom ibm|unit|micron] [--seed S]
  gen_bench.py --suite DIR [--count N] [--seed S]      # a mixed set: DIR/h01/h01.aux, ...

Each benchmark is a directory OUTDIR/NAME/ with NAME.{aux,nodes,nets,wts,pl,scl}.

What it contains (things the course benchmarks do not all have):
  * fixed macros INSIDE the core that cover several rows (row segments must be cut)
  * rows split into two subrows with a gap (--split = fraction of rows)
  * I/O pads around the chip, marked only with "terminal" in .nodes (like ibm05)
  * hotspots: clusters of cells piled up in the global placement (overflow)
  * local nets: pins are drawn from cells near a seed cell, so HPWL is meaningful
  * three geometries: ibm (site 66, row 504, negative origin), unit (site 1, row 16),
    micron (site 0.2, row 1.6, fractional coordinates)
  * keyword spelling varies (NumSites / Numsites), nets with and without names
"""
import argparse
import os

import numpy as np

GEOMS = {   # site width, row height, origin x, origin y, decimals
    'ibm': (66.0, 504.0, -33330.0, -33208.0, 1),
    'unit': (1.0, 16.0, 0.0, 0.0, 0),
    'micron': (0.2, 1.6, 10.0, 10.0, 3),
}
WIDTHS = np.array([2, 3, 4, 5, 6, 8, 10, 12, 14, 16, 20, 28])            # in sites
WPROB = np.array([12, 14, 16, 10, 12, 10, 8, 6, 4, 4, 2, 2], float)
DEGREES = np.array([2, 3, 4, 5, 6, 8, 10, 14, 20])
DPROB = np.array([55, 15, 9, 6, 5, 4, 3, 2, 1], float)


def fmt(v, dec):
    s = f'{v:.{dec}f}' if dec else f'{int(round(v))}'
    if '.' in s:
        s = s.rstrip('0').rstrip('.')
    return s


def generate(name, outdir, cells=2000, util=0.8, macros=2, pads=40, hotspots=1, split=0.2,
             geom='unit', seed=1):
    rng = np.random.default_rng(seed)
    sw, rh, ox, oy, dec = GEOMS[geom]
    wsites = rng.choice(WIDTHS, size=cells, p=WPROB / WPROB.sum())
    cell_area = wsites.sum() * sw * rh

    # macros: 2..6 rows tall, 10..40 sites wide; they take core area
    mdims = [(int(rng.integers(10, 41)), int(rng.integers(2, 7))) for _ in range(macros)]
    macro_area = sum(w * sw * h * rh for w, h in mdims)
    core_area = (cell_area / util + macro_area) / (1 - 0.03 * split * 2)   # split gaps eat a little
    side = np.sqrt(core_area)
    nrows = max(4, int(np.ceil(side / rh)))
    nsites = max(int(WIDTHS.max()) + 4, int(np.ceil(core_area / (nrows * rh) / sw)))
    W, H = nsites * sw, nrows * rh

    # rows (some split in two subrows with a gap of 3% of the width)
    rows = []
    for r in range(nrows):
        y = oy + r * rh
        if rng.random() < split:
            gap = max(2, int(0.03 * nsites))
            a = int(rng.integers(nsites // 4, 3 * nsites // 4))
            rows.append((y, [(ox, a), (ox + (a + gap) * sw, nsites - a - gap)]))
        else:
            rows.append((y, [(ox, nsites)]))

    # macros placed inside the core without overlapping each other
    placed = []
    for (mw, mh) in mdims:
        for _ in range(200):
            x = ox + rng.uniform(0.05, 0.95 - mw / nsites) * W
            r0 = int(rng.integers(1, max(2, nrows - mh - 1)))
            y = oy + r0 * rh + rng.choice([0.0, 0.5 * rh])      # not always row-aligned
            box = (x, y, mw * sw, mh * rh)
            if all(x + box[2] <= p[0] or p[0] + p[2] <= x or y + box[3] <= p[1] or p[1] + p[3] <= y
                   for p in placed):
                placed.append(box)
                break

    # global placement: background spread + hotspots (overflow)
    cx = rng.uniform(ox, ox + W, size=cells)
    cy = rng.uniform(oy, oy + H, size=cells)
    nh = int(hotspots * 0.12 * cells) if hotspots else 0
    if nh:
        centers = [(rng.uniform(ox, ox + W), rng.uniform(oy, oy + H)) for _ in range(hotspots)]
        idx = rng.choice(cells, size=nh, replace=False)
        for k, i in enumerate(idx):
            c = centers[k % hotspots]
            cx[i] = rng.normal(c[0], 0.04 * W)
            cy[i] = rng.normal(c[1], 0.04 * H)
    cw = wsites * sw
    gx = np.clip(cx - cw / 2, ox, ox + W - cw)
    gy = np.clip(cy - rh / 2, oy, oy + H - rh)
    gy = gy + rng.normal(0, 0.15 * rh, size=cells)                         # not on rows
    gy = np.clip(gy, oy, oy + H - rh)

    # pads around the chip (outside the core), 1 x 1
    pad_xy = []
    for k in range(pads):
        t = k / max(1, pads)
        side_k = k % 4
        if side_k == 0:
            pad_xy.append((ox + t * W, oy + H + rh))
        elif side_k == 1:
            pad_xy.append((ox + W + sw, oy + t * H))
        elif side_k == 2:
            pad_xy.append((ox + t * W, oy - 2 * rh))
        else:
            pad_xy.append((ox - 2 * sw, oy + t * H))

    names = [f'c{i}' for i in range(cells)]
    mnames = [f'm{i}' for i in range(len(placed))]
    pnames = [f'p{i}' for i in range(pads)]

    # nets: local — pins drawn from the cells near a seed cell (grid buckets)
    nb = max(1, int(np.sqrt(cells / 8)))
    bx = np.clip(((gx - ox) / W * nb).astype(int), 0, nb - 1)
    by = np.clip(((gy - oy) / H * nb).astype(int), 0, nb - 1)
    buckets = {}
    for i in range(cells):
        buckets.setdefault((bx[i], by[i]), []).append(i)
    nets = []
    for _ in range(int(cells * 1.05)):
        s = int(rng.integers(cells))
        deg = int(rng.choice(DEGREES, p=DPROB / DPROB.sum()))
        pool = []
        rad = 0
        while len(pool) < 3 * deg and rad <= nb:
            pool = [j for ix in range(bx[s] - rad, bx[s] + rad + 1)
                    for iy in range(by[s] - rad, by[s] + rad + 1) for j in buckets.get((ix, iy), [])]
            rad += 1
        others = rng.choice(pool, size=min(deg - 1, len(pool)), replace=False)
        pins = [names[s]] + [names[j] for j in others if j != s]
        if pads and rng.random() < 0.04:
            d = [(px - gx[s]) ** 2 + (py - gy[s]) ** 2 for px, py in pad_xy]
            pins.append(pnames[int(np.argmin(d))])
        if placed and rng.random() < 0.02:
            pins.append(mnames[int(rng.integers(len(placed)))])
        if len(pins) >= 2:
            nets.append(pins)

    # ---- write
    d = os.path.join(outdir, name)
    os.makedirs(d, exist_ok=True)
    named_nets = rng.random() < 0.5
    lower_kw = rng.random() < 0.5
    with open(os.path.join(d, name + '.aux'), 'w') as f:
        f.write(f'RowBasedPlacement : {name}.nodes {name}.nets {name}.wts {name}.pl {name}.scl\n')
    with open(os.path.join(d, name + '.nodes'), 'w') as f:
        f.write('UCLA nodes 1.0\n# generated by gen_bench.py\n\n')
        f.write(f'NumNodes : {cells + len(placed) + pads}\nNumTerminals : {len(placed) + pads}\n')
        for i in range(cells):
            f.write(f'\t{names[i]}\t{fmt(cw[i], dec)}\t{fmt(rh, dec)}\n')
        for k, (x, y, w, h) in enumerate(placed):
            f.write(f'\t{mnames[k]}\t{fmt(w, dec)}\t{fmt(h, dec)}\tterminal\n')
        for k in range(pads):
            f.write(f'\t{pnames[k]}\t1\t1\tterminal\n')
    with open(os.path.join(d, name + '.nets'), 'w') as f:
        f.write('UCLA nets 1.0\n# generated by gen_bench.py\n\n')
        f.write(f'NumNets : {len(nets)}\nNumPins : {sum(len(n) for n in nets)}\n')
        for k, pins in enumerate(nets):
            f.write(f'NetDegree : {len(pins)}' + (f'  n{k}' if named_nets else '') + '\n')
            for j, p in enumerate(pins):
                f.write(f'\t{p}\t{"O" if j == 0 else "I"} : 0 0\n')
    with open(os.path.join(d, name + '.wts'), 'w') as f:
        f.write('UCLA wts 1.0\n\n')
        for n in names + mnames + pnames:
            f.write(f'{n} 1\n')
    with open(os.path.join(d, name + '.pl'), 'w') as f:
        f.write('UCLA pl 1.0\n# generated by gen_bench.py\n\n')
        for i in range(cells):
            f.write(f'{names[i]}\t{fmt(gx[i], dec + 2)}\t{fmt(gy[i], dec + 2)}\t: N\n')
        for k, (x, y, w, h) in enumerate(placed):
            f.write(f'{mnames[k]}\t{fmt(x, dec)}\t{fmt(y, dec)}\t: N /FIXED\n')
        for k, (x, y) in enumerate(pad_xy):
            f.write(f'{pnames[k]}\t{fmt(x, dec)}\t{fmt(y, dec)}\t: {"FS" if k % 2 else "N"}\n')
    ns_kw = 'Numsites' if lower_kw else 'NumSites'
    with open(os.path.join(d, name + '.scl'), 'w') as f:
        f.write('UCLA scl 1.0\n# generated by gen_bench.py\n\n')
        f.write(f'{"Numrows" if lower_kw else "NumRows"} : {nrows}\n\n')
        for y, subs in rows:
            f.write('CoreRow Horizontal\n')
            f.write(f' Coordinate   : {fmt(y, dec)}\n Height       : {fmt(rh, dec)}\n')
            f.write(f' Sitewidth    : {fmt(sw, dec)}\n Sitespacing  : {fmt(sw, dec)}\n')
            f.write(' Siteorient   : 1\n Sitesymmetry : 1\n')
            for x0, n in subs:
                f.write(f' SubrowOrigin : {fmt(x0, dec)}  {ns_kw} : {n}\n')
            f.write('End\n')
    return {'name': name, 'cells': cells, 'nets': len(nets), 'rows': nrows, 'sites': nsites,
            'macros': len(placed), 'pads': pads, 'util': round(float(cell_area / (nrows * nsites * sw * rh)), 3),
            'geom': geom}


SUITE = [   # cells, util, macros, pads, hotspots, split, geom
    (300, 0.50, 2, 16, 1, 0.2, 'unit'),
    (1000, 0.70, 3, 40, 1, 0.3, 'ibm'),
    (2000, 0.80, 0, 0, 2, 0.0, 'micron'),
    (4000, 0.85, 4, 80, 2, 0.2, 'unit'),
    (6000, 0.60, 6, 120, 0, 0.5, 'ibm'),
    (10000, 0.88, 2, 160, 3, 0.1, 'unit'),
    (15000, 0.75, 5, 200, 2, 0.2, 'micron'),
    (25000, 0.82, 3, 300, 2, 0.1, 'ibm'),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('name', nargs='?')
    ap.add_argument('outdir', nargs='?', default='.')
    ap.add_argument('--suite')
    ap.add_argument('--count', type=int, default=len(SUITE))
    ap.add_argument('--cells', type=int, default=2000)
    ap.add_argument('--util', type=float, default=0.8)
    ap.add_argument('--macros', type=int, default=2)
    ap.add_argument('--pads', type=int, default=40)
    ap.add_argument('--hotspots', type=int, default=1)
    ap.add_argument('--split', type=float, default=0.2)
    ap.add_argument('--geom', choices=sorted(GEOMS), default='unit')
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    if a.suite:
        for k in range(a.count):
            c, u, m, p, h, s, g = SUITE[k % len(SUITE)]
            info = generate(f'h{k + 1:02d}', a.suite, c, u, m, p, h, s, g, seed=a.seed * 100 + k)
            print(info)
    elif a.name:
        print(generate(a.name, a.outdir, a.cells, a.util, a.macros, a.pads, a.hotspots, a.split, a.geom, a.seed))
    else:
        ap.print_help()


if __name__ == '__main__':
    main()
