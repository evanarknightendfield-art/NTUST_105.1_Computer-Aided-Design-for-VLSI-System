接續 AI_Solutions/CodexParallelChain/。只讀 HANDOFF.md（AI_Solutions/ClaudeOpus55_v6/）第 0、4、6、7 節、WORK_STATE.md 與 Testcases/results/CodexParallelChain/diagnosis.md 的排序表。
依 diagnosis.md 的排序改程式（只改 AI_Solutions/CodexParallelChain/src 與 tests）：
1. chain 階段平行化（target windows、ejection chains 分 band 平行；總時間與額外預算不變；結果合法、出錯退回前一步）。
2. 只有診斷顯示 band 邊界損失 ≥ 0.5% 才改平行 SA；改了要保持「同 seed、同執行緒數、固定次數結果一樣」。
每改完一項：C++ 測試（一般；有動平行程式再加 ASan/UBSan 與 TSan），補一個新功能的測試；
然後用 RUN: python3 Testcases/results/CodexParallelChain/scripts/phase2_train.py TAG 跑訓練集 Stage 1，WAIT: Testcases/results/CodexParallelChain/phase2/train/TAG/stage1.done.json。
下一個 session 讀 phase2/train/TAG/compare.md 決定保留或撤回（數據寫進 WORK_STATE）。
只用 testcases + holdout 決定參數；e08 / e16 不能拿來調參。每一項的保留條件、停損（同一項最多 3 輪）與「B 完成」的最後檢查在 HANDOFF.md 第 7 節。
回覆不超過 15 行。
