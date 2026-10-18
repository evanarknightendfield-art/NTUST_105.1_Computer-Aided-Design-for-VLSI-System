# CodexGapSA — Lab3 獨立實作

使用 C++17 標準函式庫，自行撰寫 Bookshelf parser、合法化及模擬退火。
演算法名稱是本專案的命名，不表示提出有文獻新穎性證明的演算法。

## 編譯與執行

在本資料夾執行：

```bash
make
./dp ../../Testcases/testcases/toy/toy results/manual/toy \
  --seed 1 --time 30 --effort 300 --trace results/manual/toy_trace.csv -v
python3 ../../Grader/tools/check_pl.py ../../Testcases/testcases/toy/toy results/manual/toy_legal.pl
python3 ../../Grader/tools/check_pl.py ../../Testcases/testcases/toy/toy results/manual/toy.pl
```

也支援 `cmake -S . -B build && cmake --build build`，執行檔為 `build/dp`。
輸入可以是 benchmark 前綴、`.aux` 路徑或 benchmark 資料夾。
產生七個檔案：`OUT.{aux,nodes,nets,wts,scl,pl}`、`OUT_legal.pl`。
stdout 僅有題目要求的四行指標，診斷和警告寫入 stderr。

| 選項 | 預設 / 功能 |
|---|---|
| `--seed N` | 1；SA 亂數種子 |
| `--time SEC` | 30；僅限制 SA 時間，讀檔與合法化不計入；0 跳過 SA |
| `--effort N` | 300；最多 `max(1000, N × movable cells)` 次嘗試；0 跳過 SA |
| `--trace FILE` | current、best、temperature、accept_rate 的 CSV |
| `--no-sa` | 只執行 Stage 1 |
| `--no-refine` | 關閉 Stage 1 的三輪位移修整，供消融實驗 |
| `-v` | stderr 顯示進度 |

SA 到達時間或次數上限即停止。時間每 128 次檢查一次，因此可能略超時。
降溫進度取「已用嘗試比例、已用時間比例」較大值；使用時間控制時，同 seed
仍可能因機器速度不同而產生不同結果，不宣稱位元級可重現。

## Stage 1：寬 cell 優先空隙配置與雙目標修整

1. 讀取 subrow，將與 fixed macro 相交的 site 區段扣除。movable 的位置存成
   `(segment, integer site)`；cell 寬度向上取整為 site 數，避免幾何重疊。
2. 依 cell 寬度由大到小配置，同寬時依原始 x、編號排序。
   寬 cell 可放的位置較少，先配置能減少小 cell 將連續空間切碎的風險。
3. 搜尋各 segment 的所有可容納空隙，將原始 x 投影至合法整數 site 區間，
   選 Manhattan 位移最小的位置。按 y 距離向外搜尋，以垂直距離及水平邊界
   作下界剪枝。已使用區域左側的空隙仍可再利用。
4. 三輪修整皆從位移最大的 cell 開始。先嘗試移至更近空隙，再嘗試原始位置
   附近的 cell 交換。交換僅在「兩顆總位移不增加、兩顆最大位移不增加，
   且至少一項嚴格下降」時接受，因此整體總位移與最大位移均不增加。

此方法是啟發式：沒有最佳解或任意可行輸入必定成功的保證。高密度、極端寬度
組合仍可能因碎片化而放不下，此時明確回報錯誤，不輸出未完成 placement。
本次提供的 11 筆測資均成功。修整的雙目標限制也可能阻止「先犧牲總位移才能
降低最大位移」的改善路徑。

## Stage 2：帶最佳解重啟的合法模擬退火

- 狀態：合法 placement；成本：所有 net 的未加權 cell-center HPWL。
  pin offset 與 `.wts` 不納入成本，符合題目。
- proposal 混合相鄰交換、附近隨機位移，以及朝隨機相連 cell 中心的插值目標。
  隨機搜尋窗隨進度縮小。目標落在占用區域時嘗試交換，否則找目標 segment 的
  最近可容納空隙。
- 相鄰不同寬 cell 在原本的聯合外框內反轉順序。其他交換先暫時移除兩顆，
  再依序確認並配置兩個空位；任何一步失敗都還原。如此每個 proposal 都合法。
