#!/usr/bin/env python3
"""orchestrate_codex.py NEW — run the whole next version (base ClaudeOpus55_v7) unattended; Codex only where judgment is needed.

  setup  (here)   AI_Solutions/NEW = copy of v7 (src tests CMakeLists.txt Makefile), Testcases/results/NEW/scripts = this kit,
                  sha256 of the protected folders and of COMPARISON.md / HISTORY.md
  A      (here)   scripts/phaseA_queue.py (~1.7 h) -> phaseA/summary.md
  A2     (Codex)  diagnosis.md
  B      (Codex)  one round per session: change -> RUN training -> end; the next session judges (B.md, B_next.md, B_force.md)
  asan   (here)   full ASan/UBSan with LeakSanitizer outside the sandbox
  C      (here)   frozen_sources.sha256 + scripts/final_validate.py (~6 h) + final_report.py   (skipped on NO_FINAL)
  D      (Codex)  README, COMPARISON section 14, HISTORY rows, edge table
  check  (here)   protected folders and frozen sources unchanged

Why this shape: background jobs started inside `codex exec` die with the session, and a session that waits on jobs pays for
every status check. Sessions end with RUN:/WAIT: lines; this script runs the RUN commands outside the sandbox (only
`python3 Testcases/results/NEW/scripts/*.py ...` is accepted), waits in the shell, and starts the next session.
Resumable: state in Testcases/results/NEW/orchestrator_state.json; rerun the same command after a stop.
Environment: CODEX_MODEL (default: ~/.codex/config.toml), CODEX_EFFORT_B (default high), CODEX_EFFORT (A2 / D; default: config),
B_MAX (B sessions before the forced wrap-up, default 12), DRY_RUN=1 (fake Codex: tests this script without tokens).
"""
import hashlib, json, os, re, shutil, subprocess, sys, time
from pathlib import Path

KIT = Path(__file__).resolve().parent
LAB = KIT.parents[3]                     # codex_kit -> ClaudeOpus55_v7 -> results -> Testcases -> Lab3
BASE = 'ClaudeOpus55_v7'
NEW = sys.argv[1] if len(sys.argv) > 1 else 'CodexAstra'
SRC, RES = LAB / 'AI_Solutions' / NEW, LAB / 'Testcases/results' / NEW
LOG, STATE = RES / 'orchestrator.log', RES / 'orchestrator_state.json'
PY = '/home/evan/anaconda3/bin/python3'
PROTECTED = ['ClaudeOpus55', 'CodexWindowDP', 'ClaudeOpus55_v4', 'CodexChainDP', 'ClaudeOpus55_v5', 'CodexCapDP',
             'ClaudeOpus55_v6', 'CodexParallelChain', BASE]
B_MAX = int(os.environ.get('B_MAX', '12'))
DRY = os.environ.get('DRY_RUN') == '1'


def log(msg):
    RES.mkdir(parents=True, exist_ok=True)
    with LOG.open('a') as f:
        f.write(f'{time.strftime("%m-%d %H:%M:%S")} {msg}\n')
    print(msg, flush=True)


def state(**kw):
    s = json.loads(STATE.read_text()) if STATE.exists() else {}
    s.update(kw)
    STATE.write_text(json.dumps(s, indent=1, ensure_ascii=False) + '\n')
    return s


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def protected_manifest():
    lines = []
    for d in PROTECTED:
        for p in sorted((LAB / 'AI_Solutions' / d).rglob('*')):
            if p.is_file():
                lines.append(f'{sha(p)}  {p.relative_to(LAB)}')
    return '\n'.join(lines) + '\n'


def protected_ok(when):
    now = protected_manifest()
    if now == (RES / 'provenance' / 'protected_before.sha256').read_text():
        return True
    (RES / 'provenance' / f'protected_changed_{when}.sha256').write_text(now)
    log(f'保護資料夾在 {when} 之後被改動（見 provenance/protected_changed_{when}.sha256）；停止，等使用者處理')
    return False


