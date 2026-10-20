#!/usr/bin/env python3
"""Rebuild the final capped-packing regression with ASan and UBSan."""
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
LAB = ROOT.parents[1]
OUT = LAB / 'Testcases/results/CodexChainDP/final_global_sanitized'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sources = [ROOT / 'tests/global_test.cpp'] + [
        ROOT / 'src' / (n + '.cpp') for n in ['global', 'window', 'optimizer', 'placedb']]
    hashes = {str(p.relative_to(LAB)): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in sources + sorted((ROOT / 'src').glob('*.hpp'))}
    (OUT / 'sources.json').write_text(json.dumps(hashes, indent=2) + '\n')
    binary = OUT / 'global_test'
    build = ['g++', '-std=c++17', '-O1', '-g', '-pthread', '-UNDEBUG',
             '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
             *map(str, sources), '-o', str(binary)]
    records = []
    for label, command in [('build', build), ('global_regression', [str(binary)])]:
        start = time.monotonic()
        with (OUT / (label + '.log')).open('w') as log:
            result = subprocess.run(command, stdout=log, stderr=log, timeout=240)
        records.append(dict(test=label,returncode=result.returncode,
                            seconds=time.monotonic()-start,command=json.dumps(command)))
        with (OUT / 'record.csv').open('w') as f:
            writer=csv.DictWriter(f,fieldnames=list(records[0]));writer.writeheader();writer.writerows(records)
        print(label, result.returncode, flush=True)
        if result.returncode:
            raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
