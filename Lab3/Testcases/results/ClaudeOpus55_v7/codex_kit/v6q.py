#!/usr/bin/env python3
"""v6q.py — serial, checkpointed runner shared by the v6 experiment scripts.

Reusable: copy this scripts/ folder to Testcases/results/<VER>/scripts/; R, the lock and WORK_STATE follow.

Every step writes <out>/<name>.done.json (command + hashes) when it finishes; a
rerun skips finished steps whose command did not change. Timed steps hold
results/ClaudeOpus55_v6/timing.lock, so only one timed program runs at a time.
Each finished step appends a line to AI_Solutions/ClaudeOpus55_v6/WORK_STATE.md.
"""
import fcntl, hashlib, json, os, subprocess, time
from pathlib import Path

R = Path(__file__).resolve().parents[1]          # Testcases/results/ClaudeOpus55_v6
LAB = R.parents[2]
PY = '/home/evan/anaconda3/bin/python3'
V5S = R.parent / 'ClaudeOpus55_v5' / 'scripts'
# the results folder is named after the version: Testcases/results/<VER>/ <-> AI_Solutions/<VER>/
WORK_STATE = LAB / 'AI_Solutions' / R.name / 'WORK_STATE.md'
PIN = ['taskset', '-c', '16-19']


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def note(msg):
    with WORK_STATE.open('a') as f:
        f.write(f'- {time.strftime("%m-%d %H:%M")} {msg}\n')
    print(msg, flush=True)


class Queue:
    def __init__(self, out, timed=True):
        self.out = Path(out)
        self.out.mkdir(parents=True, exist_ok=True)
        self.lock = None
        if timed:
            self.lock = (R / 'timing.lock').open('w')
            fcntl.flock(self.lock, fcntl.LOCK_EX)

    def run(self, name, cmd, env=None, cwd=LAB, quiet=False):
        cmd = [str(c) for c in cmd]
        marker = self.out / f'{name}.done.json'
        if marker.exists():
            if json.loads(marker.read_text())['command'] == cmd:
                return True
            raise RuntimeError(f'{name}: finished with a different command')
        t = time.time()
        with (self.out / f'{name}.log').open('w') as f:
            rc = subprocess.run(cmd, cwd=cwd, env=env, stdout=f, stderr=subprocess.STDOUT).returncode
        info = dict(command=cmd, returncode=rc, seconds=round(time.time() - t, 2), finished=time.ctime())
        if rc:
            (self.out / f'{name}.failed.json').write_text(json.dumps(info, indent=1) + '\n')
            note(f'{self.out.name}/{name} FAILED rc={rc}，看 {name}.log')
            return False
        marker.write_text(json.dumps(info, indent=1) + '\n')
        if not quiet:
            note(f'{self.out.relative_to(R)}/{name} 完成（{info["seconds"]:.0f} 秒）')
        return True


def env_with_pinned_cmake():
    """local_grade builds with cmake; keep the build on the E-cores."""
    tb = R / 'scripts' / 'toolbin'
    tb.mkdir(exist_ok=True)
    w = tb / 'cmake'
    w.write_text('#!/bin/sh\nexec taskset -c 16-19 /home/evan/anaconda3/envs/eda/bin/cmake "$@"\n')
    w.chmod(0o755)
    return dict(os.environ, PATH=f'{tb}:/home/evan/anaconda3/envs/eda/bin:' + os.environ['PATH'], PYTHONUNBUFFERED='1')


def build(version, out_dir, src=None):
    """cmake Release build of a solution into out_dir (pinned to the E-cores)."""
    src = src or LAB / 'AI_Solutions' / version
    e = env_with_pinned_cmake()
    subprocess.run(['cmake', '-S', src, '-B', out_dir, '-DBUILD_TESTING=OFF', '-DCMAKE_BUILD_TYPE=Release'],
                   env=e, check=True, stdout=subprocess.DEVNULL)
    subprocess.run(PIN + ['/home/evan/anaconda3/envs/eda/bin/cmake', '--build', out_dir, '-j4'],
                   env=e, check=True, stdout=subprocess.DEVNULL)
    return Path(out_dir) / 'dp'