def setup():
    SRC.mkdir(parents=True, exist_ok=True)
    for name in ['src', 'tests']:
        if not (SRC / name).exists():
            shutil.copytree(LAB / 'AI_Solutions' / BASE / name, SRC / name)
    for name in ['CMakeLists.txt', 'Makefile']:
        shutil.copy(LAB / 'AI_Solutions' / BASE / name, SRC / name)
    ws = SRC / 'WORK_STATE.md'
    if not ws.exists():
        ws.write_text(f'# {NEW} — WORK_STATE\n\n以 {BASE} 為底（{time.strftime("%Y-%m-%d %H:%M")} 由協調者複製 src、tests、CMakeLists.txt、Makefile）。\n'
                      f'規則與目標：AI_Solutions/{BASE}/HANDOFF.md。執行輸出全部在 Testcases/results/{NEW}/。\n\n## 協調者紀錄\n\n')
    (RES / 'scripts').mkdir(parents=True, exist_ok=True)
    for p in KIT.glob('*.py'):
        if p.name != 'orchestrate_codex.py':
            shutil.copy(p, RES / 'scripts' / p.name)
    (RES / 'provenance').mkdir(exist_ok=True)
    (RES / 'provenance' / 'protected_before.sha256').write_text(protected_manifest())
    (RES / 'provenance' / 'docs_before.sha256').write_text(
        ''.join(f'{sha(LAB / "AI_Solutions" / n)}  AI_Solutions/{n}\n' for n in ['COMPARISON.md', 'HISTORY.md']))
    log(f'準備完成：AI_Solutions/{NEW}、Testcases/results/{NEW}/scripts、保護資料夾 sha256')


def ws_note(msg):
    with (SRC / 'WORK_STATE.md').open('a') as f:
        f.write(f'- {time.strftime("%m-%d %H:%M")} [協調者] {msg}\n')


def wait_for(marker, hours, proc=None):
    m, t0 = LAB / marker, time.time()
    while not m.exists():
        if proc is not None and proc.poll() is not None:
            time.sleep(30)
            if not m.exists():
                return f'工作已結束（exit {proc.returncode}）但沒有出現完成標記 {marker}'
        if time.time() - t0 > hours * 3600:
            return f'等了 {hours} 小時，{marker} 仍未出現'
        time.sleep(60)
    return None


def run_here(name, cmd, marker, hours):
    """a long job of this script (not Codex): run in its own session, wait for the marker"""
    jl = RES / 'orchestrator' / f'{name}.log'
    jl.parent.mkdir(parents=True, exist_ok=True)
    log(f'{name} 開始：{" ".join(map(str, cmd))}')
    if DRY:
        (LAB / marker).parent.mkdir(parents=True, exist_ok=True)
        (LAB / marker).write_text('dry\n')
        return None
    with jl.open('a') as f:
        proc = subprocess.Popen([str(c) for c in cmd], cwd=LAB, stdout=f, stderr=subprocess.STDOUT, start_new_session=True)
    problem = wait_for(marker, hours, proc)
    log(f'{name} 結束：{problem or "完成標記出現"}')
    return problem


def limit_wait(logfile):
    text = logfile.read_text(errors='replace') if logfile.exists() else ''
    if 'usage limit' not in text.lower() and 'rate limit' not in text.lower():
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


def prompt_text(file, **fill):
    text = (KIT / 'prompts' / file).read_text() + (KIT / 'prompts' / '_footer.md').read_text()
    fill.setdefault('NOTE', '')
    fill.setdefault('FINAL_NOTE', '')
    for k, v in dict(NEW=NEW, **fill).items():
        text = text.replace('{' + k + '}', v)
    return text


def codex(name, file, effort=None, **fill):
    """one fresh `codex exec` session; on a usage-limit failure wait for the reset and run it again"""
    prompt = prompt_text(file, **fill)
    out = RES / 'orchestrator'
    out.mkdir(parents=True, exist_ok=True)
    (out / f'{name}.prompt.md').write_text(prompt)
    last = out / f'{name}.last.md'
    cmd = ['codex', 'exec', '-C', str(LAB), '-s', 'workspace-write', '--skip-git-repo-check', '-o', str(last)]
    if os.environ.get('CODEX_MODEL'):
        cmd += ['-m', os.environ['CODEX_MODEL']]
    effort = effort or os.environ.get('CODEX_EFFORT')
    if effort:
        cmd += ['-c', f'model_reasoning_effort="{effort}"']
    cmd.append('-')
    for attempt in range(8):
        log(f'codex {name}（{file}）開始' + (f'，第 {attempt + 1} 次' if attempt else ''))
        if DRY:
            last.write_text(fake_codex(name))
            rc = 0
        else:
            with (out / f'{name}.log').open('w') as f:
                try:
                    rc = subprocess.run(cmd, input=prompt, text=True, cwd=LAB, stdout=f, stderr=subprocess.STDOUT,
                                        timeout=4 * 3600).returncode
                except subprocess.TimeoutExpired:
                    rc = 'timeout'
        text = last.read_text() if last.exists() else ''
        log(f'codex {name} 結束 rc={rc}；最後訊息 {len(text)} 字')
        w = limit_wait(out / f'{name}.log')
        if rc == 0 or w is None:
            return rc, text
        log(f'{name}：使用量上限，等 {w / 60:.0f} 分鐘後重跑同一個 session')
        time.sleep(w)
    return rc, text


