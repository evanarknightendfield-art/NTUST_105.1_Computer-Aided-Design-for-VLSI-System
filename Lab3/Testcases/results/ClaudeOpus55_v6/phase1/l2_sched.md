# L2：同一個合法起點，SA / polish 的時間分配

sa_bench（CodexCapDP 的 SA / polish 原始碼）、自動執行緒、各 seed 一次。比值 = SA30→P60 ÷ SA60→P30（同起點同 seed）。

| 起點 | seed | SA60→P30 HPWL | SA30→P60 HPWL | 比值 | Stage 2 秒（60/30 vs 30/60） |
|---|---:|---:|---:|---:|---|
| ClaudeOpus55_v5 | 1 | 1.6722e+09 | 1.6190e+09 | 0.9682 | 94.4 / 91.0 |
| ClaudeOpus55_v5 | 2 | 1.6736e+09 | 1.6215e+09 | 0.9689 | 94.4 / 91.0 |
| ClaudeOpus55_v5 | 3 | 1.6734e+09 | 1.6198e+09 | 0.9679 | 94.4 / 91.0 |
| CodexCapDP | 1 | 1.6801e+09 | 1.6246e+09 | 0.9670 | 94.5 / 91.0 |
| CodexCapDP | 2 | 1.6803e+09 | 1.6248e+09 | 0.9670 | 94.6 / 91.0 |
| CodexCapDP | 3 | 1.6774e+09 | 1.6260e+09 | 0.9694 | 94.4 / 91.0 |

- 起點 ClaudeOpus55_v5：比值平均 0.9683，範圍 0.9679–0.9689（3 seeds）
- 起點 CodexCapDP：比值平均 0.9678，範圍 0.9670–0.9694（3 seeds）
- SA60->P30：起點 CodexCapDP ÷ ClaudeOpus55_v5 的 HPWL 平均 = 1.0037
- SA30->P60：起點 CodexCapDP ÷ ClaudeOpus55_v5 的 HPWL 平均 = 1.0031
