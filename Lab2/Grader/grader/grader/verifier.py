#!/usr/bin/env python3
# verifier.py 在 host 的 grader 目錄下執行
# 自動掃當前目錄下的 results/ 每個學號目錄以及 inputs/ 的測資作為輸入
# 並且評分, 輸出每個人的成績，格式為 .csv 放置在當前目錄下
# 
# usage: python3 verifier.py

import sys, os, glob, re
from collections import Counter, deque

def manhattan(a, b):
    return abs(a[0]-b[0]) + abs(a[1]-b[1])

def strip_comment(line):
    return line.split('#', 1)[0].strip()

def parse_input(path):
    lines = open(path, encoding='utf-8', errors='replace').read().splitlines()
    i = 0; limit = {}; buflib = {}; w_skew = None; src = None; sinks = {}
    while i < len(lines):
        s = strip_comment(lines[i])
        if s == '': i += 1; continue
        head = s.split()[0]
        if head == '.limit':
            i += 1
            while i < len(lines):
                s = strip_comment(lines[i]); i += 1
                if s == '.e': break
                if s == '': continue
                t = s.split()
                if t[0] == 'fanout':   limit['fanout'] = int(t[1])
                elif t[0] == 'length': limit['length'] = int(t[1])
                elif t[0] == '.dimx':  limit['dimx']   = int(t[1])
                elif t[0] == '.dimy':  limit['dimy']   = int(t[1])
        elif head == '.buflib':
            i += 1
            while i < len(lines):
                s = strip_comment(lines[i]); i += 1
                if s == '.e': break
                if s == '': continue
                t = s.split()
                buflib[t[0]] = (int(t[1]), int(t[2]), int(t[3]))
        elif head == '.objective':
            i += 1
            while i < len(lines):
                s = strip_comment(lines[i]); i += 1
                if s == '.e': break
                if s == '': continue
                t = s.split()
                if t[0] == 'w_skew': w_skew = int(t[1])
        elif head == '.pin':
            npin = int(s.split()[1]); i += 1
            coords = []
            while i < len(lines):
                s = strip_comment(lines[i]); i += 1
                if s == '.e': break
                if s == '': continue
                t = s.split()
                coords.append((int(t[0]), int(t[1])))
            src = coords[0]
            for k, c in enumerate(coords[1:], start=1):
                sinks[f"S{k}"] = c
        else:
            i += 1
    return limit, buflib, w_skew, src, sinks

def parse_output(path):
    lines = open(path, encoding='utf-8', errors='replace').read().splitlines()
    i = 0; buffers = {}; buf_order = []; levels = []; declared = 0
    while i < len(lines):
        s = strip_comment(lines[i])
        if s == '': i += 1; continue
        head = s.split()[0]
        if head == '.buffer':
            i += 1
            while i < len(lines):
                s = strip_comment(lines[i]); i += 1
                if s == '.e': break
                if s == '': continue
                t = s.split()
                buffers[t[0]] = (t[1], (int(t[2]), int(t[3])))
                buf_order.append(t[0])
        elif head == '.level':
            declared = int(s.split()[1]); i += 1
            while i < len(lines):
                s = strip_comment(lines[i]); i += 1
                if s == '.e': break
                if s == '': continue
                mm = re.match(r'\s*(\d+)\s+(.*)', s)
                k = int(mm.group(1))
                groups = {}
                for pm in re.finditer(r'(\S+?):\{([^}]*)\}', mm.group(2)):
                    groups[pm.group(1)] = pm.group(2).split()
                levels.append((k, groups))
        else:
            i += 1
    return buffers, buf_order, levels, declared