def fake_codex(name):
    """DRY_RUN: what a session would end with"""
    if name.startswith('B'):
        n = int(name[1:])
        if n == 1:
            return f'改了 T1\nRUN: python3 Testcases/results/{NEW}/scripts/phase2_train.py dry\nWAIT: Testcases/results/{NEW}/phase2/train/dry/pair.md\n'
        return 'B 完成\nB_DONE\n'
    return f'{name} done\n'


ALLOWED = re.compile(r'^(?:python3|/home/evan/anaconda3/bin/python3)\s+Testcases/results/' + re.escape(NEW) + r'/scripts/[\w.-]+\.py(?:\s+[\w.,=/:-]+)*$')


def run_jobs(name, text):
    runs = [m.strip() for m in re.findall(r'^\s*RUN:\s*(.+)$', text, re.M)]
    waits = [m.strip() for m in re.findall(r'^\s*WAIT:\s*(.+)$', text, re.M)]
    bad = [r for r in runs if not ALLOWED.match(r)]
    if bad:
        log(f'{name}：拒絕執行不在允許格式內的 RUN：{bad}')
        return None, None, f'這些 RUN 不符合格式、沒有執行：{bad}（只能是 python3 Testcases/results/{NEW}/scripts/<腳本>.py 參數…）'
    if not runs:
        return None, (waits[-1] if waits else None), None
    jl = RES / 'orchestrator' / f'{name}.jobs.log'
    script = ' && '.join(r.replace('python3 ', f'{PY} ', 1) if r.startswith('python3 ') else r for r in runs)
    log(f'{name} 執行：{script}')
    if DRY:
        for wt in waits:
            (LAB / wt).parent.mkdir(parents=True, exist_ok=True)
            (LAB / wt).write_text('dry\n')
        return None, (waits[-1] if waits else None), None
    proc = subprocess.Popen(['bash', '-c', f'{script} >> {jl} 2>&1'], cwd=LAB, stdin=subprocess.DEVNULL, start_new_session=True)
    return proc, (waits[-1] if waits else None), None


def session_with_jobs(name, file, effort=None, hours=8, **fill):
    rc, text = codex(name, file, effort, **fill)
    proc, marker, problem = run_jobs(name, text)
    if marker and not problem:
        log(f'{name} 等待 {marker}')
        problem = wait_for(marker, hours, proc)
        log(f'{name} 等待結束：{problem or "完成標記出現"}')
    elif proc is not None:
        proc.wait()
        log(f'{name} 工作結束 rc={proc.returncode}（沒有 WAIT）')
        if proc.returncode:
            problem = f'RUN 失敗 rc={proc.returncode}，看 orchestrator/{name}.jobs.log'
    return rc, text, problem


def asan_outside():
    b, out = RES / 'bin' / 'asan_outside', RES / 'phase2' / 'asan_outside.log'
    out.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, PATH='/home/evan/anaconda3/envs/eda/bin:' + os.environ['PATH'], ASAN_OPTIONS='detect_leaks=1')
    cmds = [['taskset', '-c', '16-19', 'cmake', '-S', str(SRC), '-B', str(b), '-DBUILD_TESTING=ON', '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
             '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined'],
            ['taskset', '-c', '16-19', 'cmake', '--build', str(b), '-j4'],
            ['taskset', '-c', '16-19', 'ctest', '--output-on-failure', '--timeout', '900']]
    rc = 0
    if not DRY:
        with out.open('w') as f:
            for c in cmds:
                rc = subprocess.run(c, env=env, cwd=b if c[3] == 'ctest' else LAB, stdout=f, stderr=subprocess.STDOUT).returncode
                f.write(f'\n[orchestrator] {" ".join(c[3:6])} rc={rc}\n')
                if rc:
                    break
    log(f'沙箱外 ASan/UBSan（detect_leaks=1）：{out}（rc={rc}）')
    ws_note(f'沙箱外 ASan/UBSan（detect_leaks=1）rc={rc}，log：Testcases/results/{NEW}/phase2/asan_outside.log')
    return rc


def freeze():
    files = sorted(p for d in ['src', 'tests'] for p in (SRC / d).rglob('*') if p.is_file()) + [SRC / 'CMakeLists.txt', SRC / 'Makefile']
    (RES / 'frozen_sources.sha256').write_text(''.join(f'{sha(p)}  {p.relative_to(SRC)}\n' for p in files))
    (RES / 'freeze.json').write_text(json.dumps(dict(frozen=time.strftime('%Y-%m-%d %H:%M'), files=len(files),
                                                     manifest_sha256=sha(RES / 'frozen_sources.sha256')), indent=1) + '\n')
    ws_note(f'凍結：frozen_sources.sha256（{len(files)} 檔，manifest {sha(RES / "frozen_sources.sha256")[:12]}…）')


