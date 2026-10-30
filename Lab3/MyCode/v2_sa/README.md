# v2 — 模擬退火（Milestone 6）

> 加上 Stage 2：每一步都合法的 SA，cost 只有 HPWL。先做一個「會動的」基本版（M6-1、6-2），它在 ibm 有效，但 **L1 時間到時還在高溫，HPWL 完全沒降**；
> 再加 TUTORIAL 6-3 的五項改進（optimal region、range limiter、時間控制、undo journal、CSR），L1 −41%、ibm 再少 3 ~ 4%。
> Stage 1 和 v1 完全一樣；L2 還是卡在 Abacus（v3 才修）。

## 編譯與執行

```bash
cmake -S . -B build
cmake --build build
./build/dp INPUT_FILE OUTPUT_FILE [options]
```

| 選項 | 說明 |
|---|---|
| `--time SEC` | SA 時間上限（預設 40 秒：10/28 規定整支程式 60 秒，留 20 秒給讀檔、Stage 1、寫檔） |
| `--effort K` | SA 總嘗試次數 = K × cell 數（預設 2000） |
| `--seed N` | 亂數種子（預設 1） |
| `--trace FILE` | SA 過程寫成 CSV：`iteration,current,best,temperature,window_x,window_y,accept_rate`（`Grader/tools/plot_trace.py` 畫圖） |
| `--p0` `--tend` `--steps` | 初始接受率（0.8）、最終溫度比例（0.001）、溫度個數（100） |
| `--target P` | move 的目標點取 optimal region 的機率（0.5） |
| `--win0 F`、`--minwin C R` | 初始 window（晶片的 5%）、最小 window（4 個 cell 寬 × 1.5 個 row） |
| `--clamp-target` | optimal region 的目標也限制在 window 裡（實驗用，比較差） |
| `--no-sa`、`--legalizer`、`--own-cost`、`-v` | 同 v1 |

## 測資說明

