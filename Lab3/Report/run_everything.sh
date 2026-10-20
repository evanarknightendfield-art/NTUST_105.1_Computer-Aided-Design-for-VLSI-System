#!/usr/bin/env bash
# 一行重跑報告用的實驗：編譯 → 產生測資（還沒有的話）→ 跑實驗 → 表格與圖
#   ./run_everything.sh              你的程式 Lab3/M11502102：原始結果放 Testcases/results/report/，表格和圖放 Report/{tables,figures}
#   ./run_everything.sh reference    參考解，全部實驗：原始結果放 Testcases/results/reference_report/，表格和圖放 AI_Workspace/reference_report/
#   ONLY=main ./run_everything.sh    只跑某幾組（main,holdout,stage1,sa,effort,seeds）
set -e
cd "$(dirname "$0")"
LAB3="$(cd .. && pwd)"

if [ "$1" = "reference" ]; then
    PROJ="$LAB3/AI_Solutions/ClaudeOpus55"
    OUT="$LAB3/AI_Workspace/reference_report"
    ONLY="${ONLY:-main,holdout,stage1,sa,effort,seeds}"
else
    PROJ="$LAB3/M11502102"
    OUT="$LAB3/Report"
    ONLY="${ONLY:-main,holdout}"
fi

cmake -S "$PROJ" -B "$PROJ/build" > /dev/null
cmake --build "$PROJ/build" -j
if [ ! -d "$LAB3/Testcases/testcases" ] || [ ! -d "$LAB3/Testcases/holdout" ]; then
    "$LAB3/Testcases/make_testcases.sh"
fi

cd scripts
python3 run_experiments.py --dp "$PROJ/build/dp" --out "$OUT" --only "$ONLY"
python3 make_assets.py --root "$OUT"
