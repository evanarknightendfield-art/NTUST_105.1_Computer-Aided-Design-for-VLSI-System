# 第一階段診斷協定

日期：2026-10-21（Asia/Taipei）。所有新的輸出集中於本目錄。

## 執行與來源

- Run A：`local_grade.py` 打包並編譯 CodexCapDP 的未修改 v5 src，testcases 3 + holdout 8，預設參數。
- Run B：同一執行檔，`run_ts.py` 加 `-v --trace`，相同 11 筆。這是另一次執行，不把 A 的位移接到 B 的時間。
- Run C：tests/ 的儀器化複本，holdout 8 筆，`--no-sa -v`，預設 Stage 1。保存每次 probe/full attempt、pre-chain/pre-cap、候選 raw placement 與 warm memo。
- 既有 ChainDP 比較基準固定為 `../ClaudeOpus55_v5/phase1/lg_chain/record.csv`。
- 多次執行可能因時間預算而得到不同 Stage 1；run C 含觀測負擔，不能宣稱與 A/B 逐位元相同。
- 計時工作串行；分析與編譯工作用 `taskset -c 16-19`。沒有生成新測資，沒有用 edge 系列調參。
- baseline.sh 與 diagnostic_runs.py 都在背景執行。after_baseline.py 以 inotify 事件等待依賴完成，不定期輪詢。

## 個別實驗

### 最大位移搜尋

每次嘗試記錄 row repair、site fallback、window DP 秒數；probe 無 DP。
失敗解讀為時間內沒有找到可行解，不能當成不可行證明。
full attempt 的接受條件使用整個 cap search 起始總位移 × 1.004；CSV 同時記錄 total_limit。
最後小型設計的 5/4-cell 收尾 DP 不計入某次 full attempt 的 dp_s，與整個 cap search 的時間差分開說明。

### 頂部密度

在 C 的 pre-cap 與 A 的 final legal 兩個起點，各統計上限降低 0.5%、1%、2% 時的違規 cell 數、segment 數與單 segment 最大數。

### 局部 DP

五個指定 holdout 中，選取 C 裡 raw 總位移相對該 attempt 起點增幅最大的可行 full candidate。
三種模式從相同 raw placement 與相同 warm memo 開始，標準 4/3-cell 視窗、8 workers 上限、每次 10 秒：

1. full：原始 windowSweeps。
2. local0：只解包含原違規 cell 或修復中實際移動 cell 的視窗。
3. local4：再納入這些 cell 在修復前後 segment 內各 ±4 個排序鄰居。

mask 固定，不把後續 DP 移動傳播到整張圖。這是小實驗的覆蓋率／品質取捨，並非新正式演算法。
每次完整合法性與總／最大位移由既有函式檢查；全域與局部結果分開保留。

### 修復歸因

另從 C 的 pre-cap 對 0.99 × max 做 rowRepair，seed 7，phase 1 1 秒、cleanup/phase 2 共 1 秒；
phase 2 只有 ≤3000 cell 開啟（2000 moves/cell），符合 v5 的大小切分。
phase 1、cleanup、phase 2 保存 membership；計時結束後用同一 cost/clump 方法重建座標。
重建是反事實量測；實際座標直到最終 placement 才產生。逐 cell 表包含原違規標記、前後 segment/site、位移差與移動距離。

### Chain

holdout 各自從 C 的相同 pre-chain placement/memo 出發，比較 1.5 與 4 秒。
保留 main.cpp 的兩輪預算分配，包括 targeted windows、ejection chains、post-chain windows。
報告的是 cap 之前的淨差，不能直接當成完整 Stage 1 最終差。
≤2000 cell 的 v5 預設 chain 是 3 秒；指定 1.5/4 秒是統一的控制比較。

### L2

使用歷史 v5 final local_grade 的 `output_legal.pl`（hash 與原 record 記錄於 L2_source.json）。
共同起點，seed 1，每個排程僅執行一次：SA60→polish30、SA30→polish60、polish30→SA60。
保留預設 SA 排程與 polish 最多 3 passes，記錄實際次數／時間；時間上限不是實際等耗時保證。
不重跑 L2 的合法化，不用此單 seed 推論跨 seed 的穩定改善。

## 檔案與重啟

- WORK_STATE.md 在 `AI_Solutions/CodexCapDP/`，每一步完成即追加紀錄。
- 每個實驗的 execution.json 保存完整命令、秒數與 return code；done.json 存在則不重跑。
- source_hashes.json：本次 src 起始 hash；protected_sources.json：五個禁止修改資料夾的來源 hash。
- summarize.py 只讀小型 CSV/log，產生 summary.json、tables.md 與彙整 CSV。
- 不 commit，不加入署名；完成 diagnosis.md 後停止。
