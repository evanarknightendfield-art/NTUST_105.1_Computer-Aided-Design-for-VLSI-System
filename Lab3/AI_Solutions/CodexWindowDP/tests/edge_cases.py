#!/usr/bin/env python3
"""Exercise the added fixed-only / blocked-row guards and input overwrite check."""
from pathlib import Path
import hashlib
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / 'dp'
checker = root.parents[1] / 'Grader/tools/check_pl.py'


def fixture(name, fixed_only=False, no_nets=False):
    folder = root / 'results/edge_cases' / name
    folder.mkdir(parents=True, exist_ok=True)
    prefix = folder / 'input'
    cells = 'block 10 1 terminal\n' + ('' if fixed_only else 'a 2 1\nb 1 1\n')
    placements = 'block 0 0 : FS /FIXED\n' + ('' if fixed_only else 'a 4.2 0.1 : N\nb 5.1 0 : FN\n')
    net = '' if fixed_only or no_nets else 'NetDegree : 3 n0\na I : 0 0\nb I : 0 0\nblock O : 0 0\n'
    contents = {
        '.aux': 'RowBasedPlacement : input.nodes input.nets input.wts input.pl input.scl\n',
        '.nodes': f'UCLA nodes 1.0\nNumNodes : {1 if fixed_only else 3}\nNumTerminals : 1\n' + cells,
        '.pl': 'UCLA pl 1.0\n' + placements,
        '.nets': f'UCLA nets 1.0\nNumNets : {int(bool(net))}\nNumPins : {3 if net else 0}\n' + net,
        '.wts': 'UCLA wts 1.0\n',
        '.scl': 'UCLA scl 1.0\nNumRows : 2\n' + ''.join(f'CoreRow Horizontal\n Coordinate : {y}\n Height : 1\n Sitewidth : 1\n Sitespacing : 1\n SubrowOrigin : 0 NumSites : 10\nEnd\n' for y in (0, 1)),
    }
    for suffix, text in contents.items():
        Path(str(prefix) + suffix).write_text(text)
    command = [str(exe), str(prefix), str(folder / 'output'), '--time', '1000', '--effort', '10', '--min-moves', '1000', '--no-refine', '--spread', 'off']
    result = subprocess.run(command, text=True, capture_output=True, timeout=30)
    assert result.returncode == 0, result.stderr
    for suffix in ('_legal.pl', '.pl'):
        result = subprocess.run([sys.executable, str(checker), str(prefix), str(folder / ('output' + suffix))], text=True, capture_output=True)
        assert result.returncode == 0, result.stdout + result.stderr
    before = {suffix: hashlib.sha256(Path(str(prefix) + suffix).read_bytes()).hexdigest() for suffix in contents}
    rejected = subprocess.run([str(exe), str(prefix), str(prefix), '--no-sa'], text=True, capture_output=True)
    assert rejected.returncode != 0 and 'aliases input' in rejected.stderr
    after = {suffix: hashlib.sha256(Path(str(prefix) + suffix).read_bytes()).hexdigest() for suffix in contents}
    assert before == after, 'source changed on rejected overwrite'


fixture('blocked_row')
fixture('fixed_only', fixed_only=True)
fixture('no_nets', no_nets=True)
print('PASS: fully blocked row, fixed-only design, disconnected design, input overwrite rejection')
