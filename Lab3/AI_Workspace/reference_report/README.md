# reference_report — 參考解的實驗與範例報告（格式參考）

用 `Report/` 的腳本跑參考解 `AI_Solutions/ClaudeOpus55` 得到的：

```bash
cd Lab3/Report
./run_everything.sh reference
```

| 檔案 / 資料夾 | 內容 |
|---|---|
| `reference_report.md` / `.docx` | 範例報告（中文，約 3 頁；docx = `pandoc reference_report.md -o reference_report.docx --resource-path=.`） |
| `tables/main.md` | toy / ibm01 / ibm05：總位移、最大位移、HPWL（GP / legal / SA）、時間 |
| `tables/holdout.md` | h01 ~ h08 同上 |
| `tables/stage1.md` | Tetris → Abacus → 微調 → evict 各步的位移 |
| `tables/sa_ablation.md` | SA 對照（貪心、p0、optimal region、range limiter） |
| `tables/effort.md`、`seeds.md` | 嘗試次數、seed 變異 |
| `figures/` | 報告用的圖（說明見 `Report/README.md`） |
| `results/` | 每次執行的原始結果（`.json`）、SA trace（`.csv`）、main 的輸出 `.pl` |

testcases 那幾組是 2026-10-15 跑的（`results/run_log_2026-10-15.txt`），holdout 是 2026-10-18 跑的。
