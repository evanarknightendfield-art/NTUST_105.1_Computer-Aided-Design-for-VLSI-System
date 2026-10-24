已確認 B 完成、最後檢查紀錄齊全，訓練 src_sha256 等於目前 src。
沙箱外 ASan/UBSan 10/10 通過，已補入 WORK_STATE。
已建立 frozen_sources.sha256（53 檔）與 freeze.json，確認 edge_seed6 不存在。
WORK_STATE 已記錄等待狀態，完成後跑 final_report.py。
RUN: python3 Testcases/results/CodexParallelChain/scripts/final_validate.py --seed-dir edge_seed6 --seed 20261024
WAIT: Testcases/results/CodexParallelChain/final/ALL_DONE