你是 Lab3 新版本 {NEW} 的開發者。協調者已凍結原始碼、跑完最終驗證並產生 Testcases/results/{NEW}/final/summary.md（{FINAL_NOTE}）。
只讀：AI_Solutions/ClaudeOpus55_v7/HANDOFF.md 第 0、8 節；AI_Solutions/{NEW}/WORK_STATE.md 的「B 完成」那一節；final/summary.md（表頭、摘要列、未達標表）。
1. 寫 AI_Solutions/{NEW}/README.md：診斷摘要、改了什麼（保留 / 撤回）、數據（全部取自 final/summary.md 與 B 的摘要檔）、試過沒用的、
   還沒做到的（逐筆列出未達標的測資與差距；HANDOFF 第 8 節沒達到的條件要直接寫沒達到）。
2. AI_Solutions/COMPARISON.md 末尾加「## 14. {NEW}」一節：不改既有內容；加之前把原檔複製到 Testcases/results/{NEW}/provenance/COMPARISON.before.md，
   加完用 cmp -n 確認前段逐位相同。
3. AI_Solutions/HISTORY.md 的表 1、2、5（5.1 與 5.2）、6 各加一列 {NEW}（只加列，不改舊數字；先備份到 provenance/HISTORY.before.md，加完確認只多了那幾行）。
4. Testcases/results/edge/make_table.py 的版本清單（V）最後加 ('{NEW}', '{NEW}')，執行 python3 make_table.py > tables.md（在該資料夾），確認除了多一欄其餘不變。
5. 回覆（≤ 25 行）：每個驗收條件達成多少、逐筆未達標與差距。
