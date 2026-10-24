#!/usr/bin/env python3
"""orchestrate_codex.py — drive the next version's Codex phases A2 -> B (repeated) -> C -> D unattended.

Why: background jobs started inside the Codex sandbox die with the `codex exec` session (phase A's nohup did),
and a long session that waits on jobs costs tokens on every status check. So every phase is one fresh
`codex exec` session; it writes scripts and ends with RUN:/WAIT: lines (or B_DONE / NO_FINAL). This script
runs the RUN commands outside the sandbox, waits for the WAIT marker in the shell (no tokens), and starts
the next session. Prompts: ../codex_prompts/*.md (+ _footer.md). Log: <NEW>/orchestrator.log.

usage (from Lab3): setsid nohup python3 Testcases/results/ClaudeOpus55_v6/scripts/orchestrate_codex.py CodexParallelChain &
"""
import json, os, re, subprocess, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[3]          # scripts -> ClaudeOpus55_v6 -> results -> Testcases -> Lab3
PROMPTS = HERE.parent / 'codex_prompts'
NEW = sys.argv[1] if len(sys.argv) > 1 else 'CodexParallelChain'
RES = LAB / 'Testcases/results' / NEW
LOG = RES / 'orchestrator.log'
STATE = RES / 'orchestrator_state.json'
B_MAX = int(os.environ.get('B_MAX', '9'))          # B sessions before the forced wrap-up (2 items x 3 rounds + checks)
PY = '/home/evan/anaconda3/bin/python3'


def log(msg):
    with LOG.open('a') as f:
        f.write(f'{time.strftime("%m-%d %H:%M:%S")} {msg}\n')


def state(**kw):
    s = json.loads(STATE.read_text()) if STATE.exists() else {}
    s.update(kw)
    STATE.write_text(json.dumps(s, indent=1, ensure_ascii=False) + '\n')
    return s


def wait_for(marker, hours, proc=None):
    """Wait in the shell; if the job we started exits without the marker, report it."""
    m, t0 = LAB / marker, time.time()
    while not m.exists():
        if proc is not None and proc.poll() is not None:
            time.sleep(30)
            if not m.exists():
                return f'工作已結束（exit {proc.returncode}）但沒有出現完成標記 {marker}；log：{proc.args[-1] if isinstance(proc.args, list) else ""}'
        if time.time() - t0 > hours * 3600:
            return f'等了 {hours} 小時，完成標記 {marker} 仍未出現'
        time.sleep(60)
    return None


def limit_wait(logfile):
    """Seconds to sleep if the session failed on the usage limit ('try again at 6:23 AM'), else None."""
    text = logfile.read_text(errors='replace') if logfile.exists() else ''
    if 'usage limit' not in text:
        return None
    m = re.search(r'try again at (\d{1,2}):(\d{2})\s*([AP]M)', text)
    if not m:
        return 1800
    h, mi = int(m.group(1)) % 12 + (12 if m.group(3) == 'PM' else 0), int(m.group(2))
    now = time.localtime()
    target = time.mktime((now.tm_year, now.tm_mon, now.tm_mday, h, mi, 0, 0, 0, -1)) + 120
    if target < time.time():
        target += 86400
    return target - time.time()


def codex(name, prompt_file, note='', effort=None):
    """One session; on a usage-limit failure wait for the reset and run the same session again."""
    for attempt in range(6):
        rc, text = codex_once(name, prompt_file, note, effort)
        w = limit_wait(RES / 'orchestrator' / f'{name}.log')
        if rc == 0 or w is None:
            return rc, text
        log(f'{name}：Codex 使用量上限，等 {w / 60:.0f} 分鐘後重跑同一個 session（不算一次）')
        time.sleep(w)
    return rc, text


def codex_once(name, prompt_file, note='', effort=None):
    prompt = (PROMPTS / prompt_file).read_text() + (PROMPTS / '_footer.md').read_text()
    if note:
        prompt = f'【協調者備註】{note}\n\n' + prompt
    last = RES / 'orchestrator' / f'{name}.last.md'
    last.parent.mkdir(parents=True, exist_ok=True)
    cmd = ['codex', 'exec', '-C', str(LAB), '-s', 'workspace-write', '-o', str(last)]
    if effort:
        cmd += ['-c', f'model_reasoning_effort="{effort}"']
    cmd.append('-')
    log(f'codex {name} ({prompt_file}) 開始')
    with (RES / 'orchestrator' / f'{name}.log').open('w') as f:
        try:
            rc = subprocess.run(cmd, input=prompt, text=True, cwd=LAB, stdout=f, stderr=subprocess.STDOUT, timeout=4 * 3600).returncode
        except subprocess.TimeoutExpired:
            rc = 'timeout'
    text = last.read_text() if last.exists() else ''
    log(f'codex {name} 結束 rc={rc}；最後訊息 {len(text)} 字')
    return rc, text


