你是 Lab3 新版本 {NEW} 的開發者（以 ClaudeOpus55_v7 為底）。現在是階段 B 的第一個 session。
只讀：AI_Solutions/ClaudeOpus55_v7/HANDOFF.md 第 0、2、6、7 節；Testcases/results/{NEW}/diagnosis.md 的排序表；AI_Solutions/{NEW}/WORK_STATE.md 最後一節。
依 diagnosis.md 的排序，做第一項的第 1 輪：
1. 只改 AI_Solutions/{NEW}/src 與 tests（原始碼註解標「{NEW}:」並寫理由與數據來源，風格跟周圍一樣）。新功能要有測試；有動平行程式要能在 TSan 下跑。
2. 本 session 內：編譯（taskset -c 16-19）、跑 C++ 一般測試（python3 AI_Solutions/{NEW}/tests/run_tests.py）、用一兩筆小測資確認合法。
3. 用 HANDOFF 第 2 節的腳本排訓練（TAG 用「項目_輪」，例如 T1_r1），寫 RUN / WAIT 後結束。
下一個 session 會讀結果決定保留或撤回。
