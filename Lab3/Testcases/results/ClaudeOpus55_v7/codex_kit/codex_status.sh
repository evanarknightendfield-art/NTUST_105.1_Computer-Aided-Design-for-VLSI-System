#!/usr/bin/env bash
# codex_status.sh [NEW] — where the pipeline is (reads files only; costs no tokens).
cd "$(dirname "$0")/../../../.." || exit 1
NEW=${1:-CodexAstra}; R="Testcases/results/$NEW"
pid=$(pgrep -f "orchestrate_codex.py $NEW\$" | head -1)
echo "協調者：${pid:+執行中 pid $pid}${pid:-沒有在跑}"
[ -f "$R/orchestrator_state.json" ] && echo "狀態：$(tr -d '\n' < "$R/orchestrator_state.json")"
echo "--- orchestrator.log 最後 12 行"; tail -n 12 "$R/orchestrator.log" 2>/dev/null
echo "--- 最近完成的計時步驟"; find "$R" -name '*.done.json' -newer "$R/orchestrator_state.json" -printf '%TY-%Tm-%Td %TH:%TM  %P\n' 2>/dev/null | sort | tail -n 5
echo "--- WORK_STATE 最後 8 行"; tail -n 8 "AI_Solutions/$NEW/WORK_STATE.md" 2>/dev/null