def frozen_ok():
    for line in (RES / 'frozen_sources.sha256').read_text().splitlines():
        h, name = line.split('  ', 1)
        if sha(SRC / name) != h:
            log(f'凍結的原始碼被改了：{name}')
            return False
    return True


def main():
    if not STATE.exists():
        if SRC.exists():
            sys.exit(f'AI_Solutions/{NEW} 已經存在但沒有協調者狀態：換一個名字，或確認後刪掉再跑')
        RES.mkdir(parents=True, exist_ok=True)
    s = state()
    if not s.get('setup'):
        setup()
        s = state(setup=True)
    log(f'協調者開始（{NEW}），狀態 {s}')
    if not s.get('A'):
        p = run_here('A', [PY, RES / 'scripts' / 'phaseA_queue.py'], f'Testcases/results/{NEW}/phaseA/ALL_DONE', 4)
        if p:
            log('第 A 階段量測沒有完成：' + p + '；停止'); return
        s = state(A=True)
        ws_note('第 A 階段量測完成：Testcases/results/' + NEW + '/phaseA/summary.md')
    if not s.get('A2'):
        rc, text = codex('A2', 'A2.md')
        if not protected_ok('A2'): return
        if rc != 0 and not text.strip():
            log('A2 沒有輸出就結束：檢查 codex 登入 / 模型名稱，修好後重跑同一個指令'); return
        s = state(A2=True)
    note = s.get('note', '')
    while not s.get('B_DONE'):
        n = s.get('B_runs', 0)
        if n >= B_MAX + 2:
            log('B 收尾兩次都沒有寫 B_DONE / NO_FINAL；停止，等使用者處理'); return
        file = 'B.md' if n == 0 else ('B_force.md' if n >= B_MAX else 'B_next.md')
        rc, text, problem = session_with_jobs(f'B{n + 1}', file, os.environ.get('CODEX_EFFORT_B', 'high'), NOTE=note)
        if not protected_ok(f'B{n + 1}'): return
        fails = s.get('fails', 0) + 1 if rc != 0 and not text.strip() else 0
        s = state(fails=fails)
        if fails >= 2:
            log('Codex 連續兩次沒有輸出就結束（rc 非 0）：檢查 codex 登入 / 模型名稱 / 網路，修好後重跑同一個指令'); return
        note = f'【協調者備註】上一次 RUN / 等待的問題：{problem}。請看 log、修正後重跑或撤回。' if problem else ''
        s = state(B_runs=n + 1, note=note)
        if not problem and re.search(r'^\s*NO_FINAL\s*$', text, re.M):
            s = state(B_DONE=True, final=False)
        elif not problem and re.search(r'^\s*B_DONE\s*$', text, re.M):
            s = state(B_DONE=True, final=True)
    log('B 完成')
    if s.get('final') and not s.get('asan'):
        asan_outside()
        s = state(asan=True)
    if s.get('final') and not s.get('C'):
        if not (RES / 'frozen_sources.sha256').exists():
            freeze()
        if (LAB / 'Testcases/edge_seed8').exists() and not (RES / 'final' / 'edge_seed8.done.json').exists():
            log('Testcases/edge_seed8 已經存在（不是這次最終驗證產生的）；停止'); return
        p = run_here('C', [PY, RES / 'scripts' / 'final_validate.py'], f'Testcases/results/{NEW}/final/ALL_DONE', 12)
        if p:
            log('最終驗證有問題：' + p + '；停止'); return
        if not DRY:
            subprocess.run([PY, RES / 'scripts' / 'final_report.py'], cwd=LAB, stdout=(RES / 'orchestrator' / 'final_report.log').open('w'),
                           stderr=subprocess.STDOUT)
        s = state(C=True)
    if not s.get('D'):
        fn = '凍結 sha256 見 Testcases/results/' + NEW + '/freeze.json' if s.get('final') else \
             '沒有保留的改動：沒有凍結、沒有最終驗證；README 只寫診斷、試過沒用的數據與沒做到的'
        codex('D', 'D.md', FINAL_NOTE=fn)
        if not protected_ok('D'): return
        s = state(D=True)
    ok = protected_ok('end') and (not s.get('final') or frozen_ok())
    if ok:
        (RES / 'provenance' / 'protected_at_end.sha256').write_text(protected_manifest())
    log('全部完成' + ('，保護資料夾與凍結原始碼都沒變' if ok else '，但核對失敗（見上）'))
    ws_note('協調者：全部完成' + ('' if ok else '（核對失敗，見 orchestrator.log）'))


if __name__ == '__main__':
    main()
