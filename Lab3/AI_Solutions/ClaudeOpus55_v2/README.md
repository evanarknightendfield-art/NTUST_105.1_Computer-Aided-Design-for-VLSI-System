# dp v2 — 改良版 Standard Cell Detailed Placer

`ClaudeOpus55`（v1）的改良版。仔細檢查 v1 的弱點、查了近年的 legalization / detailed placement 論文後，
Stage 1 加了 4 個方法、Stage 2 加了 5 個改動。用法、輸出格式和 v1 完全一樣。

## 結果：v1 → v2（11 筆測資，都通過 `Grader/tools/local_grade.py`）

| bench | 總位移 v1 → v2 | Δ | 最大位移 v1 → v2 | Δ | HPWL (SA) v1 → v2 | Δ | 時間 v1 / v2 (s) |
|---|---:|---:|---:|---:|---:|---:|---:|
| toy | 46,619.2 → 44,375.6 | −4.8% | 1,987.6 → 2,007.9 | +1.0% | 48,054 → 47,514 | −1.1% | 0.0 / 1.0 |
| ibm01 | 27,235,501 → 25,452,360 | −6.5% | 10,578.3 → 10,621.0 | +0.4% | 60,045,714 → **53,074,338** | **−11.6%** | 12.4 / 23.7 |
| ibm05 | 1,431,842 → 1,418,382 | −0.9% | 217.6 → 202.7 | −6.8% | 10,307,273 → **9,861,774** | **−4.3%** | 42.1 / 66.6 |
| h01 | 2,575.6 → 2,528.1 | −1.8% | 33.1 → 31.7 | −4.2% | 14,886 → 13,947.5 | −6.3% | 0.2 / 1.7 |
| h02 | 534,211 → 526,484 | −1.4% | 2,727.9 → 2,607.3 | −4.4% | 2,424,594 → 2,322,805 | −4.2% | 0.7 / 1.9 |
| h03 | 8,477.2 → 7,844.4 | −7.5% | 21.6 → 21.2 | −2.1% | 13,182.6 → 12,470.8 | −5.4% | 1.6 / 3.3 |
| h04 | 203,183 → 207,438 | +2.1% | 296.7 → 241.6 | −18.6% | 250,903 → 234,678 | −6.5% | 3.9 / 9.1 |
| h05 | 1,658,809 → 1,646,498 | −0.7% | 1,398.4 → 1,495.0 | +6.9% | 18,320,012 → 18,313,099 | −0.0% | 5.6 / 5.7 |
| h06 | 1,075,555 → 1,112,306 | +3.4% | 504.2 → 435.2 | −13.7% | 805,760 → 714,357 | −11.3% | 12.2 / 25.1 |
| h07 | 124,696 → 125,426 | +0.6% | 53.0 → 48.0 | −9.3% | 174,293 → 170,261 | −2.3% | 16.9 / 30.1 |
| h08 | 157,049,936 → 150,496,293 | −4.2% | 29,576.6 → 28,600.5 | −3.3% | 111,886,698 → 106,348,486 | −4.9% | 32.1 / 66.0 |
| **幾何平均（v2 ÷ v1）** | | **0.980** | | **0.948** | | **0.947** | |

- SA 的 HPWL **11 筆全部變好**（ibm01 −11.6%，已經比輸入那個有重疊的 global placement 的 54.5M 還低）。
- 位移：總位移 8 筆變好、最大位移 8 筆變好，沒有一筆是兩個同時變差。
- 代價：時間大約是 v1 的 1.5 ~ 2 倍（Stage 1 會跑兩次，見下面「自動選擇是否 spreading」）。最久的 ibm05、h08 約 66 秒。
- 每筆測資固定 seed 1；SA 換 seed 的變異 v1 量過約 0.1 ~ 0.5%，小於大部分的差距。toy 例外：v1 在 toy 換 seed 會從 48k 跳到 98k，v2 穩定在 46.0k ~ 47.5k（見 Stage 2 第 5 點）。

重跑這張表：

