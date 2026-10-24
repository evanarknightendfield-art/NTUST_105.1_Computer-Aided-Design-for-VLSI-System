#!/usr/bin/env bash
# codex_after.sh MARKER PROMPT_FILE LOG — wait (in the shell, costs no tokens) until MARKER exists, then start
# ONE fresh, non-interactive Codex session with the prompt in PROMPT_FILE. Its last message goes to LOG.last.
#
#   nohup Testcases/results/ClaudeOpus55_v6/scripts/codex_after.sh \
#         Testcases/results/<NEW>/final/ALL_DONE  next_prompt.md  Testcases/results/<NEW>/codex_D.log &
#
# Run from Lab3. Uses `codex exec` with the workspace-write sandbox (no network, writes only inside Lab3).
# Gives up after MAX_WAIT_HOURS (default 12) so a crashed pipeline does not leave it waiting forever.
set -u
marker=${1:?marker file}; prompt=${2:?prompt file}; log=${3:?log file}
max=$(( ${MAX_WAIT_HOURS:-12} * 3600 )); t=0
while [ ! -e "$marker" ]; do
  sleep 60; t=$((t + 60))
  if [ "$t" -ge "$max" ]; then echo "$(date) gave up waiting for $marker" >> "$log"; exit 1; fi
done
echo "$(date) $marker found; starting codex" >> "$log"
codex exec -C "$(pwd)" -s workspace-write -o "$log.last" - < "$prompt" >> "$log" 2>&1
echo "$(date) codex exited with $?" >> "$log"
