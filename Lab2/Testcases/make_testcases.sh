#!/usr/bin/env bash
# 重新產生測資（不上傳 git，用這支產生，結果每次都一模一樣）
set -e
cd "$(dirname "$0")"
LAB2="$(cd .. && pwd)"

# testcases/：001 = 課程範例，002–015 由 gen_input.py 產生（固定 seed）
python "$LAB2/Grader/tools/gen_input.py" --suite testcases
cp "$LAB2/Example/input.cbi" testcases/input001.cbi

# holdout/：報告用的 12 筆新測資（seed 2026）
(cd "$LAB2/Report/scripts" && python gen_tests.py --name holdout --count 12)
