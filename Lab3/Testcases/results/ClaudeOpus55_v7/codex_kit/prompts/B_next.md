你是 Lab3 新版本 {NEW} 的開發者（以 ClaudeOpus55_v7 為底），階段 B 繼續。
只讀：AI_Solutions/{NEW}/WORK_STATE.md 最後兩節（上一輪做了什麼、等的是哪個結果）、那個結果的摘要檔（pair.md / summary.md / gates.json），
以及 AI_Solutions/ClaudeOpus55_v7/HANDOFF.md 第 0、7 節。需要時才 grep diagnosis.md 或原始碼片段。
1. 依第 7 節判定上一輪：保留（記下保留點的 src sha256）或撤回（還原到上一個保留點），數據路徑寫進 WORK_STATE。
2. 決定下一步：同一項的下一輪（最多 3 輪）、下一項、或 B 的最後檢查（第 7 節）。改程式 → 編譯與一般測試 → RUN / WAIT → 結束。
3. 最後檢查都已跑完而且成立：WORK_STATE 寫「B 完成」，回覆最後一行只寫 B_DONE（全部撤回則寫 NO_FINAL）。
{NOTE}
