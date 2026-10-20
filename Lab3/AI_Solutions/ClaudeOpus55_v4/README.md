# dp v4 — Standard Cell Detailed Placer（第四版）

以 `CodexWindowDP`（= v3 + Codex 的兩個 row 子集合 DP、瓶頸匹配、HPWL 收尾）為底。
重讀了所有版本的程式、CodexWindowDP 的 `READING_NOTES.md`、`Grading Criteria.pdf` 和講義 L06 / L07，
**先量出每個部分離最佳解還有多遠、在多大的設計上會跑不完**，再決定改哪裡。用法、輸出跟前幾版一樣。

七個版本的完整比較、下界分析、還能改良的地方：**`../COMPARISON.md`**。

## 結果

11 筆測資全部通過 `local_grade.py`（打包 → 編譯 → 執行 → 獨立檢查器）。幾何平均，越小越好：

| | v3 | WindowDP | **v4** | v4 vs WindowDP |
|---|---:|---:|---:|---:|
| 總位移 ÷ v1 | 0.971 | 0.935 | **0.875** | **−6.4%**，11 筆全部最好 |
| 最大位移 ÷ v1 | 0.948 | 0.948 | **0.947** | 8 筆最好或並列，沒有一筆比 v3 差 |
| HPWL（SA 後）÷ v1 | 0.927 | 0.927 | **0.920** | −0.8%；8000 cell 以上 0 ~ −2.5%（平行 SA），小設計在雜訊內 |
| 11 筆合計時間 | 405 秒 | 418 秒 | 450 秒 | |

Stage 1 離**證明過的最佳解**（同樣的最大位移）：toy **0.87%**、h01 **2.0%**（WindowDP 2.0% / 4.2%，v3 3.5% / 10.5%）。

大設計（`Testcases/large`）：

| | v3 | WindowDP | **v4** |
|---|---:|---:|---:|
| 20 萬 cell：時間 | 56 分 | 56.5 分 | **2 分 58 秒** |
| 20 萬 cell：總位移 / 最大位移 / HPWL | 2.570e9 / 50,491 / 2.008e9 | 2.513e9 / 50,491 / 1.893e9 | **2.453e9 / 50,342 / 1.770e9** |
| 100 萬 cell | 一小時跑不完 | — | **3 分 24 秒**，合法 |

## 為什麼改這些（先量）

| 量到的事 | 數據 | 結論 |
|---|---|---|
| CodexWindowDP 的 DP 只掃 2 輪就停 | 掃到收斂：ibm01 再 −3.1%、h04 再 −4.8% | DP 還沒收斂 → 跑到收斂，但要便宜 |
| 跟 h01 的**證明過的最佳解**比 | 差的 87 裡有 70 來自 22 個 cell 換 row，多半是好幾個 row 連鎖地各挪一個 row | 兩個 row 的視窗不夠 → 加三個 row 的 DP |
| `Grading Criteria.pdf`（Project 2）：隱藏測資、跑不完 = 0 分、「要知道自己演算法的複雜度」 | 20 萬 cell 的測資：v1 38 分、v3 56 分、CodexWindowDP 56.5 分，**Stage 1 就花 37 ~ 55 分鐘** | 每個階段都要有時間預算，慢的步驟要換掉 |
| SA 跑更多次還能降多少 | ibm01：4 倍次數 −3.3%、8 倍次數（8 執行緒）−5.0% | SA 沒收斂 → 用多執行緒在同樣時間內做更多次 |
| 舊的下界工具 | CodexWindowDP 發現 `exact_bound.py` 有三個問題（已修）；修好後 h01 的最大位移精確最小值是 **26.5**（我們 31.7） | 之前說「最大位移幾乎最佳」是錯的，見 `../COMPARISON.md` |

## v4 的改動

### Stage 1（`src/window.cpp`、`src/legalizer.cpp`）

