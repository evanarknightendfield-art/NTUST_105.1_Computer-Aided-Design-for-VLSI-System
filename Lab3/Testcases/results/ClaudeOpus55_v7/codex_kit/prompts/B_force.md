你是 Lab3 新版本 {NEW} 的開發者。階段 B 的 session 次數已到上限，這一次要收尾。
只讀：AI_Solutions/{NEW}/WORK_STATE.md 最後兩節、上一輪結果的摘要檔、AI_Solutions/ClaudeOpus55_v7/HANDOFF.md 第 0、7 節。
1. 先判定上一輪（保留或撤回）。還沒達到保留條件的改動全部撤回（src 還原到最後一個保留點）。
2. 最後檢查（第 7 節）還沒對最後的原始碼跑過的：寫 RUN / WAIT（一次排完，依序）後結束；下一個 session 會讀結果。
3. 已經都跑過且成立：WORK_STATE 寫「B 完成」，最後一行只寫 B_DONE；全部撤回則寫 NO_FINAL。
{NOTE}