def verify(inp, outp):
    # 拆解 input: SRC 限制, buffer 型號表, w_skew 權重, SRC 座標, sinks 座標
    limit, buflib, w_skew, src, sinks = inp
    # 拆解 output: buffer 表, buffer ID 順序, 各層階層, 宣告的 max level
    buffers, buf_order, levels, declared_level = outp
    dimx, dimy = limit['dimx'], limit['dimy']
    bad = False   # 任一違規就設 True

    # [Verify] buffer ID 必須是連續的 B1..Bm
    if buf_order != [f"B{k}" for k in range(1, len(buf_order)+1)]:
        bad = True
    # [Verify] buffer 型號必須存在於 buflib
    # [Verify] buffer 不能碰到 boundary
    for bid, (btype, (x, y)) in buffers.items():
        if btype not in buflib: bad = True
        if not (0 < x < dimx and 0 < y < dimy): bad = True

    # [Verify] 所有 component 的座標不可重疊
    coord_seen = set()
    for _, c in [('SRC', src)] + list(sinks.items()) + [(b, buffers[b][1]) for b in buffers]:
        if c in coord_seen: bad = True
        coord_seen.add(c)

    # 建立 node name -> coord mapping table
    node_coord = {'SRC': src}; node_coord.update(sinks)
    for b, (_, c) in buffers.items(): node_coord[b] = c
    is_buffer = lambda n: n in buffers
    is_sink   = lambda n: n in sinks

    # 建立 tree: parent -> children
    # child_count: 每個 node 被當 child 的次數
    # child_level: 每個 node 出現在第幾層
    edges = {}; child_count = Counter(); child_level = {}
    for k, groups in levels:
        for parent, children in groups.items():
            # [Verify] 只能是 SRC 或 buffer
            if not (parent == 'SRC' or is_buffer(parent)): bad = True
            # [Verify] level 1 的 parent 必須是 SRC
            if k == 1 and parent != 'SRC': bad = True
            edges.setdefault(parent, [])
            # [Verify] child 只能是 buffer 或 sink
            for ch in children:
                if not (is_buffer(ch) or is_sink(ch)): bad = True
                edges[parent].append(ch)
                child_count[ch] += 1
                child_level[ch] = k

    # [Verify] 每個 buffer/sink 必須剛好被當 child 一次
    # 0 代表未使用的 buffer未使用
    # >1 代表不是 tree
    for n in list(buffers) + list(sinks):
        if child_count[n] != 1: bad = True
    # [Verify] SRC 不能是任何人的 child
    if child_count['SRC'] > 0: bad = True

    # [Verify] 每個 buffer 必須有 children, 不能當 leaf
    for b in buffers:
        if not edges.get(b): bad = True
    # [Verify] sink 不能當 parent
    for parent in edges:
        if is_sink(parent): bad = True

    # 從 SRC 開始做 BFS 計算每個 node 的 depth 和 arrival time
    depth = {'SRC': 0}; arrival = {'SRC': 0}
    q = deque(['SRC']); visited = {'SRC'}
    while q:
        u = q.popleft()
        for v in edges.get(u, []):
            if v in visited: continue
            visited.add(v)
            depth[v] = depth[u] + 1
            arrival[v] = arrival[u] + manhattan(node_coord[u], node_coord[v])
            q.append(v)
    all_nodes = set(buffers) | set(sinks)
    # [Verify] 所有 node 都必須從 SRC 走得到, 沒有不可達的 component
    if all_nodes - visited: bad = True
    # [Verify] BFS 算出的 depth 必須和宣告的層數一致
    for n in all_nodes & visited:
        if child_level.get(n) != depth[n]: bad = True
    # [Verify] 宣告的 .level 必須等於實際最大 depth
    max_depth = max((depth[n] for n in all_nodes & visited), default=0)
    if declared_level != max_depth: bad = True

    # [Verify] 確保每個 parent 的 fanout 和 outgoing length 沒有違反
    for parent, children in edges.items():
        if parent == 'SRC':
            F, L = limit['fanout'], limit['length']
        elif is_buffer(parent) and buffers[parent][0] in buflib:
            F, L, _ = buflib[buffers[parent][0]]
        else:
            continue
        if len(children) > F: bad = True
        tot = sum(manhattan(node_coord[parent], node_coord[c])
                  for c in children if c in node_coord)
        if tot > L: bad = True

    # 有任一違規就是 invalid, 零分, 如果合法就可以算 score
    if bad:
        return False, None, None, None
    t = [arrival[s] for s in sinks]      # 各 sink(leaf) 的 arrival time
    t_max, t_min = max(t), min(t)
    t_skew = t_max - t_min                # skew = T_max - T_min
    buf_cost = sum(buflib[buffers[b][0]][2] for b in buffers)  # 所有 buffer cost 總和
    return True, w_skew * t_skew + buf_cost, t_max, t_min   # Score = w_skew * skew + buf_cost

# 評一位學生: 對 inputs 逐筆驗證, 每筆獨立計分。
# 回 records; 每筆為 (tid, status, score, t_max, t_min)。
def grade_student(inputs, out_dir):
    records = []
    for inp_path in inputs:
        # tid 例如 input001.cbi -> 001, 對應 output001.cbi
        tid = os.path.basename(inp_path)[len("input"):-len(".cbi")]
        out_path = os.path.join(out_dir, f"output{tid}.cbi")

        if not os.path.isfile(out_path):
            records.append((tid, "MISSING", "", "", "")); continue
        try:
            valid, score, t_max, t_min = verify(parse_input(inp_path), parse_output(out_path))
        except Exception:
            records.append((tid, "PARSE_ERROR", "", "", "")); continue
        if not valid:
            records.append((tid, "INVALID", "", "", "")); continue
        records.append((tid, "VALID", score, t_max, t_min))
    return records

def main():
    # 根目錄 = 當前目錄
    input_dir  = "inputs"
    result_dir = "results"

    # 測資列表 (所有學生共用)
    inputs = sorted(glob.glob(os.path.join(input_dir, "input[0-9][0-9][0-9].cbi")))
    if not inputs:
        print(f"error: no testcases in {input_dir}")
        sys.exit(1)

    # results/ 下每個學號各自評分
    students = sorted(d for d in glob.glob(os.path.join(result_dir, "*"))
                      if os.path.isdir(d))
    if not students:
        print(f"error: no student dirs in {result_dir}")
        sys.exit(1)

    summary = []      # (sid, records)
    all_records = []  # (sid, tid, status, score, t_max, t_min)
    for sdir in students:
        sid = os.path.basename(sdir)
        print(f"grading {sid}")   # 只印當前學生
        records = grade_student(inputs, sdir)
        summary.append((sid, records))
        for tid, status, score, t_max, t_min in records:
            all_records.append((sid, tid, status, score, t_max, t_min))

    # 每筆測資結果 -> 當前目錄 record.csv
    with open("record.csv", "w", encoding="utf-8") as f:
        f.write("student_id,testcase,status,score,t_max,t_min\n")
        for sid, tid, status, score, t_max, t_min in all_records:
            f.write(f"{sid},{tid},{status},{score},{t_max},{t_min}\n")

    # 總表 -> 當前目錄 summary.csv
    # 每筆測資一欄: VALID 填 score, 否則填 status; final_score 計算方式待定, 先留空
    tids = [os.path.basename(p)[len("input"):-len(".cbi")] for p in inputs]
    with open("summary.csv", "w", encoding="utf-8") as f:
        f.write("student_id," + ",".join(tids) + ",final_score\n")
        for sid, records in summary:
            cells = [str(score) if status == "VALID" else status
                     for _, status, score, _, _ in records]
            f.write(f"{sid}," + ",".join(cells) + ",\n")

if __name__ == '__main__':
    main()
