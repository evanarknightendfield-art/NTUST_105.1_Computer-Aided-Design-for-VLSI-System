#!/usr/bin/env bash
# run_codex.sh [NEW] — start the whole next-version pipeline (base ClaudeOpus55_v7) in the background.
# It survives closing the terminal; progress: codex_status.sh NEW. Rerun the same command to resume after a stop.
#   env: CODEX_MODEL=<model id>  CODEX_EFFORT_B=high  CODEX_EFFORT=<A2/D effort>  B_MAX=12  DRY_RUN=1 (no Codex, no jobs)
set -euo pipefail
cd "$(dirname "$0")/../../../.."          # -> Lab3
NEW=${1:-CodexAstra}
command -v codex >/dev/null || { echo "找不到 codex CLI"; exit 1; }
for d in Testcases/testcases Testcases/holdout Testcases/edge Testcases/large/L1 Testcases/large/L2; do
  [ -d "$d" ] || { echo "缺少 $d：先跑 ./Testcases/make_testcases.sh --large"; exit 1; }
done
if pgrep -f "orchestrate_codex.py $NEW\$" > /dev/null; then echo "$NEW 的協調者已經在跑"; exit 1; fi
mkdir -p "Testcases/results/$NEW"
setsid nohup /home/evan/anaconda3/bin/python3 Testcases/results/ClaudeOpus55_v7/codex_kit/orchestrate_codex.py "$NEW" \
  >> "Testcases/results/$NEW/orchestrator.nohup.log" 2>&1 < /dev/null &
echo "已在背景啟動（pid $!）。看進度：bash Testcases/results/ClaudeOpus55_v7/codex_kit/codex_status.sh $NEW"
