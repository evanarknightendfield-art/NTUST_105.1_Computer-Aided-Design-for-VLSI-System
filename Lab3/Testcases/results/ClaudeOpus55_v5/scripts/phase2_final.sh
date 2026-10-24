#!/usr/bin/env bash
# Final validation of the frozen ClaudeOpus55_v5 (one timed job at a time).
#   1. local_grade.py: testcases + holdout (11), edge via run_edge.sh (16), L1, L2
#   2. Stage 1 caches for the HPWL experiments (time-stamped, --no-sa)
#   3. HPWL, 3 seeds, holdout + edge: same SA attempts / same total time / default parameters
#   4. new edge seed (generated here, after freezing): local_grade for v4, ChainDP, v5
set -uo pipefail
cd "$(dirname "$0")/../../../.."   # Lab3
export PATH=/home/evan/anaconda3/envs/eda/bin:$PATH
R=Testcases/results/ClaudeOpus55_v5
F=$R/final
mkdir -p $F
log() { echo "[$(date +%H:%M:%S)] $*" | tee -a $F/progress.log; }
V5=AI_Solutions/ClaudeOpus55_v5
NEWSEED=20261021

(cd $V5 && find src tests CMakeLists.txt Makefile -type f | sort | xargs sha256sum) | diff -q - $R/frozen_sources.sha256 \
  && log "sources match frozen hashes" || { log "SOURCES CHANGED since freeze"; exit 1; }

if [ ! -f $F/lg_v5/record.csv ]; then
  log "local_grade v5: testcases + holdout"
  python3 Grader/tools/local_grade.py $V5 ClaudeOpus55_v5 Testcases/testcases Testcases/holdout \
    --work $F/lg_v5 --dp-args=-v > $F/lg_v5.log 2>&1
fi
if [ ! -f Testcases/results/edge/ClaudeOpus55_v5/record.csv ]; then
  log "run_edge.sh ClaudeOpus55_v5"
  EDGE_WORK_ROOT=$F EDGE_DP_ARGS=-v Testcases/results/run_edge.sh ClaudeOpus55_v5 > $F/run_edge.log 2>&1
fi
for L in L1 L2; do
  if [ ! -f $F/lg_v5_$L/record.csv ]; then
    mkdir -p $F/large_$L && ln -sfn ../../../../large/$L $F/large_$L/$L
    log "local_grade v5 $L"
    python3 Grader/tools/local_grade.py $V5 ClaudeOpus55_v5 $F/large_$L --work $F/lg_v5_$L --timeout 1800 \
      --dp-args=-v > $F/lg_v5_$L.log 2>&1
  fi
done
if [ ! -f $F/ts_v5_stage1/record.csv ] || [ $(wc -l < $F/ts_v5_stage1/record.csv) -lt 25 ]; then
  log "Stage 1 cache v5 (holdout + edge)"
  rm -rf $F/ts_v5_stage1
  python3 $R/scripts/run_ts.py --bin $F/lg_v5/home/ClaudeOpus55_v5/build/dp --out $F/ts_v5_stage1 --dp-args=--no-sa \
    Testcases/holdout Testcases/edge > $F/ts_v5_stage1.log 2>&1
fi
V4="--v ClaudeOpus55_v4 AI_Solutions/ClaudeOpus55_v4/src $R/phase1/ts_ClaudeOpus55_v4"
V5V="--v ClaudeOpus55_v5 $V5/src $F/ts_v5_stage1"
log "HPWL same SA attempts (5M, 1 thread, common start = v4 Stage 1)"
python3 $R/scripts/hpwl_compare.py fixed $F/hpwl_fixed --groups holdout edge $V4 $V5V > $F/hpwl_fixed.log 2>&1
log "HPWL same total time (slower Stage 1 + 12 s)"
python3 $R/scripts/hpwl_compare.py equal $F/hpwl_equal --groups holdout edge $V4 $V5V > $F/hpwl_equal.log 2>&1
if [ ! -d Testcases/edge_seed3 ]; then
  log "generate new edge seed $NEWSEED -> Testcases/edge_seed3"
  /home/evan/anaconda3/bin/python3 Grader/tools/gen_edge.py Testcases/edge_seed3 --seed $NEWSEED --check > $F/gen_edge_seed3.log 2>&1
fi
for v in ClaudeOpus55_v4 CodexChainDP ClaudeOpus55_v5; do
  if [ ! -f $F/seed3_$v/record.csv ]; then
    log "local_grade $v on edge_seed3"
    python3 Grader/tools/local_grade.py AI_Solutions/$v $v Testcases/edge_seed3 --work $F/seed3_$v > $F/seed3_$v.log 2>&1
  fi
done
log "HPWL default parameters (SA 60 s + polish 30 s from each version's own Stage 1)"
python3 $R/scripts/hpwl_compare.py default $F/hpwl_default --groups holdout edge $V4 $V5V > $F/hpwl_default.log 2>&1
log "final runs done"
