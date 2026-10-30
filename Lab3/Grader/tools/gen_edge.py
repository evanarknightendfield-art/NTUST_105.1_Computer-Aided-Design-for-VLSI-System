#!/usr/bin/env python3
"""Generate the `edge` suite: testcases for what the course / holdout / large
testcases do NOT cover, so that tuning on those cannot hide a weakness
(overfitting check).

usage:
  gen_edge.py OUTDIR [--only e01,e05] [--check] [--seed 20261019]

  OUTDIR   e.g. Testcases/edge  ->  OUTDIR/e01_nearlegal/e01_nearlegal.{aux,nodes,nets,wts,pl,scl}
  --check  also write the generator's own legal placement (NAME_ref.pl, kept
           next to the case; dp never reads it) and run check_pl.py on it:
           proves every case is feasible, that the checker reads the geometry
           the way the generator meant it, and gives a yardstick (a placement
           that is known to be legal; a placer doing worse than it on
           displacement is doing badly)

How a case is built (every case is feasible by construction):
  1. rows / subrows / fixed objects  (case-specific geometry)
  2. a LEGAL placement is packed first: cells with random widths fill each free
     segment to the target utilization, gaps spread at random
  3. nets are drawn from cells that are close in that legal placement
  4. the global placement given to dp is derived from the legal one in a
     case-specific way (exact, jitter, smooth distortion, collapse, outside ...)
  5. the files are written with case-specific format quirks

See Testcases/README.md for the list of cases and what each one tests.
"""
import argparse
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))


def fmt(v, dec):
    s = f'{v:.{dec}f}' if dec else f'{int(round(v))}'
    if '.' in s:
        s = s.rstrip('0').rstrip('.')
    return '0' if s in ('-0', '') else s


