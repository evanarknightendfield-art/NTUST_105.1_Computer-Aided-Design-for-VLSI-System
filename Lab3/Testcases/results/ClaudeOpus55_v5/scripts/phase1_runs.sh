#!/usr/bin/env bash
# Phase 1 measurements (diagnosis.md): one job at a time so the times are comparable.
#   1. local_grade.py on CodexChainDP: 27 cases (testcases + holdout + edge) and L1 (official PASS / time)
#   2. run_ts.py (time-stamped -v stderr) on v4 and ChainDP: the same 27 cases, L1, L2
set -uo pipefail
cd "$(dirname "$0")/../../../.."   # Lab3
export PATH=/home/evan/anaconda3/envs/eda/bin:$PATH
R=Testcases/results/ClaudeOpus55_v5
P1=$R/phase1
mkdir -p $P1
log() { echo "[$(date +%H:%M:%S)] $*" | tee -a $P1/progress.log; }

if [ ! -f $P1/lg_chain/record.csv ]; then
  log "local_grade ChainDP 27"
  python3 Grader/tools/local_grade.py AI_Solutions/CodexChainDP CodexChainDP Testcases/testcases Testcases/holdout Testcases/edge \
    --work $P1/lg_chain --dp-args="-v --trace trace.csv" > $P1/lg_chain.log 2>&1
fi
if [ ! -f $P1/lg_chain_L1/record.csv ]; then
  log "local_grade ChainDP L1"
  python3 Grader/tools/local_grade.py AI_Solutions/CodexChainDP CodexChainDP Testcases/large/L1 \
    --work $P1/lg_chain_L1 --timeout 1200 --dp-args="-v --trace trace.csv" > $P1/lg_chain_L1.log 2>&1
fi
for v in ClaudeOpus55_v4 CodexChainDP; do
  if [ ! -f $P1/ts_$v/record.csv ]; then
    log "run_ts $v 27"
    python3 $R/scripts/run_ts.py --bin $R/bin/$v/dp --out $P1/ts_$v Testcases/testcases Testcases/holdout Testcases/edge > $P1/ts_$v.log 2>&1
  fi
done
for v in ClaudeOpus55_v4 CodexChainDP; do
  if [ ! -f $P1/ts_${v}_large/record.csv ] || ! grep -q '^L2,' $P1/ts_${v}_large/record.csv; then
    log "run_ts $v L1 L2"
    python3 $R/scripts/run_ts.py --bin $R/bin/$v/dp --out $P1/ts_${v}_large --timeout 3600 Testcases/large/L1/L1 Testcases/large/L2/L2 > $P1/ts_${v}_large.log 2>&1
  fi
done
log "phase1 runs done"
