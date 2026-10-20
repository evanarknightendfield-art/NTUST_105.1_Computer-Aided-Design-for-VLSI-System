#!/usr/bin/env python3
"""Check completed evidence and report each acceptance condition separately."""
import csv
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LAB = ROOT.parents[1]
OUT = LAB / 'Testcases/results/CodexChainDP'


def read(suite):
    return list(csv.DictReader((OUT / suite / 'record.csv').open()))


def main():
    stats = json.loads((OUT / 'summary.json').read_text())
    frozen = json.loads((OUT / 'frozen_sources.json').read_text())
    assert all(hashlib.sha256((LAB / p).read_bytes()).hexdigest() == h for p, h in frozen.items())
    counts = {'baseline_default': 27, 'default': 11, 'edge_CodexChainDP': 16,
              'large_L1': 1, 'fixed_work_holdout': 48, 'fixed_work_edge': 96,
              'equal_total_time': 144, 'equal_time_default': 144, 'edge_seed2': 16, 'baseline_seed2': 16}
    for suite, count in counts.items():
        rows = read(suite)
        assert len(rows) == count and all(r['verdict'] == 'PASS' for r in rows), suite
        if suite.startswith('fixed_work'):
            assert all(int(r['attempts']) in {0, 5000000} for r in rows), suite + ': attempts'
            for bench, seed in {(r['bench'], r['seed']) for r in rows}:
                attempts = [int(r['attempts']) for r in rows if (r['bench'], r['seed']) == (bench, seed)]
                assert len(attempts) == 2 and attempts[0] == attempts[1], suite + ': unequal work'
    timed = read('equal_time_default')
    for bench in {r['bench'] for r in timed}:
        rr = [r for r in timed if r['bench'] == bench]
        assert len({float(r['budget']) for r in rr}) == 1, bench + ': budgets'
        assert {(r['version'], int(r['seed'])) for r in rr} == {
            (v, s) for v in ['ClaudeOpus55_v4', 'CodexChainDP'] for s in [1, 2, 3]}
    hashes = list(csv.DictReader((OUT / 'generator_default_hashes.csv').open()))
    assert hashes and all(r['equal'] == 'True' for r in hashes)
    assert not (ROOT / 'results').exists()
    sanitized = read('final_global_sanitized')
    assert {r['test'] for r in sanitized} == {'build', 'global_regression'}
    assert all(int(r['returncode']) == 0 for r in sanitized)
    goals = {
        'max_no_case_regression': not stats['all']['max_regressions'],
        'max_geomean_ratio': stats['all']['max_geomean'],
        'total_geomean_ratio': stats['all']['disp_geomean'],
        'total_within_one_percent': stats['all']['disp_geomean'] <= 1.01,
        'equal_budget_hpwl_holdout_ratio': stats['equal_time_default']['holdout']['hpwl_geomean'],
        'equal_budget_hpwl_edge_ratio': stats['equal_time_default']['edge']['hpwl_geomean'],
        'hpwl_both_groups_better': all(stats['equal_time_default'][g]['hpwl_geomean'] < 1 for g in ['holdout', 'edge']),
        'strict_equal_elapsed_test_performed': False,
        'fresh_comparison': stats['fresh_comparison'],
        'required_default_and_unseen_pass': 44,
        'L1_seconds': float(stats['L1'][0]['seconds']),
        'L1_within_240_seconds': float(stats['L1'][0]['seconds']) < 240,
        'frozen_sources_unchanged': True,
        'original_edge_reproduced': True,
        'final_global_asan_ubsan_pass': True,
        'comparison_counts': counts,
    }
    (OUT / 'acceptance.json').write_text(json.dumps(goals, indent=2) + '\n')
    print(json.dumps(goals, indent=2))


if __name__ == '__main__':
    main()
