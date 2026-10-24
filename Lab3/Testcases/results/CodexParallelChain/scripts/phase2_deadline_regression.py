#!/usr/bin/env python3
"""27-case final-source legality/deadline regression; edge never tunes quality."""
import argparse
import fcntl
import json
from pathlib import Path
import sys
import time
import traceback
sys.dont_write_bytecode = True
from phase2_acceptance import CASES, SOLUTION, dump, prefix, src_hash, validate, worker
from diag import sha, prefix_hashes, verify_protected
from v6q import LAB, R, PY, Queue

SELF = Path(__file__).resolve()
D = R / 'phase2/deadline_regression/final_b'
TRAIN = R / 'phase2/train/final_b'
EDGES = [p.stem for p in sorted((LAB / 'Testcases/edge').glob('*/*.aux'))]
if [b.split('_')[0] for b in EDGES] != [f'e{i:02d}' for i in range(1, 17)]:
    raise RuntimeError('Expected the original 16 edge regression cases')


def protocol():
    validate(TRAIN, 'train', 'final_b')
    return dict(src_sha256=src_hash(SOLUTION / 'src'), script_sha256=sha(SELF),
                train_protocol_sha256=sha(TRAIN / 'protocol.json'),
                train_summary_sha256=sha(TRAIN / 'summary.json'),
                inputs={b: prefix_hashes(prefix('train', b)) for b in EDGES},
                cases=CASES + EDGES,
                note='Reuse 11 completed candidate training runs; run 16 edge regressions once. '
                     'Only legality and extra deadlines are gates; no edge quality tuning.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker')
    args = parser.parse_args()
    p = protocol()
    D.mkdir(parents=True, exist_ok=True)
    if (D / 'protocol.json').exists() and json.loads((D / 'protocol.json').read_text()) != p:
        raise RuntimeError('Regression protocol changed')
    if args.worker:
        if args.worker not in EDGES or not (D / 'protocol.json').exists():
            raise RuntimeError('Invalid/unprepared regression worker')
        worker(D, 'train', args.worker, 'candidate', 1)
        return
    dump(D / 'protocol.json', p)
    binary = D / 'candidate'
    if not binary.exists():
        binary.symlink_to(TRAIN / 'candidate', target_is_directory=True)
    if binary.resolve() != (TRAIN / 'candidate').resolve():
        raise RuntimeError('Wrong production binary')
    verify_protected()
    q = Queue(D / 'jobs')
    with (R.parent / 'ClaudeOpus55_v6/timing.lock').open('r') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for bench in EDGES:
            if not q.run(bench, [PY, SELF, '--worker', bench], quiet=True):
                raise RuntimeError('Regression worker failed')
        rows = []
        for bench in CASES + EDGES:
            root = TRAIN if bench in CASES else D
            path = root / 'runs' / bench / 'candidate/metrics.json'
            rows.append(dict(bench=bench, evidence=str(path.relative_to(LAB)),
                             metrics_sha256=sha(path), **json.loads(path.read_text())))
        gates = dict(all_27_legal=all(x['legal'] for x in rows),
                     zero_extra_overshoots=not any(x['extra_overshoot'] for x in rows))
        dump(D / 'summary.json', dict(rows=rows, gates=gates, all_gates=all(gates.values())))
        if protocol() != p:
            raise RuntimeError('Regression inputs changed')
        verify_protected(D / 'protected_after.json')
        dump(D / 'ALL_DONE', dict(src_sha256=p['src_sha256'], protocol_sha256=sha(D / 'protocol.json'),
                                 gates=gates, all_gates=all(gates.values()), finished=time.ctime()))
    print(gates)


if __name__ == '__main__':
    try:
        main()
    except Exception:
        dump(D / 'FAILED.json', dict(error=traceback.format_exc(), when=time.ctime()))
        raise
