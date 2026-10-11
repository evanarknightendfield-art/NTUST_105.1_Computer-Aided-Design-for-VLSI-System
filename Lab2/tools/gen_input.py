#!/usr/bin/env python3
"""
gen_input.py — 產生 CBI 測資

usage examples:
  python gen_input.py -n 100 --dim 300 -o input002.cbi
  python gen_input.py -n 2000 --dimx 2000 --dimy 1500 --lscale 4 --dist cluster -w 5 -o input010.cbi
  python gen_input.py --suite DIR       # 產生一整組不同情境的測資 input001..input0XX

參數說明
  -n          sink 數
  --dim       晶片邊長（dimx = dimy），或用 --dimx / --dimy 分別指定
  --lscale    buffer library 的 L 相對於範例的倍數（範例 L = 90/120/190, 晶片 ~110x100）
  --lib       default（範例的 3 種）或 wide（5 種，含很小與很大的）
  --srcF / --srcL   SRC 的 fanout / length 上限（srcL 預設 = 0.9 * 晶片邊長）
  --src       random（任意位置）或 edge（貼近邊界）
  --dist      uniform 或 cluster（數個高斯群聚）
  -w          w_skew
"""
import argparse
import os
import random


LIBS = {
    'default': [('SMALL', 3, 90, 10), ('MEDIUM', 6, 120, 16), ('LARGE', 9, 190, 22)],
    'wide': [('TINY', 2, 40, 6), ('SMALL', 3, 90, 10), ('MEDIUM', 6, 120, 16),
             ('LARGE', 9, 190, 22), ('HUGE', 16, 400, 45)],
}


