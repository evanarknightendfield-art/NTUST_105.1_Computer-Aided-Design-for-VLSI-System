#!/usr/bin/env bash
# Pre-global-step Stage 1 placements of CodexChainDP (= its incumbent right
# before the global cap repair): --global-time 0 --no-sa, generous Stage 1
# budget so that slower cores do not change the result. Pinned to the E-cores
# so the timed phase-1 runs on the P-cores are not disturbed.
cd "$(dirname "$0")/../../../.."
R=Testcases/results/ClaudeOpus55_v5
for d in testcases holdout edge; do
  for b in Testcases/$d/*/; do
    n=$(basename $b)
    mkdir -p $R/preglobal/$n
    ( cd $R/preglobal/$n && nice taskset -c 16-19 ../../bin/CodexChainDP/dp ../../../../../$b$n output --global-time 0 --no-sa --stage1-time 400 -v > stdout.txt 2> stderr.txt )
    echo "$n $(head -2 $R/preglobal/$n/stdout.txt | tr '\n' ' ')"
  done
done
