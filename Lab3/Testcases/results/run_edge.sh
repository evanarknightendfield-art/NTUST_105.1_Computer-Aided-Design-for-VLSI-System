#!/usr/bin/env bash
# Run solutions on the edge suite with local_grade.py; keep the small records in
# Testcases/results/edge/<version>/ (tracked), the bulky outputs in
# Testcases/results/local_grade/edge_<version>/ (not tracked).
#   Testcases/results/run_edge.sh ClaudeOpus55_v4 CodexWindowDP ...
set -euo pipefail
cd "$(dirname "$0")/../.."
for v in "$@"; do
  work=${EDGE_WORK_ROOT:-Testcases/results/local_grade}/edge_$v
  mkdir -p "$work"
  if python3 Grader/tools/local_grade.py "AI_Solutions/$v" "$v" Testcases/edge --work "$work" "--dp-args=${EDGE_DP_ARGS:-}" > "$work/driver.log" 2>&1; then
    mkdir -p "Testcases/results/edge/$v"
    cp "$work/record.csv" "$work/summary.csv" "Testcases/results/edge/$v/"
    cp "$work/logs/$v.log" "Testcases/results/edge/$v/run.log"
    echo "done $v: $(tail -1 "$work/summary.csv")"
  else
    status=$?
    echo "failed $v (exit $status): $work/driver.log" >&2
    exit "$status"
  fi
done