| # | 改動 | 效果 |
|---|---|---|
| 1 | **視窗 DP 掃到收斂**，並記住「哪些視窗解過、之後沒變」（`WindowMemo`：每個 cell 一個「最後一次被移動」的戳記；視窗裡的 cell 和夾住視窗的兩個鄰居都沒動過，就不用再解） | ibm01：20 輪全掃要 17 秒，記住之後 4 秒、結果一樣；後面幾輪幾乎不用花時間 |
| 2 | **三個 row 的 DP**（`solveMulti`）：每個 row 先做 CodexWindowDP 的子集合 DP，再把三個 row 的集合分配合起來：G(C) = min over S ⊆ C of F₁(S) + F₂(C \ S)，答案 = min over S of F₀(S) + G(U \ S)，共 3^k 組。視窗內仍是**精確最佳** | 讓「A 往下一個 row、B 再往下一個 row」這種連鎖 move 也找得到；h01 −0.6%、toy、h03 也有 |
| 3 | 兩個 row（每 row 4 顆）和三個 row（每 row 3 顆）輪流掃，到兩種都找不到改善為止；≤ 1500 個 cell 的小設計用大視窗（5 顆 / 4 顆） | 小設計多 −1 ~ −3%（h02 −2.7%）；h03 以上用大視窗太慢（35 秒）所以不用 |
| 4 | **最大位移的視窗**（`reduceMaxWindows`）：最差的 cell 和它的鄰居組成視窗，二分找視窗內能做到的最小上限 | toy 的最大位移在其中一個流程從 2,072 降到 2,036；大設計多半已經卡在熱點的邊界上，沒有作用 |
| 5 | 兩個流程（有 / 沒有 spreading）**先比較再做收尾**，收尾（DP + 舊的 local move）只做一次 | Stage 1 時間少一半 |
| 6 | **視窗 DP 平行化**：把 row 切成幾個 band，視窗不跨 band，所以不同 band 可以同時解；每一輪 band 的邊界錯開半個 band | 8 執行緒時 DP 快 4.8 倍；同樣執行緒數結果固定 |
| 7 | **時間預算**：Stage 1 的精修共 90 秒（兩個流程各 30%，其餘給收尾），每個 pass 都會檢查；舊的 total pass 最多用剩下時間的一半 | 20 萬 cell：Stage 1 從 37 ~ 55 分鐘變成 100 秒，而且總位移、最大位移都比舊版好 |
| 8 | `reduceMax` 找最差 cell 改用 heap（原本每次 move 都掃全部 cell：20 萬 × 2.3 萬次）；tie-break 跟舊的一樣挑編號最小的 | 一樣的結果、近線性 |
| 9 | **Abacus 近線性**：位置最後才寫（原本每放一顆就重寫整個 cluster）；「總位移」成本的精確計算有工作量上限（5×10⁹ 步，計數不是計時），用完之後大 cluster 改用上界 | 100 萬 cell 的 Abacus 900 秒 → 22 秒；20 萬 cell 以下都還是精確的（課程 / holdout 測資的結果不變） |

### Stage 2（`src/annealer.cpp`）

| # | 改動 | 效果 |
|---|---|---|
| 10 | **多執行緒 SA**（`annealParallel`）：row 切成 band，每個執行緒在自己的 band 裡做 move（只動自己 band 的 cell、只放進自己 band 的 row），其他 band 看成固定的；大約每個 cell 試 1 次就合併一次，band 的邊界每次錯開半個 band | ibm01、同樣 60 秒：1 執行緒 51.26M → 4 執行緒 50.00M → 8 執行緒 49.35M（開發中量的） |
| 11 | 合併的做法讓結果**不受執行緒排程影響**：每個執行緒有自己的一份座標，只讀「這一輪開始時」別人的位置；合併時只交換「這一輪動過的 cell」 | 同 seed、同執行緒數 → 同結果；ThreadSanitizer 沒有警告 |
| 12 | 執行緒數 = min(硬體核心數, 8)；**8000 cell 以下用 1 個**（單執行緒本來就做得完整個降溫，平行只會損失品質：h05 用 6 個執行緒 +1.4%）；20 萬 cell 用 4 個、100 萬用 2 個（每個執行緒一份座標和 net box，太多放不進 cache） | 同一份 v4 跟 `--threads 1` 比：ibm01 −0.7%、ibm05 −0.8%、h06 −2.2%、h07 −0.3%、h08 −1.4% |
| 13 | 依時間調整每一步的次數時，用**上一步**的速度估（高溫的步驟比較慢，用平均會提早 10 ~ 30% 結束） | 平行版能用滿 60 秒 |

### 穩健（`src/main.cpp`）

| # | 改動 |
|---|---|
| 14 | 每個「可有可無」的步驟（瓶頸匹配、DP、SA、收尾）都包起來：丟例外或破壞不變量 → 退回那一步之前的結果、印 warning、繼續。CodexWindowDP 在不變量失敗時會 `throw`，程式結束、**沒有輸出檔** |
| 15 | 合法化本身失敗時，改用不精修的 Abacus 再試一次 |
| 16 | SA 之後的收尾也有時間上限（30 秒）；HPWL 視窗、排序 DP 都會檢查時間 |

## 試過但沒有用的

