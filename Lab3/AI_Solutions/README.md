# AI_Solutions：AI 寫的解法

這個資料夾放 AI 寫的解法，**只有原始碼和說明**，跑出來的結果放在 `../Testcases/results/`。
每個資料夾都可以單獨編譯（`cmake -B build && cmake --build build`），執行方式都是 `dp INPUT OUTPUT`。

| 資料夾 | 用途 |
|---|---|
| [`ClaudeOpus55/`](ClaudeOpus55/README.md) | **參考解**：Abacus + 位移微調 + 單執行緒 SA。教學和報告用它（`AI_Workspace/TUTORIAL.md`、`Report/run_everything.sh reference`、`Grader/README.md`）；沒有全程期限，L1、L2 跑不完 |
| [`Final_Unaccelerated/`](Final_Unaccelerated/README.md) | **未加速最終版**：視窗 DP、最大位移搜尋、多執行緒 SA、polish；不限時間，品質最好，但 L2 要約 3 分鐘 |
| [`Final_Accelerated/`](Final_Accelerated/README.md) | **加速最終版**：同樣的演算法加上全程 50 秒期限和依進度分配時間；守住 10/28 每筆 60 秒規定，四筆測資都 ≤ 50 秒。`MyCode/v5_final` 是它的複本 |

照教學一步一步做到最終版的五個版本在 `../MyCode/`（[總表](../MyCode/README.md)）。
