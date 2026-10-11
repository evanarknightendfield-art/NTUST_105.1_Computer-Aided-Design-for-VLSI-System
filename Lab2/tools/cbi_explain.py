#!/usr/bin/env python3
"""
cbi_explain.py — 逐步解釋一個 CBI 解答（教學用，比 verifier 更囉嗦）

usage: python cbi_explain.py INPUT.cbi OUTPUT.cbi

會印出：
  1. 每個 parent 的 children、每條邊的 Manhattan 距離
  2. fanout / length 使用量 vs. 上限（違規會標 ✗）
  3. 每個 sink 的路徑與 arrival time
  4. T_max, T_min, skew, buffer cost, Score
"""
import re
import sys
from collections import deque


def strip(line):
    return line.split('#', 1)[0].strip()


def parse_input(path):
    lim, lib, w, pins = {}, {}, 1, []
    sec = None
    for raw in open(path, encoding='utf-8'):
        s = strip(raw)
        if not s:
            continue
        t = s.split()
        if t[0] == '.e':
            sec = None
        elif t[0] in ('.limit', '.buflib', '.objective', '.pin'):
            sec = t[0]
        elif t[0] in ('.dimx', '.dimy'):
            lim[t[0][1:]] = int(t[1])
        elif sec == '.limit':
            lim[t[0]] = int(t[1])
        elif sec == '.buflib':
            lib[t[0]] = (int(t[1]), int(t[2]), int(t[3]))
        elif sec == '.objective' and t[0] == 'w_skew':
            w = int(t[1])
        elif sec == '.pin':
            pins.append((int(t[0]), int(t[1])))
    pos = {'SRC': pins[0]}
    for i, p in enumerate(pins[1:], 1):
        pos[f'S{i}'] = p
    return lim, lib, w, pos


def parse_output(path):
    bufs, edges, declared = {}, {}, None
    sec = None
    for raw in open(path, encoding='utf-8'):
        s = strip(raw)
        if not s:
            continue
        t = s.split()
        if t[0] == '.e':
            sec = None
        elif t[0] == '.buffer':
            sec = 'buf'
        elif t[0] == '.level':
            sec, declared = 'lvl', int(t[1])
        elif sec == 'buf':
            bufs[t[0]] = (t[1], int(t[2]), int(t[3]))
        elif sec == 'lvl':
            for par, kids in re.findall(r'(\S+?):\{([^}]*)\}', s):
                edges.setdefault(par, []).extend(kids.split())
    return bufs, edges, declared


def d(a, b):
    return abs(a[0] - b[0]) + abs(a[1] - b[1])


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    lim, lib, w, pos = parse_input(sys.argv[1])
    bufs, edges, declared = parse_output(sys.argv[2])
    for b, (_, x, y) in bufs.items():
        pos[b] = (x, y)

    print(f"chip = {lim['dimx']} x {lim['dimy']},  SRC limit: F={lim['fanout']} L={lim['length']},  w_skew={w}")
    print("buffer library:", ", ".join(f"{k}(F={v[0]},L={v[1]},C={v[2]})" for k, v in lib.items()))
    print()

    ok = True
    print("── 每個 parent 的負載 ──")
    for par, kids in edges.items():
        if par == 'SRC':
            F, L, tag = lim['fanout'], lim['length'], 'SRC'
        else:
            bt = bufs[par][0]
            F, L, _ = lib[bt]
            tag = f"{par}({bt})"
        parts = [f"{k}:{d(pos[par], pos[k])}" for k in kids]
        tot = sum(d(pos[par], pos[k]) for k in kids)
        fo = '✓' if len(kids) <= F else '✗'
        lo = '✓' if tot <= L else '✗'
        ok &= (fo == '✓' and lo == '✓')
        print(f"  {tag:<12} @{pos[par]}  children {parts}")
        print(f"  {'':<12} fanout {len(kids)}/{F} {fo}   length {tot}/{L} {lo}")
    print()

    # BFS 算 arrival time
    arr, path = {'SRC': 0}, {'SRC': ['SRC']}
    q = deque(['SRC'])
    while q:
        u = q.popleft()
        for v in edges.get(u, []):
            arr[v] = arr[u] + d(pos[u], pos[v])
            path[v] = path[u] + [v]
            q.append(v)

    print("── 每個 sink 的 arrival time ──")
    sinks = [k for k in pos if k.startswith('S') and k != 'SRC']
    for s in sorted(sinks, key=lambda k: int(k[1:])):
        segs = [d(pos[a], pos[b]) for a, b in zip(path[s], path[s][1:])]
        print(f"  {s:<4} T = {' + '.join(map(str, segs))} = {arr[s]:<5}  path {' -> '.join(path[s])}")
    t = [arr[s] for s in sinks]
    tmax, tmin = max(t), min(t)
    cost = sum(lib[bt][2] for bt, _, _ in bufs.values())
    print()
    print(f"T_max = {tmax},  T_min = {tmin},  skew = {tmax - tmin}")
    print(f"buffer cost = {' + '.join(str(lib[bt][2]) for bt, _, _ in bufs.values()) or '0'} = {cost}")
    print(f"Score = cost + w_skew*skew = {cost} + {w}*{tmax - tmin} = {cost + w * (tmax - tmin)}")
    print(f"constraints: {'ALL OK' if ok else 'VIOLATION!'}   (declared .level = {declared})")


if __name__ == '__main__':
    main()
