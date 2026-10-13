# M11502102_Chatgpt6AstraUltra — Clock Buffer Insertion

以 `M11502102_ClaudeOpus55Ultracode_2` 的 C++17 程式為基礎及比較基準，保留兩條搜尋鏈、分區合併及課程要求的輸出格式。原版資料夾沒有修改。

## 編譯與執行

```bash
conda activate eda  # 此台電腦的 cmake 位於 eda 環境；評分機不需要這一行
cmake -S . -B build
cmake --build build -j 4
./build/cbi ../testcases/input001.cbi output001.cbi
./build/cbi ../testcases/input010.cbi output010.cbi -t 5 -s 1
```

介面：`cbi INPUT OUTPUT [-t seconds] [-s seed] [-v] [-d PREFIX]`。
預設時間與基準相同：N ≤ 12 為 5 秒，其餘 `min(30, 10 + N/100)` 秒；最多兩條執行緒。
時間預算控制退火；建樹、驗證、同步及 I/O 可能造成少量超時，並非硬即時保證。
stdout 僅一行 `T_max: ..., T_min: ..., Score: ...`，詳細紀錄在 stderr。

## 改進內容

### 完整保留真正低 Score 的合法候選

基準的退火 energy 是 `cost + w_skew × (skew + 0.3σ)`，但題目只計 `Score = cost + w_skew × skew`。
某候選即使 Score 更低，也可能因 σ 變大被退火拒絕；基準只保存已接受且距上次保存足夠久的解。
新版在合法性與分區檢查後、Metropolis 決定前，記錄每一個 Score 創新低的候選。這保留探索方式，又不漏掉已發現的好答案。

為了避免每次進步都複製 O(N) 的完整樹，`tree.cpp` 追蹤前一次 checkpoint 以來改變過的座標、parent、kind、alive。
保存成本為 O(不同的結構異動節點數 + 新增槽位數)，不是固定 O(N)。被保存的試探若遭 rollback，還原造成的差異會追蹤到下一次 checkpoint。
單一最佳紀錄對所有已通過合法性及分區檢查的候選有效；這並不代表已搜尋所有可能的樹。

### 保留合併前後三份候選

分區平行搜尋完成後，基準以合併解覆蓋第二條鏈，再只比較兩份解。
新版分別驗證並比較 chain 0、chain 1、merged 三份解，避免合法但分數較差的合併結果蓋掉好答案。

### FLUTE 啟發的分群候選

參考資料是 `../../brief/flute-2.5-mar08.pdf`，Chris Chu 的 FLUTE 簡報：

- 第 2 頁：RSMT 的目標是最小總線長。
- 第 4、9 頁：低 pin 數的 lookup table 與 Hanan grid。
- 第 26–33 頁：高 pin 數的遞迴 net breaking，考慮多個方向與切點，以候選數控制品質和成本。
- 第 34 頁：合併時改善局部子樹。

本作業沒有共享線段折抵的 RSMT 計分，且有 fanout、outgoing 總長、buffer 成本、skew、禁用重疊座標等限制。
因此**沒有直接套用 FLUTE lookup table、Hanan-grid 最優性或 RSMT 最優解**。

`construct_spatial.cpp` 借用「多個方向與切點」：較高層用 Manhattan compactness 篩選；底層最多試 6 個兩層 CBI 子樹候選，計算合法型號成本及子樹 skew，再 rollback。
最後仍把原版兩種建樹、候選分群兩種建樹各自平衡、獨立驗證，以實際全樹 Score 選起點。
因此建樹階段保留原始方案；但不同起點及建樹時間會改變後續退火結果，**不保證最終每筆都比基準好**。

### 解析 Manhattan 菱形交點

`diamond.h` 沿用 AstraMedium 已驗證的解析求交，取代 delay buffer 的半徑逐點掃描。
選點順序與原版相同。只需解常數個線性區間，再跳過可行但被佔用的位置，避免搜尋成本隨巨大座標半徑線性成長。

### 未預設啟用的精確座標搜尋

`exact_placement.cpp` 可以固定一個 buffer 的 x 或 y，將容量門檻、延遲上下界交點與座標折點列為候選，求該直線上的最低實際 Score。
它通過與窮舉對照的正確性測試，但開發集未顯示額外得分收益，所以預設權重為 0。
可用 `CBI_EXACT_WEIGHT=4` 重現實驗；不能把單條直線的最優性說成整棵 clock tree 全域最優。

## 測試與重現

```bash
cmake -S . -B build-check -DCBI_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS='-fsanitize=undefined -fno-sanitize-recover=all'
cmake --build build-check -j 4
ctest --test-dir build-check --output-on-failure
```

測試包含：

- 128,000 次隨機交易及 58,235 次 snapshot 全欄位對照，覆蓋新增、刪除、重用、保存試探後 rollback。
- 250,000 組解析菱形求交與原始逐點掃描對照。
- 4,386 次精確座標最低 Score 與所有合法整數座標窮舉對照（此操作預設停用）。

跑分工具使用課程的 `../grader/grader/verifier.py`，並確認 stdout Score 一致；每個 solver 綁定相同兩個 CPU，依序執行。
測資、種子、逐筆 CSV、輸出與 stderr 保存在 `experiments/`。

```bash
python3 tests/compare.py --seconds 5 --seeds 1 --out experiments/reproduced_testcases
python3 tests/ablate.py --inputs experiments/holdout_inputs \
  --variants baseline,core,spatial_core --seconds 5 --out experiments/reproduced_holdout
```

`CBI_SPATIAL=0` 關閉 FLUTE 啟發分群以做消融實驗；`CBI_EXACT_WEIGHT=0` 停用精確座標 move（預設）。
其餘 `CBI_THREADS` 等原版實驗開關仍可使用。
正式編譯與執行只依賴 C++17 標準函式庫與 pthread；Python、課程 verifier 只用於本地測試。

完整比較及限制見 [COMPARISON.md](COMPARISON.md)。時間制退火即使 seed 相同，也會受機器負載及排程影響。

## 本次實測結果

相同兩核／兩執行緒下，15 筆既有測資（各 5 秒）幾何平均比值 0.999451，約持平；12 筆新測資（各 5 秒）為 0.989798，改善約 1.02%。10 萬 sinks（兩版皆 30 秒）由 281768 降至 281142，改善約 0.22%。28 組正式比較的輸出均合法。這是固定 seed 的實測，不保證每筆都勝出。
