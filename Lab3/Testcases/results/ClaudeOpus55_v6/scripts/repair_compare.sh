#!/usr/bin/env bash
# rows vs negotiation repair from this session's CodexCapDP final legal placement (phase1/lg); timed: holds timing.lock
cd "$(dirname "$0")/../../../.."
R=Testcases/results/ClaudeOpus55_v6
exec 9>$R/timing.lock; flock 9
for b in holdout/h03 holdout/h04 holdout/h05 holdout/h06 holdout/h07 edge/e03_dense97 edge/e12_collapse edge/e08_fragmented; do
  n=$(basename $b)
  [ -s $R/phase1/repair_compare/$n.csv ] && continue
  $R/bin/repair_compare Testcases/$b/$n $R/phase1/lg/CodexCapDP_27/results/CodexCapDP/$n/output_legal.pl 2 1 2 3 > $R/phase1/repair_compare/$n.tmp && mv $R/phase1/repair_compare/$n.tmp $R/phase1/repair_compare/$n.csv
done
echo "- $(date +'%m-%d %H:%M') repair_compare 完成（rows vs 協商，phase1/repair_compare/）" >> AI_Solutions/ClaudeOpus55_v6/WORK_STATE.md
