#!/bin/bash
# 補充實驗（README 用）：一次一個 dp，拿機器鎖。輸出在 extra/out（不進 git），紀錄在 extra/*.log
cd "$(dirname "$0")"
LAB=../../../..
TC=$LAB/Testcases
B=../build
mkdir -p out
exec 9>>$TC/results/timing_global.lock
flock 9
run() { tag=$1; v=$2; c=$3; shift 3; echo "== $tag $c $*"; taskset -c 0-15 /usr/bin/time -f "wall %es" $B/$v/dp $TC/$c out/x "$@" 2>&1 | grep -vE "total-refine pass|^\[read\]"; }
# basic_sa_L1_verbose.log：v2 的基本 SA（M6-2）在 L1 的 -v 紀錄
# v3 Stage 1 各部分（不跑 SA，結果是確定的）
for c in testcases/ibm01/ibm01 testcases/ibm05/ibm05; do
  run abacus v3_refine $c --no-sa --no-refine
  run noevict v3_refine $c --no-sa --no-evict
  run refine v3_refine $c --no-sa
done > v3_stage1.log
for c in large/L1/L1 large/L2/L2; do
  run refine v3_refine $c --no-sa -v
done >> v3_stage1.log
# v3 的 (H) 低溫起點：關掉（p0-low 的門檻設 0）
for c in large/L1/L1 large/L2/L2; do run nolowp0 v3_refine $c --p0-low 0 0.05; done > v3_lowp0.log
