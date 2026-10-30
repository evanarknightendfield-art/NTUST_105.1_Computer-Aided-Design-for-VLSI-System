#!/usr/bin/env python3
"""machine_lock.py — the machine-wide timing lock: benches hold Testcases/results/timing_global.lock while a dp runs,
so only one dp runs on the machine at a time. Held until the process exits.
"""
import fcntl
from pathlib import Path


def machine_lock(lab):
    lab = Path(lab)
    held = []
    for p in [lab / 'Testcases/results/timing_global.lock']:
        if p.parent.is_dir():
            f = p.open('a')
            fcntl.flock(f, fcntl.LOCK_EX)
            held.append(f)
    return held
