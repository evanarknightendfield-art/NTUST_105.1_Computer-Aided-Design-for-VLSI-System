#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../../.."
R=Testcases/results/CodexCapDP
CM=/home/evan/anaconda3/envs/eda/bin/cmake
taskset -c 16-19 /home/evan/anaconda3/envs/eda/bin/ctest --test-dir "$R/phase2/build_v5" --output-on-failure -j1 > "$R/phase2/tests_v5.log" 2>&1
python3 "$R/scripts/tune_full.py" full_v5 --bin "$R/phase2/build_v5/dp" > "$R/phase2/full_v5.log" 2>&1
taskset -c 16-19 "$CM" -S AI_Solutions/CodexCapDP -B "$R/phase2/asan_v5" -DCMAKE_BUILD_TYPE=RelWithDebInfo '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' '-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O1 -g' '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -no-pie' > "$R/phase2/asan_configure_v5.log" 2>&1
taskset -c 16-19 "$CM" --build "$R/phase2/asan_v5" -j4 > "$R/phase2/asan_build_v5.log" 2>&1
taskset -c 16-19 /home/evan/anaconda3/envs/eda/bin/ctest --test-dir "$R/phase2/asan_v5" --output-on-failure -j1 > "$R/phase2/asan_tests_v5.log" 2>&1
taskset -c 16-19 python3 - <<'PY'
from pathlib import Path
with Path('AI_Solutions/CodexCapDP/WORK_STATE.md').open('a') as f:
    f.write('\n- full_v5 後的 ASan/UBSan 10 個 C++ 測試已全部完成；見 phase2/asan_tests_v5.log。下一步檢查 full_v5 逐筆結果，決定是否凍結。\n')
Path('Testcases/results/CodexCapDP/phase2/train_v5.done.json').write_text('{"complete": true}\n')
PY
