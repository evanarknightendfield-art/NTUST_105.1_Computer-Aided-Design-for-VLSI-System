import argparse
import os
import random

from common import INPUT_DIR, LAB2
from gen_input import LIBS, make


def main():
    ap = argparse.ArgumentParser(description='產生額外測資到 inputs/<name>/')
    ap.add_argument('--name', default='holdout')
    ap.add_argument('--count', type=int, default=12)
    ap.add_argument('--seed', type=int, default=2026)
    a = ap.parse_args()

    out_dir = os.path.join(INPUT_DIR, a.name)
    os.makedirs(out_dir, exist_ok=True)
    rng = random.Random(a.seed)
    sizes = [5, 10, 20, 50, 100, 200, 300, 500, 1000, 2000, 5000, 10000]
    for i in range(1, a.count + 1):
        n = sizes[(i - 1) % len(sizes)]
        dim = max(60, int((n ** 0.5) * rng.uniform(25, 60)))
        lscale = round(max(0.5, dim / 110 * rng.uniform(0.35, 1.0)), 2)
        lib = rng.choice(list(LIBS))
        src_f = rng.choice([1, 2, 3, 4])
        src_l = int(0.9 * dim)
        src = rng.choice(['random', 'edge'])
        dist = rng.choice(['uniform', 'cluster'])
        w = rng.choice([1, 1, 2, 3, 5, 10])
        text = make(n, dim, dim, lscale, LIBS[lib], src_f, src_l, src, dist, w, seed=a.seed * 100 + i)
        path = os.path.join(out_dir, f'input{i:03d}.cbi')
        open(path, 'w').write(text)
        print(f'{path}: n={n} chip={dim}x{dim} lscale={lscale} lib={lib} srcF={src_f} srcL={src_l} '
              f'src={src} dist={dist} w={w}')

    official = os.path.join(INPUT_DIR, 'testcases')
    if not os.path.exists(official):
        os.symlink(os.path.join(LAB2, 'testcases'), official)
        print(f'{official} -> ../testcases')


if __name__ == '__main__':
    main()