def make(n, dimx, dimy, lscale, lib, srcF, srcL, src_mode, dist, w, seed):
    rng = random.Random(seed)
    used = set()

    def free_point(x, y):
        x = min(max(int(round(x)), 0), dimx)
        y = min(max(int(round(y)), 0), dimy)
        while (x, y) in used:
            x = min(max(x + rng.randint(-1, 1), 0), dimx)
            y = min(max(y + rng.randint(-1, 1), 0), dimy)
        used.add((x, y))
        return x, y

    if src_mode == 'edge':
        side = rng.randrange(4)
        if side == 0:
            src = free_point(0, rng.randint(0, dimy))
        elif side == 1:
            src = free_point(dimx, rng.randint(0, dimy))
        elif side == 2:
            src = free_point(rng.randint(0, dimx), 0)
        else:
            src = free_point(rng.randint(0, dimx), dimy)
    else:
        src = free_point(rng.randint(0, dimx), rng.randint(0, dimy))

    sinks = []
    if dist == 'cluster':
        k = max(2, int(n ** 0.5) // 3)
        centers = [(rng.uniform(0.1, 0.9) * dimx, rng.uniform(0.1, 0.9) * dimy) for _ in range(k)]
        sd = 0.06 * min(dimx, dimy)
        for _ in range(n):
            cx, cy = rng.choice(centers)
            sinks.append(free_point(rng.gauss(cx, sd), rng.gauss(cy, sd)))
    else:
        for _ in range(n):
            sinks.append(free_point(rng.uniform(0, dimx), rng.uniform(0, dimy)))

    out = []
    out.append('.limit')
    out.append(f'fanout {srcF}\t\t# maximum fanouts for SRC')
    out.append(f'length {srcL}\t# maximum length for SRC')
    out.append(f'.dimx {dimx}\t# chip dimension in x-axis')
    out.append(f'.dimy {dimy}\t# chip dimension in y-axis')
    out.append('.e')
    out.append('')
    out.append(f'.buflib {len(lib)}')
    out.append('# type\t#F\t#L\t#C #fanout/length/cost')
    for name, F, L, C in lib:
        out.append(f'{name}\t{F}\t{max(1, int(round(L * lscale)))}\t{C}')
    out.append('.e')
    out.append('')
    out.append('.objective')
    out.append(f'w_skew {w}')
    out.append('.e')
    out.append('')
    out.append(f'.pin {n + 1}\t\t# number of pins')
    out.append(f'{src[0]} {src[1]}\t\t# clock SRC coordinates')
    for i, (x, y) in enumerate(sinks, 1):
        out.append(f'{x} {y}\t\t# clock sink S{i} coordinates')
    out.append('.e')
    return '\n'.join(out) + '\n'


# 一組涵蓋各種情境的測資：(n, dimx, dimy, lscale, lib, srcF, srcL(None=自動), src, dist, w)
SUITE = [
    (5, 110, 100, 1.0, 'default', 2, 100, 'random', 'uniform', 1),     # 002 範例規模
    (20, 200, 200, 1.0, 'default', 2, 180, 'edge', 'uniform', 1),      # 003
    (50, 300, 300, 1.0, 'default', 2, None, 'random', 'uniform', 2),   # 004
    (100, 500, 500, 1.0, 'default', 3, None, 'edge', 'uniform', 1),    # 005 稀疏（需要 relay）
    (200, 400, 400, 1.5, 'wide', 2, None, 'random', 'cluster', 5),     # 006 群聚 + wide lib
    (500, 800, 800, 2.0, 'default', 4, None, 'edge', 'uniform', 1),    # 007
    (1000, 1000, 1000, 2.0, 'default', 2, None, 'random', 'cluster', 3),   # 008
    (2000, 2000, 1500, 3.0, 'wide', 2, None, 'edge', 'uniform', 1),    # 009
    (5000, 3000, 3000, 4.0, 'default', 3, None, 'random', 'uniform', 10),  # 010 w_skew 很大
    (10000, 5000, 5000, 5.0, 'default', 2, None, 'edge', 'cluster', 1),    # 011
    (30, 1000, 1000, 1.0, 'default', 1, 60, 'random', 'uniform', 1),   # 012 很稀疏、SRC 很弱
    (300, 120, 120, 1.0, 'default', 2, 100, 'random', 'uniform', 1),   # 013 很密集（座標容易撞）
    (1, 50, 50, 1.0, 'default', 1, 10, 'random', 'uniform', 1),        # 014 只有 1 個 sink
    (8, 100, 100, 1.0, 'default', 9, 1000, 'random', 'uniform', 1),    # 015 SRC 可以直接接全部
]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-n', type=int, default=50)
    ap.add_argument('--dim', type=int)
    ap.add_argument('--dimx', type=int, default=300)
    ap.add_argument('--dimy', type=int, default=300)
    ap.add_argument('--lscale', type=float, default=1.0)
    ap.add_argument('--lib', choices=list(LIBS), default='default')
    ap.add_argument('--srcF', type=int, default=2)
    ap.add_argument('--srcL', type=int)
    ap.add_argument('--src', choices=['random', 'edge'], default='random')
    ap.add_argument('--dist', choices=['uniform', 'cluster'], default='uniform')
    ap.add_argument('-w', type=int, default=1)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('-o', '--out')
    ap.add_argument('--suite', metavar='DIR', help='產生整組測資到 DIR')
    a = ap.parse_args()

    if a.suite:
        os.makedirs(a.suite, exist_ok=True)
        for i, (n, dx, dy, ls, lib, sF, sL, src, dist, w) in enumerate(SUITE, start=2):
            if sL is None:
                sL = int(0.9 * max(dx, dy))
            txt = make(n, dx, dy, ls, LIBS[lib], sF, sL, src, dist, w, seed=1000 + i)
            path = os.path.join(a.suite, f'input{i:03d}.cbi')
            open(path, 'w').write(txt)
            print(f'{path}: n={n} chip={dx}x{dy} lscale={ls} lib={lib} srcF={sF} srcL={sL} src={src} dist={dist} w={w}')
        return

    if a.dim:
        a.dimx = a.dimy = a.dim
    srcL = a.srcL if a.srcL is not None else int(0.9 * max(a.dimx, a.dimy))
    txt = make(a.n, a.dimx, a.dimy, a.lscale, LIBS[a.lib], a.srcF, srcL, a.src, a.dist, a.w, a.seed)
    if a.out:
        open(a.out, 'w').write(txt)
    else:
        print(txt, end='')


if __name__ == '__main__':
    main()