```bash
cd Lab3
python3 Grader/tools/local_grade.py AI_Solutions/ClaudeOpus55    V1 Testcases/testcases Testcases/holdout --work /tmp/lg_v1
python3 Grader/tools/local_grade.py AI_Solutions/ClaudeOpus55_v2 V2 Testcases/testcases Testcases/holdout --work /tmp/lg_v2
```

## v1 的弱點（檢查出來的）

| 弱點 | 證據 |
|---|---|
| Stage 1 最大位移卡在「一道牆」 | ibm01 微調到最後，幾百個 cell 的位移都在最大值附近；v1 的 move 一次只動 1 ~ 2 個 cell，熱點裡的 cell 要「一起往外推」才能再降 |
| Abacus 的位置是**平方**最小，不是總位移（L1）最小 | row 和順序固定時，L1 的最佳解可以精確算出來，v1 沒算 |
| SA **一半的嘗試是白費的** | `-v` 統計：ibm01 48.6%、ibm05 40.1% 的 proposal 因為「放不下」直接失敗（目標 cell 跟自己寬度不合、空位太小） |
| SA 會搬沒有接 net 的 cell | 它們放哪裡 HPWL 都一樣，搬它們只是浪費次數（toy 有 3 個這種 cell，結果會飛到晶片另一頭） |
| 小設計的 SA 次數太少 | 次數 = 2000 × cell 數，toy 只有 6.2 萬次，換 seed 結果從 48k 到 98k 都有 |

## Stage 1 的改動（`src/legalizer.cpp`）

### 1. 每個 row 精確重排（row、順序固定）—— `Refiner::optimizeSegment`

row 和 cell 的左右順序固定時，最小化 Σ φ(d_i) 是一個「鏈狀限制的凸問題」：
`x_{i+1} ≥ x_i + w_i`，每個 cost 對 x_i 是凸的。用 **pool-adjacent-violators**（跟 Abacus 一樣的 cluster 合併，
但每個 cluster 的最佳位置用「離散導數 ≥ 0 的第一個 x」二分搜尋）可以得到**精確最佳解**。
這是 RippleLG 的 "fixed row & fixed order" 步驟，論文用 dual min-cost flow 解，這裡用 clumping 解（單 row 高度時等價、比較好寫）。

- 第一次用 φ(d) = d（純總位移），把 Abacus 的平方最佳換成 L1 最佳。
- 之後每輪用 φ(d) = d（d ≤ δ₀）、d·(d/δ₀)⁴（d > δ₀），δ₀ = 目前最大位移，而且每個 cell 不能超過目前最大位移 → 降總位移、不讓最大位移變大。

### 2. 同寬 cell 的最小成本匹配 —— `Refiner::matchWindows` / `matchBox`

