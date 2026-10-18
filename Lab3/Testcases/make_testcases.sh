#!/usr/bin/env bash
# 重新產生測資（測資資料夾不放進 git，用這支產生，結果每次都一模一樣）
set -e
cd "$(dirname "$0")"
LAB3="$(cd .. && pwd)"

# testcases/：課程給的 bench.tgz（toy、ibm01、ibm05）
mkdir -p testcases
tar xzf bench.tgz --strip-components=1 -C testcases

# holdout/：報告用的 8 筆新測資（seed 2026），有 macro、split row、pad、熱點
python3 "$LAB3/Grader/tools/gen_bench.py" --suite holdout --count 8 --seed 2026
