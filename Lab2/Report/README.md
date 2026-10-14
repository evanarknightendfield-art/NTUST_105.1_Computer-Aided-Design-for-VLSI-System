# Report — 產生測資、跑分、表格與圖（報告用）

這個資料夾**不要放進交作業的 zip**。它會編譯 4 個版本的程式、在同一組測資上跑，再整理成報告需要的表格和圖。

## 一行跑完全部

```bash
conda activate eda
./run_everything.sh
```

大約 5 分鐘。做的事情依序是：

1. `scripts/build_all.py`：編譯 5 個版本到 `build/v1..v5/cbi`
2. `scripts/gen_tests.py`：產生 12 筆新測資到 `../Testcases/holdout/`（課程測資在 `../Testcases/testcases/`）
3. `scripts/run_all.py`：每個版本跑每筆測資，輸出與 log 存到 `results/<set>/<版本>/`，並用課程 verifier 檢查 VALID
4. `scripts/make_tables.py`：表格到 `tables/<set>/`（`.md` 和 `.csv` 各一份）
5. `scripts/make_figures.py`：圖到 `figures/<set>/`
6. `scripts/make_arrival.py`：A-004 的 v1 / v5 sink 到達時間比較圖（報告圖 1）

**只想重跑某個版本（例如改了 v5）：** 不要跑 `run_everything.sh`，否則 v1～v4 的分數也會因為隨機性而改變，報告裡的數字要全部重填。改用：

```bash
cd scripts
python build_all.py
python run_all.py --set testcases --versions v5
python run_all.py --set holdout --versions v5
python make_tables.py --set testcases && python make_tables.py --set holdout
python make_figures.py --set testcases --trees 001,004 && python make_figures.py --set holdout --trees ""
python make_arrival.py --set testcases --case 004
```

`run_all.py` 只會覆寫被重跑版本的結果，`summary.csv` 裡其他版本的資料會保留。

## 版本

| 代號 | 名稱 | 資料夾 | 內容 |
|---|---|---|---|
| v1 | Construction | `MyCode/v1_legal_initial_construction` | 第 3 課：由下往上分群建合法樹 |
| v2 | Hill climbing | `MyCode/v2_hill_climbing` | 第 4 課：+ hill climbing（Shift / Reparent / Pad） |
| v3 | Simulated annealing | `MyCode/v3_simulated_annealing` | 第 5 課：+ 模擬退火、σ 能量、小測資重啟 |
| v4 | Incremental SA | `MyCode/v4_incremental` | 第 6 課：+ 增量計算（lo/hi）、對準關鍵路徑 |
| v5 | Balanced + structural moves | `MyCode/v5_balanced_swap` | brief 改進：零 skew 合併點（L04 Tsay / DME / GMA）+ 交換 parent（L06 TimberWolf、L10 KL）+ Merge / Split + 改接到較遠的鄰近 buffer，時間上限 `min(45, 10 + N/100)` 秒（= 交出去的版本） |

要加新版本：在 `scripts/common.py` 的 `VERSIONS` 加一行。分數圖最多畫 3 個版本（`PLOT_VERSIONS`），樹狀圖畫 4 個（`TREE_VERSIONS`）。

## 單獨執行某一步

```bash
cd scripts
python gen_tests.py --name holdout2 --count 20 --seed 7    # 另一組新測資
python run_all.py --set holdout2                           # 跑全部版本
python run_all.py --set testcases --versions v4            # 只跑某個版本
python make_tables.py --set holdout2
python make_figures.py --set testcases --trees 001,004,006 # 指定要畫樹的測資
```

## 產出

### 表格 `tables/<set>/`

| 檔案 | 內容 | 報告用途 |
|---|---|---|
| `stages.md` | 最終版本（v5）的 Stage 1（建構）→ Stage 2（最佳化）：buffers / cost / skew / Score / 改善 % / 秒數 | **題目要求的「各階段比較」** |
| `versions.md` | 5 個版本的最終 Score，最後一列是相對 v1 的幾何平均 | 每個方法的貢獻 |
| `bounds.md` | cost 下限、skew 下限（奇偶性）、Score 下限，和最終結果的比值 | 討論離最佳解還有多遠 |

**下限怎麼算**（報告可引用）：
- **cost 下限：** SRC 與所有 buffer 的 children 總數 = N + B（每個 sink、buffer 剛好當一次 child）。
  所以 Σ(F_b − 1) ≥ N − F_SRC，cost ≥ (N − F_SRC) × min_t C_t / (F_t − 1)。
- **skew 下限：** 邊長的奇偶 = 兩端 (x+y) 的奇偶差，所以 sink 的 arrival 奇偶 = (x_s+y_s) − (x_SRC+y_SRC) 的奇偶，
  任何樹都改不了。sinks 的 (x+y) 奇偶不一致時，skew ≥ 1。

### 圖 `figures/<set>/`

| 檔案 | 內容 |
|---|---|
| `fig_trees_XXX.png` | 同一筆測資 v1 / v2 / v4 / v5 的樹（2×2），標題有 cost / skew / Score。sink 顏色 = arrival time |
| `fig_score_by_version.png` | 每筆測資 v2 / v4 / v5 的 Score ÷ 初始建構 Score（越左越好） |
| `fig_cost_skew_split.png` | 每筆測資初始 / 最終的 Score 拆成 buffer cost 與 w_skew × skew 兩段 |
| `fig_arrival_004.png` | A-004 的 v1 / v5 sink 到達時間分布（`make_arrival.py`） |

