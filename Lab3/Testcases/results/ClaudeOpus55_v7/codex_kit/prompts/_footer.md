
【非互動執行的規則（協調者自動開 session，沒有人會回答問題）】
- 這是 `codex exec` 的單次 session；沙箱裡啟動的背景工作會在 session 結束時被殺掉。不要自己跑超過 5 分鐘的計算、不要等待、不要查進度。
  要跑的東西寫在最後回覆的最後幾行：
    RUN: <從 Lab3 目錄執行的完整命令>          （可多行，依序執行；只能是 python3 Testcases/results/{NEW}/scripts/<腳本>.py …）
    WAIT: <完成標記，相對 Lab3>                （最後一個 RUN 產生的 .done.json、summary.md 或 ALL_DONE）
  協調者會在沙箱外依序執行、在 shell 裡等 WAIT 出現（不花 token），再開下一個 session。命令不要加 taskset。
- 5 分鐘內能完成的（編譯、C++ 一般測試、讀摘要）直接在本 session 做。ASan 在沙箱內用 ASAN_OPTIONS=detect_leaks=0 並註明。
- 編譯產物只放 Testcases/results/{NEW}/ 底下（例如 cmake -S AI_Solutions/{NEW} -B Testcases/results/{NEW}/build_dev）；
  不要在任何 AI_Solutions/ 資料夾裡建 build/ 或留下 dp 執行檔（保護資料夾一有新檔案協調者就會停）。
- 開始時先讀 AI_Solutions/{NEW}/WORK_STATE.md 的最後一節；結束前更新它（做了什麼、數據在哪、下一步、RUN/WAIT）。
- 每個指令輸出 ≤ 40 行；不要讀整份 record.csv / log / .pl；回覆 ≤ 15 行。
- 不 commit、不加 AI 署名、不改保護資料夾（HANDOFF 第 0 節）。