[RippleLG, DAC'18]：**寬度一樣的 cell 互換位置一定不會重疊**。在位移最大的 cell 周圍開一個視窗
（包住它的 GP 位置和目前位置，再往外擴 1.5 倍位移），視窗裡每一種寬度做一次指派問題
（**Hungarian 演算法**，O(n³)），cost = φ(位移)：δ₀ 以下是線性（顧總位移）、以上急遽變大（壓最大位移），
而且任何 cell 都不能超過目前的最大位移（自己原本的位置一定可以）。

一次指派可以產生任意長的「搬家鏈」：遠的 cell 往內搬一格、原本在那裡的 cell 往外搬一點、……
這正是 v1 的 1 ~ 2 個 cell 的 move 做不到的。

- 只處理「有 cell 超過門檻」的寬度（其他寬度降不了最大值，省時間）。
- 工作量上限用 Σ g³（Hungarian 的步數）而不是秒數，**所以結果在每台機器都一樣**。
- 跟 v1 的 insert / swap / evict 交替：匹配先解決大部分超過門檻的 cell，剩下的（例如寬度很少見的）交給 v1 的 move。

### 3. 全晶片的匹配掃描（降總位移）—— `Refiner::matchSweep`

6 個 row × 120 個 site 的視窗鋪滿整個晶片（掃兩次，第二次錯開半個視窗），每種寬度做一次指派，
cost = 位移、不能超過目前最大值。這是把 detailed placement 的 independent set matching 拿來降位移。

### 4. 流量式 spreading + 自動選擇 —— `class Spreader`

[Brenner, DATE'12；Karimpour Darav et al., ISPD'17；講義 L07 "network flow for legalization among regions"]

把晶片切成 bin（2 rows × 16 個平均 cell 寬），依 GP 位置把 cell 放進 bin。bin 裡的 cell 總寬超過空 site 數時，
用 **BFS 找最近有空間的 bin**，沿著路徑**每個 bin 把一個 cell 交給下一個 bin**，所以每個 cell 只移動大約一個 bin，
而不是讓過載的 cell 一路被推到很遠。每個 cell 離 GP 不能超過上限 B，找不到路徑才把 B 放大 1.5 倍（最多 4 次，
剩下的小過載交給 Abacus）。結果是每個 cell 的「目標位置」，Abacus 改成往目標合法化（位移還是對原本的 GP 算）。

實驗結果是**有好有壞**（單獨開 spreading 時，ibm05 總位移 −4%，但最大位移有時變差、合成測資有時更差），
所以預設是 `--spread auto`：**有 spreading、沒 spreading 各跑一次完整的 Stage 1，留比較好的**
（總位移和最大位移的相對值各佔一半）。Stage 1 是確定性的，這樣做保證不會比不做差（就這個標準而言）。
超過 6 萬個 cell 的設計只跑一次（避免時間加倍）。

## Stage 2 的改動（`src/annealer.cpp`）

| # | 改動 | 為什麼 / 效果 |
|---|---|---|
| 1 | **目標失敗時改試附近的 cell 和空位**（`swapWith` / `moveInto` / `gapAfter`） | 目標放不下時，依序試目標左右最近的 3 個 cell（交換）和它們後面的空位（搬過去），只接受離目標幾個 cell 寬以內的。ibm01 失敗的 proposal 從 48.6% 降到 10%；同樣次數下 ibm01 −13%、ibm05 −4% |
| 2 | **依密度自動開關第 1 點**（`near_`） | 第 1 點在使用率 ≥ 0.74 的 7 筆全部變好（−1% ~ −13%），在 ≤ 0.70 的 4 筆全部變差（+2 ~ 3%）：稀疏的設計目標通常放得下，補救的 move 品質比較差。所以使用率 ≥ 0.72 才開 |
| 3 | **range window 上限 = 晶片的 10%**（`maxWin`） | 第 1 點讓高溫時接受率變高，VPR 的規則會把 window 放大到半個晶片，隨機 move 變成長距離亂跳。設上限後 ibm01、ibm05、h06、h08 都再好一點 |
| 4 | **local reordering**（`proposeReorder`）[FastPlace-DP] | 同一 row 相鄰 3 個 cell 在原本的範圍內換順序（空隙位置不變），10% 的 move 是這種。效果小（0 ~ −1%，接近 seed 的雜訊），但接受率和「變好」的比例都比 swap 高，留著 |
| 5 | **只搬有接 net 的 cell**（`active_`），**至少做 500 萬次嘗試**（`minMoves`） | 沒接 net 的 cell 不影響 HPWL；小設計（toy 31 個 cell）原本只做 6 萬次，結果不穩定，現在 toy 換 6 個 seed 都在 46.0k ~ 47.5k |

`-v` 會印出每種 move 的比例、接受率、變好的比例，報告可以直接用：

```
$ ./build/dp ibm01 out -v          # 預設參數
[sa] proposals failed 10.0%
[sa] slide      1.8% of attempts, accepted  83.3%, improving  26.7%
[sa] move       7.5% of attempts, accepted  50.5%, improving  26.0%
[sa] swap      70.8% of attempts, accepted  33.9%, improving  14.7%
[sa] reorder   10.0% of attempts, accepted  42.9%, improving  16.9%
```

（slide = 在自己的空位裡滑動，move = 搬到別的空位。v1 同一筆是 48.6% 失敗。）

## 試過但沒有採用的

| 試過的 | 結果 |
|---|---|
| 更大的匹配視窗（2 倍位移、每種寬度 500 個 cell） | ibm01 最大位移曾降到 9,721（−8%），但 Stage 1 要 30 ~ 50 秒，而且換個設定結果就差很多（雜訊大），不划算 |
| 用時間（秒數）限制匹配 | 結果會隨機器快慢改變，改成工作量上限 |
| 只開 spreading（不比較） | 見上面第 4 點，有好有壞 → 改成兩個都跑、留好的 |
| 第 1 點全部開（不看密度） | 稀疏的 h01、h02、h05、toy 變差 2 ~ 3% → 改成依密度 |
| local reordering 機率 0.2、0.3 | ibm01、ibm05 都比 0.1 稍差（1 ~ 2%），用 0.1 |
| v1 時代：diffusion 預先散開 | 原型發散，沒有做下去 |

## 編譯與執行

跟 v1 一樣：

```bash
cmake -S . -B build && cmake --build build
./build/dp INPUT_FILE OUTPUT_FILE [options]
```

新選項（`./build/dp` 會列出全部）：

| 選項 | 說明 |
|---|---|
| `--spread auto\|on\|off` | 流量式 spreading（預設 auto：兩種都跑、留好的） |
| `--bin R C`、`--spread-rounds N` | spreading 的 bin 大小（2 rows × 16 個平均 cell 寬）、上限放大次數（4） |
| `--no-matching`、`--no-rowopt` | 關掉同寬匹配 / 每 row 精確重排 |
| `--reorder P` | local reordering 的機率（0.1） |
| `--near K` | 目標失敗時每邊試幾個（預設依密度：0 或 3） |
| `--maxwin F` | range window 上限（晶片的 0.1） |

時間：Stage 1 在 ibm05 約 12 秒（兩次 pipeline），SA 上限 60 秒（`--time`），ibm05 / h08 全部約 66 秒。

## 參考資料

- P. Spindler, U. Schlichtmann, F. M. Johannes, "Abacus: Fast Legalization of Standard Cell Circuits with Minimal Movement," ISPD 2008.（v1 就有）
- H. Li, W.-K. Chow, G. Chen, E. F. Y. Young, B. Yu, "Routability-Driven and Fence-Aware Legalization for Mixed-Cell-Height Circuits" (RippleLG), DAC 2018 — [paper](https://www.cse.cuhk.edu.hk/~byu/papers/C72-DAC2018-RippleLG.pdf)、[TCAD 2022 版](https://www.cse.cuhk.edu.hk/~byu/papers/J59-TCAD2022-RippleLG.pdf)：同寬 cell 的二分圖匹配（cost φ(δ) = δ / δ⁵/δ₀⁴）、fixed row & fixed order 最佳化
- N. Karimpour Darav, I. S. Bustany, A. Kennings, L. Behjat, "A Fast, Robust Network Flow-based Standard-Cell Legalization Method for Minimizing Maximum Movement," ISPD 2017 — [slides](https://ispd.cc/slides/2017/s7_3.pdf)：bin、BFS 找路徑、最大位移上限、逐步放寬
- U. Brenner, "VLSI Legalization with Minimum Perturbation by Iterative Augmentation," DATE 2012 — [paper](https://past.date-conference.com/date15/files/proceedings/2012/pdffiles/11.7_2.pdf)
- M. Pan, N. Viswanathan, C. Chu, "An Efficient and Effective Detailed Placement Algorithm" (FastPlace-DP), ICCAD 2005：global swap、vertical swap、local reordering
- Y. Lin et al., "ABCDPlace: Accelerated Batch-based Concurrent Detailed Placement," TCAD 2020 — [page](https://research.nvidia.com/publication/2020-02_abcdplace-accelerated-batch-based-concurrent-detailed-placement-multi-threaded)：independent set matching、global swap、local reordering
- V. Betz, J. Rose, "VPR: A New Packing, Placement and Routing Tool for FPGA Research," FPL 1997：range limiter（v1 就有）
- 課程講義 L06（TimberWolf：M1 / M2 move、range limiter）、L07（Greedy legalization、network flow for legalization among regions）
