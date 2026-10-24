接續 AI_Solutions/CodexParallelChain/。只讀 HANDOFF.md（AI_Solutions/ClaudeOpus55_v6/）第 0、1、6、7 節與 WORK_STATE.md。
協調者在沙箱外補跑的完整 ASan/UBSan 結果在 Testcases/results/CodexParallelChain/phase2/asan_outside.log（最後幾行），請記進 WORK_STATE。
1. 確認 WORK_STATE 有「B 完成」，而且第 7 節的最後檢查都有紀錄（最後一次訓練的 src_sha256 等於目前 src）。
   如果所有改動都撤回了：不凍結，寫 WORK_STATE「沒有保留的改動，跳過最終驗證」，最後一行寫 NO_FINAL。
2. 凍結：在 Testcases/results/CodexParallelChain/ 寫 frozen_sources.sha256（AI_Solutions/CodexParallelChain 下 src、tests、CMakeLists.txt、Makefile 的相對路徑，格式 "sha256  相對路徑"）與 freeze.json。
3. 確認 Testcases/edge_seed6 不存在，然後交給協調者：
   RUN: python3 Testcases/results/CodexParallelChain/scripts/final_validate.py --seed-dir edge_seed6 --seed 20261024
   WAIT: Testcases/results/CodexParallelChain/final/ALL_DONE
4. WORK_STATE 寫「等待中：final_validate.py，完成標記 final/ALL_DONE；之後跑 final_report.py」，然後結束。
回覆不超過 8 行。
