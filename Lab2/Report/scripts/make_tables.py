import argparse
import csv
import math
import os

from common import INPUT_DIR, RESULT_DIR, TABLE_DIR, VERSIONS, case_ids, lower_bounds, parse_log


def load(set_name):
    in_dir = os.path.join(INPUT_DIR, set_name)
    data = {}
    for cid in case_ids(in_dir):
        lb = lower_bounds(os.path.join(in_dir, f'input{cid}.cbi'))
        row = dict(case=cid, n=lb[3], w=lb[4], cost_lb=lb[0], skew_lb=lb[1], score_lb=lb[2], v={})
        for tag, _, _ in VERSIONS:
            log = os.path.join(RESULT_DIR, set_name, tag, f'log{cid}.txt')
            if os.path.exists(log):
                row['v'][tag] = parse_log(log)
        data[cid] = row
    status = {}
    summary = os.path.join(RESULT_DIR, set_name, 'summary.csv')
    if os.path.exists(summary):
        for r in csv.DictReader(open(summary)):
            status[(r['version'], r['case'])] = r
    return data, status


def stage_val(row, tag, stage, key):
    st = row['v'].get(tag, ({}, None))[0]
    return int(st[stage][key]) if stage in st and key in st[stage] else None


def final_score(row, tag):
    final = row['v'].get(tag, ({}, None))[1]
    return final['score'] if final else None


def write(path, header, rows):
    with open(path + '.csv', 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)
    with open(path + '.md', 'w') as f:
        f.write('| ' + ' | '.join(header) + ' |\n')
        f.write('|' + '---|' * len(header) + '\n')
        for r in rows:
            f.write('| ' + ' | '.join('' if c is None else str(c) for c in r) + ' |\n')
    print('wrote', path + '.md')


def geomean(xs):
    xs = [x for x in xs if x and x > 0]
    return math.exp(sum(math.log(x) for x in xs) / len(xs)) if xs else None


def main():
    ap = argparse.ArgumentParser(description='把 results/<set>/ 整理成表格到 tables/<set>/')
    ap.add_argument('--set', default='testcases')
    ap.add_argument('--final', default=VERSIONS[-1][0], help='「各階段」表格用哪個版本')
    a = ap.parse_args()

    data, status = load(a.set)
    out = os.path.join(TABLE_DIR, a.set)
    os.makedirs(out, exist_ok=True)
    tags = [t for t, _, _ in VERSIONS]
    labels = {t: l for t, l, _ in VERSIONS}

    header = ['case', 'N', 'w_skew',
              'S1 buffers', 'S1 cost', 'S1 skew', 'S1 Score',
              'S2 buffers', 'S2 cost', 'S2 skew', 'S2 Score', 'improve %', 'time s']
    rows = []
    for cid, r in data.items():
        s1 = [stage_val(r, a.final, 'stage1', k) for k in ('buffers', 'cost', 'skew', 'score')]
        s2 = [stage_val(r, a.final, 'stage2', k) for k in ('buffers', 'cost', 'skew', 'score')]
        imp = round(100 * (s1[3] - s2[3]) / s1[3], 1) if s1[3] and s2[3] is not None else None
        sec = status.get((a.final, cid), {}).get('seconds')
        rows.append([cid, r['n'], r['w']] + s1 + s2 + [imp, sec])
    write(os.path.join(out, 'stages'), header, rows)

    header = ['case', 'N', 'w_skew'] + [labels[t] for t in tags] + ['status']
    rows, ratios = [], {t: [] for t in tags}
    for cid, r in data.items():
        scores = [final_score(r, t) for t in tags]
        st = {status.get((t, cid), {}).get('status', '-') for t in tags}
        rows.append([cid, r['n'], r['w']] + scores + ['/'.join(sorted(st))])
        for t, s in zip(tags, scores):
            if s and scores[0]:
                ratios[t].append(s / scores[0])
    rows.append(['geomean vs ' + labels[tags[0]], '', ''] +
                [None if geomean(ratios[t]) is None else round(geomean(ratios[t]), 3) for t in tags] + [''])
    write(os.path.join(out, 'versions'), header, rows)

    header = ['case', 'N', 'w_skew', 'cost LB', 'skew LB', 'Score LB',
              'final cost', 'final skew', 'final Score', 'Score / LB']
    rows = []
    for cid, r in data.items():
        cost = stage_val(r, a.final, 'stage2', 'cost')
        skew = stage_val(r, a.final, 'stage2', 'skew')
        score = final_score(r, a.final)
        ratio = round(score / r['score_lb'], 2) if score and r['score_lb'] else None
        rows.append([cid, r['n'], r['w'], r['cost_lb'], r['skew_lb'], r['score_lb'], cost, skew, score, ratio])
    write(os.path.join(out, 'bounds'), header, rows)


if __name__ == '__main__':
    main()
