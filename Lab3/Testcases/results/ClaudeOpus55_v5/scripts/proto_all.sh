#!/usr/bin/env bash
# capsearch prototype (scratch build) on the 27 pre-global placements, E-cores only
cd "$(dirname "$0")/../../../.."
R=Testcases/results/ClaudeOpus55_v5
echo "bench,max0,max1,tot0,tot1,att,feas,acc,sec,legal"
for d in testcases holdout edge; do for b in Testcases/$d/*/; do n=$(basename $b);
  CAP_T2=0.01 CAP_WARM=1 timeout 300 taskset -c 16-19 /tmp/claude-1000/-home-evan-Desktop-NTUST-105-1-Class-Computer-Aided-Design-for-VLSI-System/eb69fe17-0b6c-45d2-9324-580dc01babc4/scratchpad/v5new/driver_cap $b$n $R/preglobal/$n/output_legal.pl ${SECS:-10} ${RATIO:-1.005} 4 2>/dev/null | sed "s|Testcases/[a-z_0-9]*/[a-z0-9_]*/||" || echo "$n,failed"
done; done