def run_jobs(name, text):
    """Start every RUN: line (sequentially, in one background shell) and return (proc, marker)."""
    runs = [m.strip() for m in re.findall(r'^\s*RUN:\s*(.+)$', text, re.M)]
    waits = [m.strip() for m in re.findall(r'^\s*WAIT:\s*(.+)$', text, re.M)]
    if not runs:
        return None, (waits[-1] if waits else None)
    script = ' && '.join(runs)
    jl = RES / 'orchestrator' / f'{name}.jobs.log'
    log(f'{name} 啟動：{script}')
    proc = subprocess.Popen(['setsid', 'bash', '-c', script + f' >> {jl} 2>&1', str(jl)], cwd=LAB, stdin=subprocess.DEVNULL)
    return proc, (waits[-1] if waits else None)


def phase_with_jobs(name, prompt_file, note='', effort=None, hours=4):
    rc, text = codex(name, prompt_file, note, effort)
    proc, marker = run_jobs(name, text)
    problem = None
    if marker:
        log(f'{name} 等待 {marker}')
        problem = wait_for(marker, hours, proc)
        log(f'{name} 等待結束：{problem or "完成標記出現"}')
    elif proc is not None:
        proc.wait()
        log(f'{name} 工作結束 rc={proc.returncode}（沒有 WAIT）')
    return rc, text, problem


def asan_outside():
    """Full ASan/UBSan (with LeakSanitizer) outside the sandbox, for phase C to record."""
    b = RES / 'bin' / 'asan_outside'
    out = RES / 'phase2' / 'asan_outside.log'
    out.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, PATH='/home/evan/anaconda3/envs/eda/bin:' + os.environ['PATH'], ASAN_OPTIONS='detect_leaks=1')
    cmds = [['taskset', '-c', '16-19', 'cmake', '-S', str(LAB / 'AI_Solutions' / NEW), '-B', str(b), '-DBUILD_TESTING=ON',
             '-DCMAKE_BUILD_TYPE=RelWithDebInfo', '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined'],
            ['taskset', '-c', '16-19', 'cmake', '--build', str(b), '-j4'],
            ['taskset', '-c', '16-19', 'ctest', '--output-on-failure', '--timeout', '900']]
    rc = 0
    with out.open('w') as f:
        for c in cmds:
            rc = subprocess.run(c, env=env, cwd=b if c[3] == 'ctest' else LAB, stdout=f, stderr=subprocess.STDOUT).returncode
            f.write(f'\n[orchestrator] {" ".join(c[3:6])} rc={rc}\n')
            if rc:
                break
    log(f'沙箱外 ASan/UBSan：{out}（rc={rc}）')


def main():
    RES.mkdir(parents=True, exist_ok=True)
    s = state()
    log(f'協調者開始（{NEW}），狀態 {s}')
    if not s.get('A2'):
        p = wait_for('Testcases/results/' + NEW + '/phase1/ALL_DONE', 6)
        if p:
            log('A 的診斷沒有完成：' + p + '；停止'); return
        codex('A2', 'A2.md')
        s = state(A2=True)
    note = ''
    while not s.get('B_DONE'):
        n = s.get('B_runs', 0)
        if n >= B_MAX + 1:
            log('B 收尾也沒有寫 B_DONE；停止，等使用者處理'); return
        prompt = 'B.md' if n == 0 else ('B_force.md' if n >= B_MAX else 'B_next.md')
        rc, text, problem = phase_with_jobs(f'B{n + 1}', prompt, note, effort='high', hours=4)
        note = f'上一次等待的問題：{problem}（請看 log、修正後重跑或撤回）' if problem else ''
        s = state(B_runs=n + 1)
        if re.search(r'^\s*B_DONE\s*$', text, re.M) and not problem:
            s = state(B_DONE=True)
    log('B 完成')
    if not s.get('asan'):
        asan_outside()
        s = state(asan=True)
    if not s.get('C'):
        rc, text, problem = phase_with_jobs('C', 'C.md', hours=12)
        if re.search(r'^\s*NO_FINAL\s*$', text, re.M):
            state(C=True, final=False)
            log('C：沒有保留的改動，跳過最終驗證與 D；結果寫在 WORK_STATE，等使用者看'); return
        elif problem:
            log('最終驗證有問題：' + problem + '；停止，等使用者處理'); return
        s = state(C=True)
    if not s.get('D'):
        codex('D', 'D.md')
        state(D=True)
    log('全部完成')


if __name__ == '__main__':
    main()
