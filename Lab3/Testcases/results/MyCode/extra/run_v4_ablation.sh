#!/bin/bash
# v4 每一項的貢獻（README 用）：一次一個 dp，拿機器鎖。執行檔 = run_versions.py 編好的 build/v4_match_polish/dp
cd "$(dirname "$0")"
TC=../../../../Testcases
B=../build/v4_match_polish/dp
mkdir -p out
exec 9>>$TC/results/timing_global.lock
flock 9
run() { c=$1; shift; echo "== $c $*"; taskset -c 0-15 /usr/bin/time -f "wall %es" $B $TC/$c out/x "$@" 2>&1 | grep -E "^Total|^Maximum|wall"; }
{
for c in testcases/ibm01/ibm01 testcases/ibm05/ibm05; do
  run $c --no-sa --no-match --no-nswap
  run $c --no-sa --no-nswap
  run $c --no-sa
done
run testcases/ibm01/ibm01 --effort 2000
for c in testcases/ibm01/ibm01 large/L1/L1 large/L2/L2; do run $c --no-nswap; done
for c in large/L1/L1 large/L2/L2; do run $c --polish-share 0; done
} > v4_ablation.log
rm -rf out
