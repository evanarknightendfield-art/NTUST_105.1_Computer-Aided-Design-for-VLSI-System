#!/usr/bin/env bash
# local_grade.py on L1 only (it wants a folder of benchmark folders); waits for phase1_runs.sh
cd "$(dirname "$0")/../../../.."
export PATH=/home/evan/anaconda3/envs/eda/bin:$PATH
R=Testcases/results/ClaudeOpus55_v5
until grep -q "phase1 runs done" $R/phase1/progress.log; do sleep 20; done
echo "[$(date +%H:%M:%S)] local_grade ChainDP L1 (retry)" >> $R/phase1/progress.log
rm -rf $R/phase1/lg_chain_L1
python3 Grader/tools/local_grade.py AI_Solutions/CodexChainDP CodexChainDP $R/phase1/large_L1_only \
  --work $R/phase1/lg_chain_L1 --timeout 1200 --dp-args="-v --trace trace.csv" > $R/phase1/lg_chain_L1.log 2>&1
echo "[$(date +%H:%M:%S)] L1 done" >> $R/phase1/progress.log