class Case:
    """Geometry + cells + nets + global placement of one testcase."""

    def __init__(self, name, seed):
        self.name = name
        self.rng = np.random.default_rng(seed)
        self.rows = []        # (y, height, sitewidth, spacing, [(origin, numsites), ...])
        self.fixed = []       # (name, x, y, w, h, kind)  kind: 'terminal' | 'terminal_NI'
        self.fixed_flag = {}  # name -> write /FIXED (or /FIXED_NI) in .pl
        self.cells = []       # (name, w, h)
        self.legal = {}       # name -> (x, y)  the packed legal placement
        self.gp = {}          # name -> (x, y)  global placement written to .pl
        self.orient = {}      # name -> orientation in .pl (default N)
        self.nets = []        # list of (net_name or None, [(node, dir, ox, oy), ...])
        self.dec = 0          # decimals for coordinates
        self.fmt_opts = {}

    # ------------------------------------------------------------- geometry
    def free_segments(self):
        """Free (origin, numsites, spacing, y, height) runs: subrows minus fixed objects."""
        segs = []
        for (y, h, sw, sp, subs) in self.rows:
            for (x0, n) in subs:
                blocked = np.zeros(n, dtype=bool)
                for (_, fx, fy, fw, fh, _) in self.fixed:
                    if fy < y + h - 1e-9 and y < fy + fh - 1e-9:
                        a = int(np.floor((fx - x0) / sp + 1e-9))
                        b = int(np.ceil((fx + fw - x0) / sp - 1e-9))
                        a, b = max(0, min(n, a)), max(0, min(n, b))
                        blocked[a:b] = True
                k = 0
                while k < n:
                    if blocked[k]:
                        k += 1
                        continue
                    j = k
                    while j < n and not blocked[j]:
                        j += 1
                    segs.append((x0 + k * sp, j - k, sp, y, h))
                    k = j
        return segs

    def pack(self, util, widths, wprob, h, prefix='c', limit=None, name_fn=None):
        """Fill every free segment to `util` with cells of `widths` (in units of
        the segment's site spacing times `unit` below), gaps at random."""
        rng = self.rng
        wprob = np.asarray(wprob, float) / np.sum(wprob)
        count = 0
        for (x0, n, sp, y, rh) in self.free_segments():
            target = util * n
            ws = []
            used = 0
            for _ in range(10 * n):
                if used >= target or (limit is not None and count + len(ws) >= limit):
                    break
                w = int(rng.choice(widths, p=wprob))
                if used + w > n:
                    small = [x for x in widths if used + x <= n]
                    if not small:
                        break
                    w = int(min(small))
                ws.append(w)
                used += w
            if not ws:
                continue
            gaps = rng.multinomial(n - used, np.ones(len(ws) + 1) / (len(ws) + 1))
            k = int(gaps[0])
            for i, w in enumerate(ws):
                nm = name_fn(count) if name_fn else f'{prefix}{count}'
                self.cells.append((nm, w * sp, h))
                self.legal[nm] = (x0 + k * sp, y)
                k += w + int(gaps[i + 1])
                count += 1
        return count

    def bbox(self):
        xs = [x0 for (_, _, _, _, subs) in self.rows for (x0, _) in subs]
        xe = [x0 + n * sp for (_, _, _, sp, subs) in self.rows for (x0, n) in subs]
        ys = [r[0] for r in self.rows]
        ye = [r[0] + r[1] for r in self.rows]
        return min(xs), min(ys), max(xe), max(ye)

    def add_pads(self, k, kind='terminal', fixed_flag=False, prefix='p'):
        x0, y0, x1, y1 = self.bbox()
        W, H = x1 - x0, y1 - y0
        for i in range(k):
            t = (i + 0.5) / max(1, k)
            side = i % 4
            x, y = [(x0 + t * W, y1 + 2), (x1 + 3, y0 + t * H), (x0 + t * W, y0 - 3), (x0 - 4, y0 + t * H)][side]
            nm = f'{prefix}{i}'
            self.fixed.append((nm, x, y, 1, 1, kind))
            self.fixed_flag[nm] = fixed_flag

    def add_macros(self, k, wrange, hrange, align=True, prefix='m'):
        """Fixed macros inside the core, not overlapping each other."""
        rng = self.rng
        x0, y0, x1, y1 = self.bbox()
        rh = self.rows[0][1]
        sp = self.rows[0][3]
        placed = []
        for i in range(k):
            for _ in range(500):
                w = int(rng.integers(*wrange)) * sp
                hh = int(rng.integers(*hrange)) * rh
                x = x0 + rng.uniform(0, max(1e-9, x1 - x0 - w))
                if align:
                    x = x0 + round((x - x0) / sp) * sp
                ry = sorted(r[0] for r in self.rows)
                y = ry[int(rng.integers(0, max(1, len(ry) - int(hh / rh))))]
                if not align:
                    y += 0.5 * rh
                if all(x + w <= px or px + pw <= x or y + hh <= py or py + ph <= y for (px, py, pw, ph) in placed):
                    placed.append((x, y, w, hh))
                    nm = f'{prefix}{i}'
                    self.fixed.append((nm, x, y, w, hh, 'terminal'))
                    self.fixed_flag[nm] = True
                    break

    # ------------------------------------------------------------- nets
    def local_nets(self, per_cell=1.05, degrees=(2, 3, 4, 5, 6, 8, 10, 14, 20),
                   dprob=(55, 15, 9, 6, 5, 4, 3, 2, 1), pad_frac=0.04, pos=None):
        """Nets whose pins are near each other in `pos` (default: the legal placement)."""
        rng = self.rng
        pos = pos or self.legal
        names = [c[0] for c in self.cells]
        if not names:
            return
        P = np.array([pos[n] for n in names])
        x0, y0, x1, y1 = self.bbox()
        nb = max(1, int(np.sqrt(len(names) / 8)))
        bx = np.clip(((P[:, 0] - x0) / max(1e-9, x1 - x0) * nb).astype(int), 0, nb - 1)
        by = np.clip(((P[:, 1] - y0) / max(1e-9, y1 - y0) * nb).astype(int), 0, nb - 1)
        buckets = {}
        for i in range(len(names)):
            buckets.setdefault((bx[i], by[i]), []).append(i)
        pads = [f for f in self.fixed if f[3] <= 1 and f[4] <= 1]
        dprob = np.asarray(dprob, float) / np.sum(dprob)
        for _ in range(int(len(names) * per_cell)):
            s = int(rng.integers(len(names)))
            deg = int(rng.choice(degrees, p=dprob))
            pool, rad = [], 0
            while len(pool) < 3 * deg and rad <= nb:
                pool = [j for ix in range(bx[s] - rad, bx[s] + rad + 1)
                        for iy in range(by[s] - rad, by[s] + rad + 1) for j in buckets.get((ix, iy), [])]
                rad += 1
            others = rng.choice(pool, size=min(deg - 1, len(pool)), replace=False)
            pins = [names[s]] + [names[j] for j in others if j != s]
            if pads and rng.random() < pad_frac:
                d = [(p[1] - P[s, 0]) ** 2 + (p[2] - P[s, 1]) ** 2 for p in pads]
                pins.append(pads[int(np.argmin(d))][0])
            if len(pins) >= 2:
                self.nets.append((None, [(p, 'O' if j == 0 else 'I', 0, 0) for j, p in enumerate(pins)]))

    def big_net(self, degree):
        names = [c[0] for c in self.cells]
        pins = list(self.rng.choice(names, size=min(degree, len(names)), replace=False))
        self.nets.append((None, [(p, 'O' if j == 0 else 'I', 0, 0) for j, p in enumerate(pins)]))

    # ------------------------------------------------------------- global placement
    def gp_from_legal(self, jx=0.0, jy=0.0, smooth=0.0, waves=4):
        """GP = legal + uniform jitter (jx sites, jy rows) + a smooth distortion
        field of amplitude `smooth` rows (cells drift together, like the density
        imbalance a real global placer leaves)."""
        rng = self.rng
        x0, y0, x1, y1 = self.bbox()
        rh = self.rows[0][1]
        sp = self.rows[0][3]
        comps = [(rng.uniform(0.5, 2.5), rng.uniform(0.5, 2.5), rng.uniform(0, 6.3), rng.uniform(0, 6.3))
                 for _ in range(waves)]
        for (nm, w, h) in self.cells:
            x, y = self.legal[nm]
            u, v = (x - x0) / max(1e-9, x1 - x0), (y - y0) / max(1e-9, y1 - y0)
            dx = dy = 0.0
            for (a, b, p, q) in comps:
                dx += np.sin(2 * np.pi * a * v + p) * np.cos(2 * np.pi * b * u + q)
                dy += np.cos(2 * np.pi * a * u + q) * np.sin(2 * np.pi * b * v + p)
            dx *= smooth * rh / np.sqrt(waves)
            dy *= smooth * rh / np.sqrt(waves)
            self.gp[nm] = (x + dx + rng.uniform(-jx, jx) * sp, y + dy + rng.uniform(-jy, jy) * rh)

    def clamp_gp(self, margin=0.0):
        x0, y0, x1, y1 = self.bbox()
        for (nm, w, h) in self.cells:
            x, y = self.gp[nm]
            self.gp[nm] = (min(max(x, x0 - margin), x1 - w + margin), min(max(y, y0 - margin), y1 - h + margin))

    # ------------------------------------------------------------- write
    def write(self, outdir, ref=False):
        o = self.fmt_opts
        d = os.path.join(outdir, self.name)
        os.makedirs(d, exist_ok=True)
        nl = '\r\n' if o.get('crlf') else '\n'
        dec = self.dec
        rng = np.random.default_rng(12345)

        def w(path, lines):
            with open(os.path.join(d, path), 'w', newline='') as f:
                f.write(nl.join(lines) + nl)

        n = self.name
        aux = o.get('aux', f'RowBasedPlacement : {n}.nodes {n}.nets {n}.wts {n}.pl {n}.scl')
        w(n + '.aux', [aux])
        lines = ['UCLA nodes 1.0', '# generated by gen_edge.py', '']
        lines += [f'NumNodes : {len(self.cells) + len(self.fixed)}', f'NumTerminals : {len(self.fixed)}']
        for (nm, cw, ch) in self.cells:
            lines.append(f'\t{nm}\t{fmt(cw, dec)}\t{fmt(ch, dec)}')
        for (nm, x, y, fw, fh, kind) in self.fixed:
            lines.append(f'\t{nm}\t{fmt(fw, dec)}\t{fmt(fh, dec)}\t{kind}')
        if o.get('comments'):
            lines.insert(len(lines) // 2, '# a comment in the middle of the node list')
        w(n + '.nodes', lines)

        lines = ['UCLA nets 1.0', '', f'NumNets : {len(self.nets)}', f'NumPins : {sum(len(p) for _, p in self.nets)}']
        for k, (nn, pins) in enumerate(self.nets):
            named = o.get('net_names', 'mixed')
            label = nn or f'net{k}'
            show = named == 'all' or (named == 'mixed' and k % 2 == 0)
            lines.append(f'NetDegree : {len(pins)}' + (f'   {label}' if show else ''))
            for (p, dr, ox, oy) in pins:
                lines.append(f'\t{p}\t{dr} : {fmt(ox, 4)} {fmt(oy, 4)}')
            if o.get('comments') and k == len(self.nets) // 2:
                lines.append('# comment between nets')
        w(n + '.nets', lines)

        lines = ['UCLA wts 1.0', '']
        lines += [f'{nm} 1' for (nm, _, _) in self.cells] + [f'{f[0]} 1' for f in self.fixed]
        w(n + '.wts', lines)

        def pl_lines(pos):
            out = ['UCLA pl 1.0', '# generated by gen_edge.py', '']
            for (nm, _, _) in self.cells:
                x, y = pos[nm]
                out.append(f'{nm}\t{fmt(x, dec + 3)}\t{fmt(y, dec + 3)}\t: {self.orient.get(nm, "N")}')
            for (nm, x, y, fw, fh, kind) in self.fixed:
                flag = (' /FIXED_NI' if kind == 'terminal_NI' else ' /FIXED') if self.fixed_flag.get(nm) else ''
                out.append(f'{nm}\t{fmt(x, dec)}\t{fmt(y, dec)}\t: {self.orient.get(nm, "N")}{flag}')
            return out
        w(n + '.pl', pl_lines(self.gp))
        if ref:
            w(n + '_ref.pl', pl_lines(self.legal))

        so, ss = o.get('siteorient', '1'), o.get('sitesymmetry', '1')
        nk = o.get('numsites_kw', 'NumSites')
        lines = ['UCLA scl 1.0', '', f'NumRows : {len(self.rows)}', '']
        order = list(range(len(self.rows)))
        if o.get('shuffle_rows'):
            rng.shuffle(order)
        for i in order:
            (y, h, sw, sp, subs) = self.rows[i]
            lines += ['CoreRow Horizontal', f'  Coordinate    : {fmt(y, dec)}', f'  Height        : {fmt(h, dec)}',
                      f'  Sitewidth     : {fmt(sw, dec)}', f'  Sitespacing   : {fmt(sp, dec)}',
                      f'  Siteorient    : {so}', f'  Sitesymmetry  : {ss}']
            for (x0, ns) in subs:
                lines.append(f'  SubrowOrigin  : {fmt(x0, dec + 2)}\t{nk} : {ns}')
            lines.append('End')
        w(n + '.scl', lines)
        return d


def uniform_rows(c, nrows, nsites, sw, rh, ox=0.0, oy=0.0, split=0.0, gap=0.03):
    rng = c.rng
    for r in range(nrows):
        y = oy + r * rh
        if rng.random() < split:
            g = max(2, int(gap * nsites))
            a = int(rng.integers(nsites // 4, 3 * nsites // 4))
            c.rows.append((y, rh, sw, sw, [(ox, a), (ox + (a + g) * sw, nsites - a - g)]))
        else:
            c.rows.append((y, rh, sw, sw, [(ox, nsites)]))


WIDTHS = [2, 3, 4, 5, 6, 8, 10, 12, 14, 16, 20, 28]
WPROB = [12, 14, 16, 10, 12, 10, 8, 6, 4, 4, 2, 2]


def e01_nearlegal(c):
    """Real-placer-like input: a legal placement jittered by +-1.5 sites, +-0.25 rows."""
    uniform_rows(c, 90, 900, 1, 16, split=0.1)
    c.add_macros(3, (20, 60), (3, 7))
    c.add_pads(60)
    c.pack(0.85, WIDTHS, WPROB, 16)
    c.local_nets()
    c.gp_from_legal(jx=1.5, jy=0.25)


def e02_legalgp(c):
    """The global placement is ALREADY legal: the ideal Stage 1 moves nothing."""
    uniform_rows(c, 60, 700, 66, 504, ox=-20000, oy=-15000, split=0.2)
    c.add_macros(2, (15, 40), (2, 5))
    c.add_pads(40)
    c.dec = 0
    c.pack(0.75, WIDTHS, WPROB, 504)
    c.local_nets()
    c.gp = dict(c.legal)


def e03_dense97(c):
    """97 % of the free sites are used: barely legal; smooth distortion of 4 rows."""
    uniform_rows(c, 70, 600, 1, 16, split=0.2)
    c.add_macros(4, (15, 50), (2, 6))
    c.add_pads(40)
    c.pack(0.97, WIDTHS, WPROB, 16)
    c.local_nets()
    c.gp_from_legal(jx=2, jy=0.3, smooth=4)
    c.clamp_gp()


def e04_sparse10(c):
    """10 % utilization; global placement uniformly random (no structure)."""
    uniform_rows(c, 50, 2000, 0.2, 1.6, ox=10, oy=10)
    c.dec = 3
    c.add_pads(30)
    c.pack(0.10, WIDTHS, WPROB, 1.6)
    x0, y0, x1, y1 = c.bbox()
    for (nm, w, h) in c.cells:
        c.gp[nm] = (c.rng.uniform(x0, x1 - w), c.rng.uniform(y0, y1 - h))
    c.local_nets(pos=c.gp)


def e05_bignets(c):
    """Huge nets (clock / reset-like: 2000, 800, 300 pins) plus 50 nets of 50 pins."""
    uniform_rows(c, 100, 1000, 1, 16, split=0.1)
    c.add_macros(2, (20, 50), (3, 6))
    c.add_pads(80)
    c.pack(0.80, WIDTHS, WPROB, 16)
    c.local_nets()
    for d in (2000, 800, 300):
        c.big_net(d)
    for _ in range(50):
        c.big_net(50)
    c.gp_from_legal(jx=2, jy=0.3, smooth=2)
    c.clamp_gp()


def e06_tallnarrow(c):
    """800 rows of only 40 sites; some cells are 28 sites wide (70 % of a row)."""
    uniform_rows(c, 800, 40, 1, 16)
    c.add_pads(40)
    c.pack(0.75, WIDTHS, WPROB, 16)
    c.local_nets()
    c.gp_from_legal(jx=3, jy=0.4, smooth=6)
    c.clamp_gp()


def e07_tworows(c):
    """Only 2 rows, 12000 sites each: no room to move vertically at all."""
    uniform_rows(c, 2, 12000, 1, 16)
    c.add_pads(20)
    c.pack(0.80, WIDTHS, WPROB, 16)
    c.local_nets()
    for (nm, w, h) in c.cells:
        x, y = c.legal[nm]
        c.gp[nm] = (x + c.rng.normal(0, 40), c.rng.uniform(0, 16))
    c.clamp_gp()


def e08_fragmented(c):
    """40 fixed macros cut the rows into many short segments, some narrower than
    the widest cells; some global positions lie inside macros."""
    uniform_rows(c, 120, 600, 1, 16, split=0.3)
    c.add_macros(40, (4, 40), (1, 8))
    c.add_pads(40)
    c.pack(0.70, WIDTHS, WPROB, 16)
    c.local_nets()
    c.gp_from_legal(jx=4, jy=0.5, smooth=3)
    c.clamp_gp()


def e09_offgrid(c):
    """Every row's origin is shifted by a different fraction of a site (no common
    x grid), site 0.19, row 1.71, 4 decimals; 5 % of the cells start far outside
    the core."""
    c.dec = 4
    sw, rh = 0.19, 1.71
    for r in range(80):
        off = ((r * 0.37) % 1.0) * sw
        c.rows.append((3.3 + r * rh, rh, sw, sw, [(7.7 + off, 900)]))
    c.add_pads(40)
    c.pack(0.80, WIDTHS, WPROB, rh)
    c.local_nets()
    c.gp_from_legal(jx=2, jy=0.3, smooth=2)
    x0, y0, x1, y1 = c.bbox()
    for (nm, w, h) in c.cells:
        if c.rng.random() < 0.05:
            side = c.rng.integers(4)
            x, y = c.gp[nm]
            dx, dy = 0.2 * (x1 - x0) * c.rng.random(), 0.2 * (y1 - y0) * c.rng.random()
            c.gp[nm] = [(x0 - w - dx, y), (x1 + dx, y), (x, y0 - h - dy), (x, y1 + dy)][side]


def e10_mixedsites(c):
    """Rows alternate site spacing 2 and 3 (sitewidth = spacing); cell widths are
    multiples of 6 so they fit both. No single site grid for the whole chip."""
    for r in range(60):
        sp = 2 if r % 2 == 0 else 3
        n = 3000 // sp
        c.rows.append((r * 16, 16, sp, sp, [(0, n)]))
    c.add_pads(30)
    # widths in sites differ per row; pack with widths given in sites of each row:
    # cell width 6k -> 3k sites (spacing 2) or 2k sites (spacing 3)
    rng = c.rng
    count = 0
    for (x0, n, sp, y, h) in c.free_segments():
        ws, used = [], 0
        while used < 0.7 * n:
            k = int(rng.choice([1, 2, 3, 4], p=[0.4, 0.3, 0.2, 0.1]))
            wsite = 6 * k // sp
            if used + wsite > n:
                break
            ws.append(wsite)
            used += wsite
        gaps = rng.multinomial(n - used, np.ones(len(ws) + 1) / (len(ws) + 1))
        pos = int(gaps[0])
        for i, ws_ in enumerate(ws):
            nm = f'c{count}'
            c.cells.append((nm, ws_ * sp, 16))
            c.legal[nm] = (x0 + pos * sp, y)
            pos += ws_ + int(gaps[i + 1])
            count += 1
    c.local_nets()
    c.gp_from_legal(jx=2, jy=0.4, smooth=2)
    c.clamp_gp()


def e11_format(c):
    """Format robustness: CRLF line ends, comments in the middle, pin offsets and
    B pins, a pin listed twice, 1-pin nets, cells on no net, names with / [ ] .,
    movable cells in FS / FN, Siteorient N / Sitesymmetry Y, Numsites spelling,
    ISPD-2006 style terminal_NI pads with /FIXED_NI, pads without /FIXED."""
    uniform_rows(c, 40, 500, 1, 12, split=0.25)
    c.add_macros(2, (10, 30), (2, 4))
    c.add_pads(24)
    c.add_pads(12, kind='terminal_NI', fixed_flag=True, prefix='ni')
    c.pack(0.70, WIDTHS, WPROB, 12, name_fn=lambda i: f'u{i // 50}/blk[{i % 50}].g{i}')
    c.local_nets()
    rng = c.rng
    nets = []
    for k, (nn, pins) in enumerate(c.nets):
        new = [(p, 'B' if rng.random() < 0.1 else d, round(rng.uniform(-3, 3), 2), round(rng.uniform(-5, 5), 2))
               for (p, d, _, _) in pins]
        if k % 37 == 0:
            new.append(new[-1])                          # the same pin twice
        nets.append((nn, new))
    names = [x[0] for x in c.cells]
    for k in range(25):
        nets.append((f'single{k}', [(names[int(rng.integers(len(names)))], 'I', 0, 0)]))
    # every 20th cell is on no net at all (its pins are dropped; nets left with < 1 pin go)
    lonely = set(names[::20])
    nets = [(nn, [x for x in pins if x[0] not in lonely]) for (nn, pins) in nets]
    c.nets = [(nn, pins) for (nn, pins) in nets if pins]
    for (nm, w, h) in c.cells:
        r = rng.random()
        if r < 0.15:
            c.orient[nm] = 'FS'
        elif r < 0.25:
            c.orient[nm] = 'FN'
    for f in c.fixed:
        if f[0].startswith('p'):
            c.fixed_flag[f[0]] = (int(f[0][1:]) % 2 == 0)   # half of the pads without /FIXED
    c.gp_from_legal(jx=2, jy=0.3, smooth=1.5)
    c.clamp_gp()
    c.fmt_opts = {'crlf': True, 'comments': True, 'siteorient': 'N', 'sitesymmetry': 'Y',
                  'numsites_kw': 'Numsites', 'net_names': 'mixed',
                  'aux': f'RowBasedPlacement  :  {c.name}.nodes  {c.name}.nets  {c.name}.wts  {c.name}.pl  {c.name}.scl  '}


def e12_collapse(c):
    """70 % of the cells start at exactly the same 3 points (identical coordinates)."""
    uniform_rows(c, 60, 700, 1, 16)
    c.add_pads(30)
    c.pack(0.60, WIDTHS, WPROB, 16)
    c.local_nets()
    c.gp_from_legal(jx=1, jy=0.2)
    x0, y0, x1, y1 = c.bbox()
    pts = [(x0 + 0.2 * (x1 - x0), y0 + 0.3 * (y1 - y0)), (x0 + 0.7 * (x1 - x0), y0 + 0.6 * (y1 - y0)),
           (x0 + 0.5 * (x1 - x0), y0 + 0.9 * (y1 - y0) - 16)]
    for i, (nm, w, h) in enumerate(c.cells):
        if c.rng.random() < 0.7:
            c.gp[nm] = pts[i % 3]


def e13_onecell(c):
    """One movable cell, one row of 20 sites, two pads, one net."""
    c.rows.append((0, 10, 1, 1, [(0, 20)]))
    c.cells.append(('only', 3, 10))
    c.legal['only'] = (5, 0)
    c.gp['only'] = (7.37, 4.2)
    c.fixed += [('pa', -5, 3, 1, 1, 'terminal'), ('pb', 30, 3, 1, 1, 'terminal')]
    c.fixed_flag.update({'pa': True, 'pb': False})
    c.nets.append(('n0', [('only', 'O', 0, 0), ('pa', 'I', 0, 0), ('pb', 'I', 0, 0)]))


def e14_fixedonly(c):
    """No movable cell at all: three fixed objects, two rows, nets between pads."""
    c.rows += [(0, 10, 1, 1, [(0, 50)]), (10, 10, 1, 1, [(0, 50)])]
    c.fixed += [('blk', 10, 0, 8, 20, 'terminal'), ('pa', -3, 5, 1, 1, 'terminal'), ('pb', 60, 15, 1, 1, 'terminal')]
    c.fixed_flag.update({'blk': True, 'pa': True, 'pb': True})
    c.nets.append(('n0', [('pa', 'O', 0, 0), ('pb', 'I', 0, 0), ('blk', 'I', 0, 0)]))


def e15_realistic(c):
    """Larger and closest to a real global placement: legal placement with a
    smooth density distortion (regions 20 - 30 % over-full), local nets with a
    few big ones, macros and 400 I/O pads, IBM geometry."""
    uniform_rows(c, 170, 1600, 66, 504, ox=-50000, oy=-40000, split=0.1)
    c.add_macros(5, (20, 80), (3, 10))
    c.add_pads(400)
    c.pack(0.82, WIDTHS, WPROB, 504)
    c.local_nets()
    for d in (600, 200, 100):
        c.big_net(d)
    c.gp_from_legal(jx=3, jy=0.4, smooth=5, waves=6)
    c.clamp_gp()


def e16_rowgaps(c):
    """Rows are NOT evenly spaced (channels of 0.5 - 3 row heights between groups
    of rows) and the .scl lists them in random order."""
    y = 0.0
    r = 0
    while r < 90:
        group = int(c.rng.integers(3, 12))
        for _ in range(group):
            c.rows.append((y, 16, 1, 1, [(0, 800)]))
            y += 16
            r += 1
        y += 16 * float(c.rng.choice([0.5, 1, 2, 3]))
    c.add_pads(40)
    c.pack(0.75, WIDTHS, WPROB, 16)
    c.local_nets()
    c.gp_from_legal(jx=2, jy=0.6, smooth=3)
    c.clamp_gp()
    c.fmt_opts = {'shuffle_rows': True}


CASES = [e01_nearlegal, e02_legalgp, e03_dense97, e04_sparse10, e05_bignets, e06_tallnarrow, e07_tworows,
         e08_fragmented, e09_offgrid, e10_mixedsites, e11_format, e12_collapse, e13_onecell, e14_fixedonly,
         e15_realistic, e16_rowgaps]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('outdir')
    ap.add_argument('--only', default='')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--seed', type=int, default=2026_1019, help='base RNG seed; default preserves the original edge suite byte-for-byte')
    a = ap.parse_args()
    want = [x for x in a.only.split(',') if x]
    for k, fn in enumerate(CASES):
        tag = fn.__name__
        if want and not any(tag.startswith(w) for w in want):
            continue
        c = Case(tag, seed=a.seed + k)
        fn(c)
        d = c.write(a.outdir, ref=a.check)
        info = f'{tag}: {len(c.cells)} cells, {len(c.nets)} nets, {len(c.rows)} rows, {len(c.fixed)} fixed'
        if a.check:
            p = subprocess.run([sys.executable, os.path.join(HERE, 'check_pl.py'), os.path.join(d, tag),
                                os.path.join(d, tag + '_ref.pl')], capture_output=True, text=True)
            out = p.stdout.strip().split('\n')
            info += f'  reference placement: {out[0]}  ' + '  '.join(x.strip() for x in out[1:4])
        print(info)


if __name__ == '__main__':
    main()