| 試過的 | 結果 |
|---|---|
| 平行 SA，每個溫度步驟才合併一次 | 比單執行緒還差 8%：每個 band 對「別的 band 很久以前的位置」最佳化太多次 → 改成約每個 cell 一次就合併 |
| 平行 SA 第一版 | 只有第 0 個執行緒初始化了 move 的範圍，其他 band 的範圍是 0 → 修好之後才是上面的結果（這個 bug 一開始被誤以為是「band 切太細」） |
| 平行 SA，band 外的隨機目標直接算失敗 | 失敗的 proposal 從 10% 變 19% → 改成只在「範圍 ∩ band」裡抽 |
| 每個視窗的空白只留到 cell 們的 global x 範圍 | ibm05 一樣慢（視窗本來就擠），保留但沒有效果 |
| 3 個 row × 4 顆的大視窗用在 2000 個 cell 的 h03 | −1.1%，但 Stage 1 從 4 秒變 35 秒 → 只用在 ≤ 1500 個 cell |
| 瞄準 optimal region 的 move 不准超出 band | 20 萬 cell 上 HPWL 差 21% → 夾到 band 邊上 |
| 視窗 DP 的 band 只看 cell 數切 | toy 的 band 只有 1 ~ 3 個 row，大部分視窗被跳過 → 每個 band 至少 6 個 row |
| Abacus 一律用上界 | h04 / h06 的最大位移差 1 ~ 3%、20 萬 cell 差 4% → 精確計算的工作量用完才用上界 |
| max pass 改 heap 時 tie-break 跟舊的不同 | 最大位移走了不同的路，h04 / h06 差 3% → 改成同樣挑編號最小的 |

## 編譯與執行

```bash
cmake -S . -B build && cmake --build build
./build/dp INPUT_FILE OUTPUT_FILE [options]
# 或
make && ./dp INPUT_FILE OUTPUT_FILE
```

新選項（`./build/dp` 會列出全部）：

| 選項 | 說明 |
|---|---|
| `--threads N` | SA 和視窗 DP 的執行緒數（0 = 自動，最多 8；1 = 跟 v3 一樣的單執行緒 SA） |
| `--stage1-time S` | Stage 1 精修的時間預算，從程式開始算（90） |
| `--time S` | SA 的時間上限（60） |
| `--polish-time S` | SA 之後收尾的時間上限（30） |
| `--sync M` | 平行 SA：每個 cell 大約試 M 次就合併（1） |
| `--window-cells N` | 兩個 row 的視窗每 row 幾顆（4；≤ 1500 個 cell 時 5） |
| `--window-passes 0` | 關掉視窗 DP |

時間：ibm01 約 61 秒、ibm05 約 79 秒、h08 約 96 秒；20 萬 cell 約 3 分鐘、100 萬 cell 約 3.5 分鐘（Stage 1 約 100 秒 + SA 60 秒 + 收尾）。

## 測試

```bash
# 1500 組 1 / 2 / 3 個 row 的 DP 跟窮舉比對 + 三個 row 的連鎖例子
g++ -std=c++17 -O2 tests/multirow_test.cpp src/window.cpp src/placedb.cpp -o /tmp/multirow_test && /tmp/multirow_test
# CodexWindowDP 的測試（兩個 row 的 DP、排序 DP、匹配、隨機 placement）
g++ -std=c++17 -O2 -pthread tests/window_test.cpp src/window.cpp src/placedb.cpp -o /tmp/window_test && /tmp/window_test
g++ -std=c++17 -O2 -pthread tests/optimizer_test.cpp src/optimizer.cpp src/window.cpp src/placedb.cpp -o /tmp/optimizer_test && /tmp/optimizer_test
```

三個測試都用 `-fsanitize=address,undefined` 跑過；整支程式用 `-fsanitize=thread` 在 h04、ibm01、h08（最終版，8 執行緒）跑過，沒有警告。

## 參考資料

- 課程講義 L06（SA、range limiter）、L07（linear placement、window interleaving、多 row 視窗）
- `CodexWindowDP`：兩個 row 的子集合 DP、同 row 排序 DP、瓶頸匹配、`READING_NOTES.md`（下界工具的 bug）
- J. S. Rose, W. M. Snelgrove, Z. G. Vranesic, "Parallel standard cell placement algorithms with quality equivalent to simulated annealing," TCAD 1988；
  S. A. Kravitz, R. A. Rutenbar, "Placement by simulated annealing on a multiprocessor," TCAD 1987 — 依空間切塊的平行 SA
- Y. Lin et al., "ABCDPlace," TCAD 2020 — 多執行緒 / GPU 的批次 detailed placement
- 其他（Abacus、RippleLG、ISPD'17 flow、Brenner、FastPlace-DP、NTUplace3）見 `../COMPARISON.md` 的參考資料；
  range limiter：V. Betz, J. Rose, "VPR: A New Packing, Placement and Routing Tool for FPGA Research," FPL 1997
  （v2、v3 的 README 原本也列了這些，兩個版本已從資料夾移除，在 git commit `e1fd81f` 裡）
