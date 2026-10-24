#!/usr/bin/env python3
"""tests_queue.py TAG — the C++ suite (tests/run_tests.py) plain, ASan+UBSan (detect_leaks=1) and TSan, under timing.lock so
the runs never overlap a timed program. Logs: Testcases/results/ClaudeOpus55_v7/tests*/ (run_tests.py) and phase2/tests/TAG/*.log."""
import hashlib, os, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, PY, note  # noqa: E402

tag = sys.argv[1]
d = R / 'phase2' / 'tests' / tag
q = Queue(d)
src = LAB / 'AI_Solutions' / R.name
h = hashlib.sha256(b''.join(p.read_bytes() for p in sorted((src / 'src').iterdir()))).hexdigest()
(d / 'src_sha256.txt').write_text(h + '\n')
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',
           TSAN_OPTIONS='halt_on_error=1')
ok = all([q.run('plain', [PY, src / 'tests/run_tests.py'], env=env, quiet=True),
          q.run('asan_ubsan', [PY, src / 'tests/run_tests.py', '--sanitize'], env=env, quiet=True),
          q.run('tsan', [PY, src / 'tests/run_tests.py', '--tsan'], env=env, quiet=True)])
note(f'phase2/tests/{tag}：一般 / ASan+UBSan / TSan {"全部通過" if ok else "有失敗，看 log"}（src {h[:12]}）')
