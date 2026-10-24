#!/usr/bin/env bash
set -euo pipefail
export PATH=/home/evan/anaconda3/envs/eda/bin:$PATH
export PYTHONDONTWRITEBYTECODE=1
R=Testcases/results/CodexCapDP
S=AI_Solutions/CodexCapDP/WORK_STATE.md
python3 -u Grader/tools/local_grade.py AI_Solutions/CodexCapDP CodexCapDP Testcases/testcases Testcases/holdout --work "$R/run_A_local_grade" > "$R/run_A.log" 2>&1
printf '\n- Run A 完成：`results/CodexCapDP/run_A_local_grade/record.csv`；下一步 run B。\n' >> "$S"
python3 -u Testcases/results/ClaudeOpus55_v5/scripts/run_ts.py --bin "$R/run_A_local_grade/home/CodexCapDP/build/dp" --out "$R/run_B_ts" Testcases/testcases Testcases/holdout > "$R/run_B.log" 2>&1
printf '\n- Run B 完成：`results/CodexCapDP/run_B_ts/record.csv`；下一步 cap/chain/L2 診斷。\n' >> "$S"
touch "$R/baseline.done"
