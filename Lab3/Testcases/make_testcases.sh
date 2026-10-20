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

# edge/：16 筆「現有測資沒測到的情況」（過擬合檢查；5 秒、15 MB），見 README.md
# edge_seed2/：同一個產生器換 seed（CodexChainDP 的最後驗證）。--check 會驗證每筆有解，並留下 NAME_ref.pl
# 亂數跟 numpy 版本有關：numpy 2.4.6 兩組都跟已量過的一模一樣；2.5.3 的 edge_seed2/e04 會不同。
# 指定 Python：PY=/home/evan/anaconda3/bin/python3 ./make_testcases.sh
PY="${PY:-python3}"
"$PY" "$LAB3/Grader/tools/gen_edge.py" edge --check
"$PY" "$LAB3/Grader/tools/gen_edge.py" edge_seed2 --seed 20261020 --check

# large/：規模測試（v4 加的），20 萬與 100 萬個 cell，約 1 分鐘、160 MB。
# 不需要時可以略過：./make_testcases.sh 只會在給 --large 時產生。
if [ "$1" = "--large" ]; then
  python3 "$LAB3/Grader/tools/gen_bench.py" L1 large --cells 200000 --util 0.80 --macros 12 --pads 1000 \
    --hotspots 4 --split 0.1 --geom ibm --seed 7
  python3 "$LAB3/Grader/tools/gen_bench.py" L2 large --cells 1000000 --util 0.70 --macros 20 --pads 2000 \
    --hotspots 6 --split 0.1 --geom unit --seed 8
fi
