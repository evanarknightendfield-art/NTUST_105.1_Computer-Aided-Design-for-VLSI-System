#!/usr/bin/env bash
# 準備測資（測資資料夾不放進 git，用這支產生 / 下載）
#   ./make_testcases.sh            testcases/ + ibm-place2/
#   ./make_testcases.sh --large    再加 large/（L1、L2）
set -e
cd "$(dirname "$0")"
LAB3="$(cd .. && pwd)"

# testcases/：課程給的 bench.tgz（toy、ibm01、ibm05）
mkdir -p testcases
tar xzf bench.tgz --strip-components=1 -C testcases

# ibm-place2/：10/28 規定說隱藏測資從這組 benchmark 改出來；原樣下載、原樣解壓，不做任何修改。
# 原始的 .pl 只有 I/O pad 的位置，所有 cell 都在 (0, 0)（沒有 global placement）。
B=http://vlsicad.eecs.umich.edu/BK/Slots/cache/er.cs.ucla.edu/benchmarks/ibm-place2
mkdir -p ibm-place2/download && cd ibm-place2/download
for f in bookshelf/ibm{01,02,07,08,09,10,11,12}-v2-bookshelf.tar.gz bookshelf/ibm-place2-all-bookshelf-nopad.tar.gz \
         LEFDEF/ibm{01,02,07,08,09,10,11,12}-v2-lefdef.tar.gz LEFDEF/ibm-place2-all-lefdef-nopad.tar.gz \
         pl-files-dragon.tar.gz readme-bookshelf.html readme-lefdef.html stats.gif comparison.gif; do
  [ -s "$f" ] || { mkdir -p "$(dirname "$f")"; curl -fL --retry 3 -o "$f" "$B/$f"; }
done
cd ..
for n in 01 02 07 08 09 10 11 12; do tar xzf "download/bookshelf/ibm$n-v2-bookshelf.tar.gz"; done
mkdir -p nopad && tar xzf download/bookshelf/ibm-place2-all-bookshelf-nopad.tar.gz -C nopad
cd ..

# large/：規模測試（v4 加的），20 萬與 100 萬個 cell，約 1 分鐘、160 MB。
# 不需要時可以略過：./make_testcases.sh 只會在給 --large 時產生。
if [ "$1" = "--large" ]; then
  python3 "$LAB3/Grader/tools/gen_bench.py" L1 large --cells 200000 --util 0.80 --macros 12 --pads 1000 \
    --hotspots 4 --split 0.1 --geom ibm --seed 7
  python3 "$LAB3/Grader/tools/gen_bench.py" L2 large --cells 1000000 --util 0.70 --macros 20 --pads 2000 \
    --hotspots 6 --split 0.1 --geom unit --seed 8
fi