- HPWL 差值只重算移動 cell 所連 net 的聯集，去除重複 net。
- 先採樣至多 256 個 proposal，使用正成本增量平均值估計初溫
  `T0 = -mean(positive delta) / log(0.65)`。以 `exp(-delta/T)` 接受變差步驟。
- 前 90% 進度以幾何方式降至 `T0 × 10^-4`，最後 10% 採零溫度下降。
  **進度到 60% 時，回到目前最佳解一次，再以當時的低溫繼續搜尋。**
  這能避免短時間預算全部耗在恢復高溫打亂的 placement；trace 中 current 的
  一次突降可能來自這次重啟，不應解讀成單一搬移的改善。
- 以 dirty cell 集合增量更新最佳解快照，結束輸出最佳解；包含初始合法解，
  所以最終 HPWL 不會劣於 Stage 1。Stage 2 不限制相對 GP 的位移，尤其沒有
  net 的 cell 可能漂移；這不影響題目要求的 Stage 2 HPWL 與合法性。

空隙查找最壞需掃過 segment 內所有 cell，單次目標 segment 選擇掃過所有
segments；此版本重視可讀性與驗證，尚未使用 interval tree 加速。

## 輸入處理與限制

- `.nodes` 的 `terminal` / `terminal_NI`，或 `.pl` 的 `/FIXED*` 都視為固定。
  座標、方向與原本 suffix 保留。支援負座標、小數 site、split rows、固定 macro。
- 支援大小寫不同的 Bookshelf 關鍵字、無名稱 net、重複 pin（HPWL 去重不改變值）。
- 原始 toy 第 29 條 net 的 `NetDegree : 22` 實際只有 3 個 pin。
  parser 以 `NetDegree` 分隔 net、使用實際列出的 pin 並警告；另驗證整份檔案
  `NumPins` 與 `NumNets`。沒有修改原始測資。
- 僅支援 horizontal、單 row 高度的 movable cell；不支援旋轉尺寸換算或跨 row
  movable macro。不同 spacing 可讀取，但當 sitewidth 不等於 spacing 時採保守
  whole-site envelope，可能犧牲部分可用空間。
- 讀寫失敗、未知 pin、缺少 cell、無空間等情況以非零退出碼回報。

## 測試

完整整合測試（環境需有 CMake；例如先啟用現有 `eda` 環境）：

```bash
python3 ../../Grader/tools/local_grade.py . CodexGapSA \
  ../../Testcases/testcases ../../Testcases/holdout \
  --work results/grade --dp-args '--time 5 --effort 100 --trace trace.csv -v'
```

該工具打包、解壓後重新編譯，使用外部 `check_pl.py` 核對兩階段合法性、四項
stdout 數值、七個輸出檔案，以及 SA 不劣於 legal 的條件。
詳細結果及參數見 `TEST_RESULTS.md`、`results/grade/record.csv` 和 `results/grade/logs/`。

幾何回歸測試（獨立窮舉空隙最佳位置，驗證不等寬交換與 rollback）：

```bash
mkdir -p build
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  tests/geometry.cpp src/db.cpp -o build/geometry_test
ASAN_OPTIONS=detect_leaks=0 ./build/geometry_test
```

`--trace` 的 CSV 可直接交給既有 `../../Grader/tools/plot_trace.py` 畫圖。

## 閱讀範圍與獨立性

已閱讀題目 PDF、兩份 Bookshelf 說明、範例、測資說明與產生工具、程式骨架、
檢查／評分／繪圖工具，以及報告工作流程；解析三筆課程及八筆額外測資。
`M11502102/src` 與原始骨架相同，經逐檔比對確認。

未開啟或執行 `AI_Solutions/ClaudeOpus55` 的程式。共用教學／README 含有該解的
方法介紹與數字，閱讀導覽時已看到；沒有以那些數字作為本程式成果，也沒有搬用
其原始碼。為遵守排除要求，未閱讀其 `AI_Workspace/reference_report` 報告、表格、
圖片和輸出，也未採用 `AI_Workspace/step1_example` 與 `step2_grader` 的參考解輸出。
本專案所有程式碼都是本次新寫；不改動使用者的 `M11502102`。