和 [v1](../v1_abacus/README.md#測資說明) 一樣：ibm01、ibm05、L1、L2，一次只跑一個 dp，`check_pl.py` 檢查兩個輸出。紀錄在 `Testcases/results/MyCode/records.csv`。

## 和 v1 相比改了什麼

### M6-1 `RowMap`（`rowmap.hpp`）：保證每一步都合法

每個 segment 一個依 site 排序的 cell 清單，補完兩個查詢：

- `freeAround(v)`：v 拿起來之後的空間 = 左鄰居結尾 ~ 右鄰居開頭。
- `cellAt(s, t)`：site t 被誰蓋住（`upper_bound` 找開頭 ≤ t 的最後一個 cell，看它的結尾有沒有超過 t）；沒人的話給出 t 所在的空位。

### M6-2 基本 SA（先做一個會動的）

```
cur = 總 HPWL；best = cur
T0 = 試做幾千個 move，變差的平均 Δ̄ → T0 = −Δ̄ / ln(0.8)
100 個溫度，T 每次 × α，α = 0.001^(1/100)
每個溫度做 (2000 × cell 數) / 100 次：
    挑 cell a，目標點 = a 的中心 ± window（晶片寬、高的 5%，固定）
    目標是空位而且放得下 → a 搬過去
    目標上有 cell b       → a、b 交換（a 放得進 freeAround(b)、b 放得進 freeAround(a)；同一個 segment 相鄰就直接對調順序）
    d = 新 HPWL − 舊 HPWL（只重算 a、b 的 nets，同一條 net 用 stamp 只算一次）
    d <= 0 或 rand < exp(−d / T) → 接受，否則還原
    時間到 → 直接停
每個溫度結束：cur < best 就把整個 placement 複製一份
最後輸出 best
```

| 設計 | 為什麼 |
|---|---|
| 每個 move 都合法 | 狀態永遠是合法的 placement，cost 不需要 overlap penalty；`main` 最後再 `checkLegal` 一次當保險 |
| 只重算受影響的 nets | 整個重算一次 HPWL 要幾毫秒，增量只要幾百奈秒 |
| T0 用取樣估 | 各測資的 HPWL 差 1000 倍（變差的 move 平均變差多少：ibm05 約 230，L1 約 3 萬），不能寫死 |

### M6-3 讓它變好（現在的程式）

| 改動 | 程式裡的位置 | 做法 | 解決基本版的什麼問題 |
|---|---|---|---|
| **(A) optimal region** | `optimalRegion`、`propose` | 一半的 move 把目標點設在 a 的「最佳區域」，另一半才是 window 裡的隨機點 | 隨機目標大多沒用 |
| **(B) range limiter** | `run` 每個溫度結束 | `window *= 1 − 0.44 + 接受率`（限制在 ×0.5 ~ ×2），讓接受率接近 0.44 [Betz & Rose, VPR] | 固定 window：高溫太小、低溫太大 |
| **(C) 時間控制** | `run` 每個溫度結束 | 量目前速度，剩下的時間平均分給剩下的溫度：**每個溫度少做一點，但溫度一樣降到底** | 時間到就停 → 大設計停在高溫 |
| **(D) undo journal** | `commit`、`restoreBest` | 記「上次最好之後，每個 cell 第一次被動到時的位置」；結束時倒回去 | 每個溫度才存一次 best，會錯過中間的最好值；每次都存又太慢 |
| **(E) CSR** | `Csr` | net → pins、cell → nets 攤平成一個陣列，並去掉不到 2 個 pin 的 net | 速度 |

**(A) optimal region：** cell a 的每條 net，**去掉 a 之後**的 bounding box 有左右兩個邊界；所有 net 的邊界放在一起，取中位數區間（x、y 分開）。
a 放在這個區間裡任何一點，a 自己那幾條 net 的 HPWL 總和最小（Manhattan 距離的中位數性質）。
隨機 move 是「試試看附近」，optimal region 是「直接去最好的地方」，兩個一半一半：只用後者會一直把 cells 往同一點擠。

**(D) undo journal：** 只記「上次最好之後被動過的 cells 原本在哪」（每個 cell 只記第一次），有新的最好就清空；
結束時從後往前倒回去，就是真正的最好那一刻。記錄的量只和「最好之後動過幾個 cell」有關。

## 實驗結果

| 測資 | HPWL legal（= v1） | 基本 SA（M6-2） | **v2（M6-3）** | v2 ÷ 基本 | v2 ÷ legal | v2 時間 |
|---|---:|---:|---:|---:|---:|---:|
| ibm01 | 87,346,806 | 62,076,678（6.6 s） | **60,343,290** | −2.8% | −30.9% | 11.1 s |
| ibm05 | 12,029,591.5 | 10,785,182.5（20.6 s） | **10,323,185.5** | −4.3% | −14.2% | 35.9 s |
| L1 | 4,767,648,614.6 | 4,767,648,614.6（50.4 s） | **2,794,891,505.8** | **−41.4%** | −41.4% | 50.1 s |
| L2 | 300 秒沒跑完 | | | | | > 300 s |

基本 SA 的紀錄版本名是 `v2_basic_sa`（M6-2 做完時存的那份）。Stage 1 和 v1 完全一樣，四筆的兩個輸出都 LEGAL，`Total HPWL (SA)` 都 ≤ `Total HPWL (legal)`。

### 每一項的貢獻

v2 的 SA 和參考解（`AI_Solutions/ClaudeOpus55`）的 SA 是同一個設計，TUTORIAL 6-3 有參考解拿掉每一項的結果（ibm01 / ibm05，時間不限）：

| 拿掉 | ibm01 | ibm05 |
|---|---:|---:|
| range limiter | +46.9% | +11.1% |
| optimal region | +36.5% | +8.5% |
| 溫度（改成純貪心 T ≈ 0） | +10.8% | +3.2% |

## 觀察

- **基本 SA 在 L1 完全沒用：** L1 有 20 萬 cells，2000 × 20 萬 = 4 億次 move，40 秒只做得到一小部分。基本版「溫度照原本的速度降，時間到就停」，
  停下來時還在高溫：SA 前段本來就會讓 HPWL **先變差**，best 一直是一開始的 legal placement，等於白跑 40 秒。
  實測（`-v`）：第一個溫度結束時 current 已經衝到 1.29e10（legal 的 2.7 倍），時間到時只做完約 15 個溫度，T 還有 T0 的 35%。
  (C) 時間控制讓溫度一定降到底，同樣 40 秒 HPWL 少了 41%：SA 的改善幾乎都發生在中低溫。
- **ibm 的改善比較小：** ibm 的 cell 少，基本版本來就做得完全部 move（ibm01 只要 6 秒），v2 的進步來自 optimal region 和 range limiter 讓每個 move 更有用。
  v2 比較慢（ibm01 11 秒）：optimal region 每次要掃 a 所有 net 的所有 pin。
- **SA 曲線：** 前段 current 會往上衝（接受變差的 move），中段之後才降到比一開始低很多；best 用 journal 記住，所以最後輸出的一定 ≤ legal。
- **剩下的問題（v3）：** L2 的 Abacus 跑不完；最大位移還是 Abacus 的結果（ibm01 17,783、L1 117,644），完全沒處理；
  整支程式的時間只靠「SA 40 秒」估，L1 已經 50 秒，再大一點的設計就會超過 60 秒。