### 配色

顏色定義在 `scripts/common.py`，用的是 dataviz 預設配色的前三色（固定順序）：

| 用途 | 顏色 |
|---|---|
| `COLORS`：v2 / v4 / v5（分數圖，點形狀分別是 ● ■ ◆） | 藍 `#2a78d6`、橘 `#eb6834`、青綠 `#1baf7a` |
| `PART_COLORS`：buffer cost / w_skew × skew（堆疊長條圖） | 藍 `#2a78d6`、橘 `#eb6834` |
| v1 不畫成一種顏色，它是分數圖上的基準線 1.0 | 灰色虛線 |

分數圖是點狀圖，每一對顏色都會同時出現，所以要用 `--pairs all` 驗證。dataviz 文件指出只有前三色能通過全部配對。
原本的「橘、青綠、黃」中，黃和橘的辨識度 ΔE 只有 13.7，低於標準 15，**不合格**，所以改成現在這組。
青綠和背景的對比 2.74:1 低於 3:1，補救方式是不同的點形狀加上 `versions.md` 表格。

改顏色之後用 `eda` 環境裡的 node 重新驗證：

```bash
conda activate eda
node /tmp/claude-1000/bundled-skills/2.1.295/b9a997ced0aab1736bc5c8b24f865ec5/dataviz/scripts/validate_palette.js "#2a78d6,#eb6834,#1baf7a" --mode light --surface "#fcfcfb" --pairs all
```

（驗證程式在 Claude 的暫存目錄裡，換了機器或重開機後可能不在，需要時再請 Claude 跑。）

## 報告架構建議（≤ 3 頁）

| 節 | 篇幅 | 內容 | 放什麼 |
|---|---|---|---|
| 1. Problem & Notation | 0.3 頁 | Score 定義、fanout / length 限制、verifier 比 PDF 嚴格的地方（buffer 不能在邊界、.level 深度） | — |
| 2. Initial Legal Construction | 0.6 頁 | Morton 排序 → 貪心分群 → 零 skew 合併點放 buffer（讓 children 到達時間對齊，對應 Tsay / GMA）→ 螺旋找空位 → relay 往 SRC；為什麼一定合法、一定結束 | `fig_trees_004.png` 左上 |
| 3. Optimization | 0.9 頁 | 5 種 move（Pad / Shift / Reparent + 刪空 buffer / 零 skew 重新放置 / 交換 parent）、每步只檢查被改到的節點、模擬退火（溫度、σ 能量）、增量 lo/hi（O(N) → O(深度×fanout)）、關鍵路徑 | `fig_trees_004.png` 其他三格 |
| 4. Experiments | 0.9 頁 | 各階段表格、版本比較、新測資驗證、執行時間 | `stages.md`、`versions.md`、`fig_score_by_version.png` |
| 5. Observations | 0.3 頁 | 奇偶性下限、cost 下限與差距、最佳化主要是降 skew（cost 幾乎不變）、w_skew 大時插繞路 buffer 很划算 | `fig_cost_skew_split.png`、`bounds.md` |

## 目前的重點數據

v1～v4 是 `./run_everything.sh` 跑一次的結果；v5 換成新版程式後，只重跑了 v5（v1～v4 的結果沒有動）。

- **全部 VALID：** testcases 15 筆、holdout 12 筆，5 個版本都是。
- **相對初始建構的 Score（幾何平均）：**

  | 測資組 | Hill climbing | Simulated annealing | Incremental SA | Balanced + structural moves |
  |---|---:|---:|---:|---:|
  | testcases（15 筆） | 0.630 | 0.521 | 0.487 | **0.409** |
  | holdout（12 筆） | 0.452 | 0.380 | 0.313 | **0.258** |

- **v5 沒有任何一筆比 v4 差：** testcases 13 筆變好、2 筆持平；holdout 10 筆變好、2 筆持平。
- **方法 vs. 時間：** v5 的時間上限改回和 v4 相同（`min(20, 2 + N/500)`）時，幾何平均仍是 v4 的 0.869 / 0.846（testcases / holdout）；用自己的時間上限是 0.840 / 0.823。大部分改善來自方法。
- **最佳化主要降低 skew：** 例如 010 的 skew 從 3007 降到 176，cost 從 18414 降到 16862。
- **大測資接近 cost 下限：** 011 最終 Score / 下限 = 1.16、013 = 1.07。
- **速度（v3 → v4）：** 1 萬 sinks 在 20 秒內的嘗試次數從約 171 萬次增加到約 3420 萬次（約 20 倍）。
- **退火的設定實驗（第 5 課）：** 不加 σ 差 3%、起始溫度 ×0.3 差 4%、所有測資都重啟差 0.4%，所以只在 N ≤ 50 時重啟。
- **v5 各項改動的實驗（`AI_Solutions/ClaudeOpus55_v6/README.md`）：** 改接到較遠的 buffer + 最佳解保存修正約 −7～8%、Merge / Split 約 −2% / −6%（testcases / holdout）、時間上限約 −2.5%。
- **執行時間：** v5 每筆 5～45 秒（N ≤ 12 為 5 秒，其他 `min(45, 10 + N/100)`），最長 45.05 秒，限制是 60 秒。

**注意：** 模擬退火有隨機性，每次跑的分數會有幾 % 的差異，報告的數字以 `results/` 裡的結果為準。
