
【非互動執行的規則（由協調者自動開 session，使用者在睡覺）】
- 這是 codex exec 的單次 session，沙箱裡啟動的背景工作會在 session 結束時被殺掉（第 A 階段的 nohup 就是這樣死掉的）。
  所以不要自己啟動超過 5 分鐘的計算；把要跑的東西寫成腳本，然後在最後回覆的最後幾行寫：
    RUN: <從 Lab3 目錄執行的完整命令>        （可以多行，依序執行）
    WAIT: <完成標記的路徑，相對 Lab3>
  協調者會在沙箱外依序執行 RUN、等 WAIT 出現，再開下一個 session。命令不要加 taskset（計時程式不能綁 E-core；編譯已由 v6q 綁）。
- 要跑的東西都在 5 分鐘內的話（例如 C++ 測試），直接在本 session 跑完，不要寫 RUN/WAIT。
- ASan：LeakSanitizer 在沙箱內會因 ptrace 失敗，請用 ASAN_OPTIONS=detect_leaks=0 跑並註明；協調者會在沙箱外補跑完整的 ASan。
- 階段 B 全部完成（HANDOFF.md 第 7 節）時，最後一行只寫：B_DONE
- 不 commit、不加任何 AI 署名；不要問問題（沒有人會回答），自己依 HANDOFF.md 判斷並把判斷寫進 WORK_STATE.md。
