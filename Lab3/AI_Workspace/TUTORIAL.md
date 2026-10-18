# Project 3 — Standard Cell Detailed Placement 實作教學

> 目標：自己從骨架做出 `dp`，交出 `學號.tgz`（程式 + ReadMe）和報告 PDF（≤ 3 頁）。
> 截止：**2026/11/04 23:59**

---

## 0. 資料夾導覽

跟 Lab2 同樣的結構：

| 路徑 | 內容 |
|---|---|
| `Project03.pdf`、`GSRC Bookshelf Format ….txt` | 題目、輸入檔格式說明 |
| `Example/` | 題目給的範例：toy + PDF 裡的輸入 / 輸出圖（PDF 的數字對不起來，見裡面的 README） |
| `Testcases/testcases/{toy,ibm01,ibm05}/` | 課程測資（`Testcases/bench.tgz` 解壓出來的） |
| `Testcases/holdout/h01..h08/` | 報告 / 穩健度測試用的新測資：有 macro、split row、pad、小數座標 |
| `M11502102/` | **你的繳交資料夾**（目前是骨架的複本），在這裡寫 |
| `MyCode/my_dp_skeleton/` | 原始骨架；之後每個版本也存在 `MyCode/`（v1_tetris、v2_abacus…） |
| `AI_Solutions/ClaudeOpus55/` | **參考解**（先別看！每個 Milestone 自己做完再對照） |
| `AI_Solutions/ClaudeOpus55_v2/` | 改良版：查論文後加的同寬匹配、每 row 精確重排、流量式 spreading、SA 補救 move（11 筆測資 HPWL 平均 −5%），自己的版本做完再看 |
| `AI_Solutions/ClaudeOpus55_v3/` | 第三版：講義 L07 的 linear placement、independent set matching、Stage 1 收尾輪（HPWL 比 v1 平均 −7%） |
| `AI_Solutions/COMPARISON.md` | 五個版本（v1、v2、v3、兩個 Codex 版）的比較、離最佳解多遠（toy、h01 的精確最佳解）、還能改良的地方 |
| `AI_Workspace/step1_example/` | 參考解跑 toy 的輸出和圖 |
| `AI_Workspace/reference_report/` | 參考解的實驗表格、圖、範例報告 |
| `Grader/tools/check_pl.py` | 獨立檢查器：合法性 + 總位移 + 最大位移 + HPWL（**debug 最好用**） |
| `Grader/tools/plot_pl.py` | 畫 placement（題目說 GUI 加分，這就是一個） |
| `Grader/tools/plot_trace.py` | 畫 SA 的 current / best-so-far 曲線（報告必備的圖） |
| `Grader/tools/local_grade.py` | 模擬繳交：打包 → 編譯 → 跑全部測資 → 檢查 |
| `Grader/step2_grader/` | 檢查器對 1 個正確、10 個故意做錯的輸出的判斷 |
| `Report/` | 你的報告（`draft/`、`completed/`）和跑實驗的腳本（`run_everything.sh`） |

測資資料夾不放進 git，重新 clone 的話先產生：

```bash
cd Lab3/Testcases
./make_testcases.sh        # testcases/ ← bench.tgz，holdout/ ← gen_bench.py（seed 固定，每次一樣）
```

每次開新 terminal：

```bash
conda activate eda     # 有 cmake、matplotlib
```

---

## 1. 規則總整理

### 輸入 / 輸出

```
dp INPUT_FILE OUTPUT_FILE
```

- 讀 `INPUT_FILE.aux`，它列出 `.nodes .nets .wts .pl .scl`（GSRC Bookshelf 格式）。
- 輸出：`OUTPUT_FILE_legal.pl`（Stage 1）、`OUTPUT_FILE.pl`（Stage 2），還有 `OUTPUT_FILE.aux/.nodes/.nets/.wts/.scl`（照抄輸入，aux 改成指向新檔名）。
- stdout 印 4 行：

```
Total displacement (legal): 62531.2
Maximum displacement (legal): 5219.7
Total HPWL (legal): 757296
Total HPWL (SA): 690185
```

> ⚠️ PDF 裡的範例數字**跟附的 toy 對不起來**：附的 toy 的 global placement HPWL 只有 2,698，範例卻印 legal 757,296。所以不要拿 PDF 的數字對答案，用 `Grader/tools/check_pl.py` 檢查自己的輸出。

### 名詞

| 名詞 | 定義 |
|---|---|
| global placement（GP） | 輸入的 `.pl`，cells 有重疊、沒對齊 |
| row / subrow / site | `.scl`：row 的 y（Coordinate）、高度；subrow 從 `SubrowOrigin` 開始有 `NumSites` 個 site，間距 `Sitespacing` |
| 合法（legal） | 每個 movable cell 在某個 row 上、x 對齊 site、整個在一個 subrow 裡、彼此不重疊；不能改方向；fixed 物件不能動 |
| 位移 | `d_i = |x_gp − x_legal| + |y_gp − y_legal|`（左下角，Manhattan） |
| 總位移 / 最大位移 | 所有 movable cell 的 Σ d_i / max d_i |
| HPWL | 每條 net：所有 pin（= **cell 中心** `(x + w/2, y + h/2)`）的 bounding box 寬 + 高；**不管 pin offset**；全部 net 直接相加（不看 `.wts`） |

### 題目沒講清楚、我的決定

| 狀況 | 怎麼處理 |
|---|---|
| fixed 怎麼判斷 | `.nodes` 有 `terminal`（或 `terminal_NI`）**或** `.pl` 有 `/FIXED`。ibm05 的 1201 個 pad **沒有** `/FIXED`，只有 `terminal` |
| terminal 算不算 HPWL | 算（它們是 net 的 pin），但不算位移 |
| 關鍵字大小寫 | ibm05 寫 `Numsites`、`Numrows`，toy 寫 `NumSites` → 比對前一律轉小寫 |
| net 名稱 | toy / ibm01 是 `NetDegree : 2`（沒名字），ibm05 是 `NetDegree : 5  net0` → 都要能讀 |
| fixed 物件在 row 裡面 | 給的測資沒有（ibm05 的 pad 都在晶片外），但隱藏測資可能有 → 把被擋住的 sites 從 row 挖掉 |
| fixed pad 彼此重疊 | ibm05 有（p739、p740 疊在一起），是輸入本身的，不用管 |
| 執行時間上限 | 題目沒寫 → 給一個預設時間（參考解 SA 60 秒），可以用參數改 |

### 測資

| | toy | ibm01 | ibm05 |
|---|---:|---:|---:|
| movable cells | 31 | 12,028 | 28,146 |
| terminals（fixed） | 0 | 0 | 1,201 |
| nets | 38 | 11,507 | 28,446 |
| rows × sites | 132 × 1011 | 132 × 1011 | 148 × 2360 |
| site 寬 / row 高 | 66 / 504 | 66 / 504 | 1 / 16 |
| 使用率（cell 面積 / row 面積） | 0.3% | **85.1%** | **80.0%** |
| GP 的 HPWL（有重疊） | 2,698.3 | 54,504,337.3 | 10,669,960.6 |
| GP 最擠的地方（cell 面積 / 容量） | 31 個疊在一起 | **5.4 倍**（左邊界） | 3.0 倍 |

ibm01 很擠（85%），而且左邊界有一塊 5 倍過載：那裡的 cells 一定要搬很遠，**最大位移幾乎都由這種熱點決定**。

---

## 2. 工具

以下都在 `Lab3/M11502102/` 執行（`../` = `Lab3/`）：

```bash
# 檢查合法性 + 算分數（不合法會列出原因，例如哪兩個 cell 重疊）
python3 ../Grader/tools/check_pl.py ../Testcases/testcases/toy/toy out/toy_legal.pl

# 畫圖：--arrows N 畫出位移最大的 N 個 cell（GP → 結果），--heat 依位移上色，--window 放大
python3 ../Grader/tools/plot_pl.py ../Testcases/testcases/toy/toy out/toy_legal.pl --arrows 31 --window 26000 -31000 32000 -26000 -o toy.png
python3 ../Grader/tools/plot_pl.py ../Testcases/testcases/ibm01/ibm01 out/ibm01_legal.pl --heat --arrows 20 -o ibm01.png
python3 ../Grader/tools/plot_pl.py ../Testcases/testcases/ibm01/ibm01 -o ibm01_gp.png    # 不給 .pl 就畫輸入

# SA 曲線（要先用 --trace 輸出 CSV）
python3 ../Grader/tools/plot_trace.py out/ibm01_trace.csv -o trace.png

# 模擬繳交：打包 → 解壓 → 編譯 → 11 筆測資 → 檢查（結果在 Grader/local_grade_M11502102/record.csv）
python3 ../Grader/tools/local_grade.py . M11502102 ../Testcases/testcases ../Testcases/holdout
```

`check_pl.py` 跟你的程式**完全獨立**（自己讀 `.scl` 檢查幾何），所以它說 LEGAL 才算數。
想知道它會抓哪些錯，看 `Grader/step2_grader/record.csv`（重疊、沒對齊 site、不在 row 上、超出 subrow、
放在 split row 缺口、疊在 macro 上、fixed 被移動、方向被改、少 cell）。

---

## 3. Milestones

`Lab3/M11502102/` 已經是骨架的複本，直接在那裡寫：

```bash
cd Lab3/M11502102
cmake -S . -B build && cmake --build build
./build/dp ../Testcases/testcases/toy/toy out/toy -v
```

跟 Project 2 一樣，每完成一個里程碑就存一份到 `MyCode/`（例如 `cp -r M11502102 MyCode/v1_tetris`），報告比較版本時用。

骨架已經寫好 `main.cpp`（整個流程）、`placedb.hpp`（資料結構）、小工具，要補的地方都標了 `TODO(Mx)`。骨架一開始會印 `error: TODO(M1): ...`。

### 先看懂資料結構（`placedb.hpp`）

最重要的設計：**movable cell 的位置存成「第幾個 segment 的第幾個 site」**，不是 (x, y)。

```
segment = 一段連續、沒被擋住的 sites（一個 subrow；被 fixed 物件擋住就切成好幾段）
x = seg.x0 + site * seg.sp       y = seg.y
```

這樣 cell 一定在 row 上、一定對齊 site，合法性只剩兩件事要管：**不超出 segment**、**不重疊**。fixed node 的 `seg = -1`，位置就是輸入的 `(gx, gy)`。

---

### Milestone 1：讀檔 `PlaceDB::read()`（`placedb.cpp`）

`.aux` 已經寫好。用骨架給的 `tokens(line)`：它會刪掉 `#` 註解，而且把 `:` 切成獨立的 token。

| 檔案 | 一行長這樣 | 取什麼 |
|---|---|---|
| `.nodes` | `a10084  924.0  504.0` / `p1  1  1  terminal` | name、w、h、是否 terminal |
| `.pl` | `a10084  28840.7  -29019.6  :  N` | gx、gy、orient、`/FIXED` |
| `.nets` | `NetDegree : 3 [name]` 後面接 3 行 `a10828  I : 88 252` | 每行第一個 token = node 名字 |
| `.scl` | `CoreRow Horizontal` … `Coordinate : -33208` … `SubrowOrigin : -33330 NumSites : 1011` … `End` | y、height、spacing、x0、numSites |

要注意：
- 跳過檔頭 `UCLA nodes 1.0`、`NumNodes : ...`、`NumTerminals : ...`、`NumNets`、`NumPins`。
- `.scl` 的關鍵字**轉小寫**再比（`Numsites`）。`Sitespacing` 是 site 的間距（通常 = `Sitewidth`）。
- 一條 net 裡同一個 node 出現兩次 → 去掉重複（`sort` + `unique`）。
- rows 要依 y 排序；`nearestRow()` 靠這個做二分搜尋。
- segments：先做最簡單的「一個 subrow = 一個 segment」。之後有空再補「fixed 物件挖洞」：
  fixed 物件 `(ox, oy, ow, oh)` 跟 row 的 y 範圍重疊時，site `k` 被擋住的條件是 `x0 + k·sp < ox + ow` 且 `x0 + (k+1)·sp > ox`。

**檢查點**：`./build/dp ... -v` 第一行要印出（後面還會因為 M2 沒做而出錯，沒關係）

```
toy:   [read] 31 nodes (31 movable), 38 nets, 132 rows, 132 segments
ibm01: [read] 12028 nodes (12028 movable), 11507 nets, 132 rows, 132 segments
ibm05: [read] 29347 nodes (28146 movable), 28446 nets, 148 rows, 148 segments
```

---

### Milestone 2：評分與寫檔（`placedb.cpp`）

1. `displacementOf` / `displacement`：照定義。
2. `totalHPWL`：對每條 net 求 cell 中心的 bounding box。用 `p.x(db, v)`，它對 fixed node 會回傳 `gx`。
3. `checkLegal`：每個 cell 在自己的 segment 裡（`0 <= site` 且 `site + 寬度 <= numSites`），每個 segment 依 site 排序後前一個的結尾 `<=` 下一個的開頭。
4. `writePl`：`name<TAB>x<TAB>y<TAB>: orient [/FIXED]`，數字用 `fmtNum()`。

**檢查點**：在 `main.cpp` 讀完檔後**暫時**加這幾行，算 GP 本身的 HPWL（把每個 node 都當 fixed，`p.x()` 就會回傳 `gx`）：

```cpp
Placement gp;
gp.seg.assign(db.nodes.size(), -1);
gp.site.assign(db.nodes.size(), 0);
std::fprintf(stderr, "GP HPWL = %.1f\n", totalHPWL(db, gp));
```

要等於 toy **2698.3**、ibm01 **54504337.3**、ibm05 **10669960.6**（跟 `check_pl.py ../Testcases/testcases/X/X ../Testcases/testcases/X/X.pl` 印的一樣）。對了再刪掉。

---

### Milestone 3：第一個合法解 — Tetris（`legalizer.cpp`）

講義 L07「Greedy Legalization [ISPD'04]」：

```
cells 依 GP 的 x 排序（xOrder）
每個 segment 記 edge = 已經用到第幾個 site（一開始 0）
for 每個 cell v：
    for 附近的 row r（forRowsNear：從最近的 row 往上下展開）：
        for r 的每個 segment s：
            u = (gx - s.x0) / s.sp                        ← v 想去的 site（實數）
            site = max(edge[s], round(u))，放不下（site + w > numSites）就跳過
            cost = |s.x0 + site*s.sp - gx| + |dy|
    挑 cost 最小的 (s, site)，edge[s] = site + w
```

`forRowsNear(db, y, visit, best)` 骨架已經寫好：它從最近的 row 開始上下交替，**一旦 |dy| ≥ best() 就停**（再遠的 row 光垂直距離就輸了）。

**檢查點**：`check_pl.py` 顯示 LEGAL，而且數字接近

| | 總位移 | 最大位移 |
|---|---:|---:|
| toy | 60,495.6 | 2,913.0 |
| ibm05 | 2,606,792.6 | 1,027.0 |
| ibm01 | **放不下（no room）** | |

**ibm01 失敗是正常的**，想想看為什麼：Tetris 只會往右放，左邊用過的空間不會再回頭用。ibm01 使用率 85%，最後幾個（最右邊的）cell 會發現所有 row 的右邊都滿了。這正好是報告可以寫的觀察，也是 Abacus 的動機。

---

### Milestone 4：Abacus（`legalizer.cpp`）

[Spindler, Schlichtmann, Johannes, *Abacus: Fast Legalization of Standard Cell Circuits with Minimal Movement*, ISPD 2008]

想法：一樣依 x 排序、一樣加在 row 的最右邊，但**左邊已經放好的 cells 可以被推動**。row 裡緊貼在一起的一串 cells 叫 **cluster**，整串一起移到「每個 cell 離自己想去的位置的平方和最小」的地方。

cluster 只要記 4 個數：

| | 意義 |
|---|---|
| `e` | 權重總和（每個 cell 權重 1 → 就是 cell 數） |
| `q` | Σ e_i · (u_i − offset_i)，offset_i = cell i 在 cluster 裡的起點 |
| `w` | 總寬度（sites） |
| `x` | cluster 左緣 = `clamp(round(q / e), 0, numSites − w)` |

為什麼最佳位置是 `q / e`：cluster 左緣在 x 時 cell i 在 `x + offset_i`，要最小化 Σ (x + offset_i − u_i)²，微分 = 0 → x = Σ(u_i − offset_i) / n。

**加一個 cell**（寬 w_v、想去 u_v）：先自己當一個新 cluster `{e=1, q=u_v, w=w_v}`。
**合併**：新 cluster 跟左邊的 cluster `c` 重疊（`c.x + c.w > x`）時，把新的接在 `c` 後面：

```
c.q += q_new - e_new * c.w      ← 新的 cells 在合併後的 offset 都多了 c.w
c.e += e_new
c.w += w_new
c.x  = clamp(round(c.q / c.e), 0, numSites - c.w)
```

合併完可能又跟更左邊的重疊 → 繼續合併（while 迴圈）。最後，**只有最後一個 cluster 的 cells 位置會變**，從 `x` 開始一個接一個排過去。

`trial(v, s, dy)`：同樣的計算但**不能改到資料**（用區域變數模擬，從右往左看要合併幾個 cluster）。cost 有兩種選法，報告可以比較：

| cost | toy 總 / 最大 | ibm01 總 / 最大 | ibm05 總 / 最大 |
|---|---|---|---|
| 只算 v 自己的位移 | 49,700.6 / 2,548.0 | 40,796,017 / 26,570.8 | 1,804,388 / 282.3 |
| v 的位移 + 被合併 cells 位移的**變化量** | 48,976.8 / 2,226.0 | 36,759,608 / 17,782.7 | 1,771,547 / 330.4 |

第二種要把被合併的 cells 走一遍（`|新位置 − u| − |舊位置 − u|`），慢一點但總位移比較小。

**檢查點**：三筆都 LEGAL，數字接近上表。用 `plot_pl.py --arrows` 看 toy：會是一個漂亮的菱形（Manhattan 距離下最省的形狀）。

---

### Milestone 5（進階）：合法之後再降位移

Abacus 是貪心的（x 小的先挑位置），最大位移通常集中在熱點。參考解的做法（`AI_Solutions/ClaudeOpus55/src/legalizer.cpp` 的 `Refiner`，**自己想過再看**）：

1. **insert-with-push**：把 cell c 拿起來，插到某個 segment 的某個 site，左右鄰居各自往外推「剛好夠」的距離。被推的鄰居新的位移不能超過上限。
2. **swap**：c 和 b 交換，各自放得進對方的空間（`RowMap::freeAround`）。
3. **最大位移**：反覆拿位移最大的 cell c，上限設成「目前最大值 − 1 個 site」，找得到 insert / swap 就做 → 最大值只會下降。卡住時試 **evict**：附近找一個位移很小的 cell b 拿出來，c 放進 b 的位置，b 再去找別的地方。
4. **總位移**：每個 cell 試 insert / swap，總位移變小、而且不超過目前最大值就做。3、4 交替幾輪。

| | toy 總 / 最大 | ibm01 總 / 最大 | ibm05 總 / 最大 |
|---|---|---|---|
| Abacus | 48,977 / 2,226.0 | 36,759,608 / 17,782.7 | 1,771,547 / 330.4 |
| + insert / swap 微調 | 46,282 / 2,079.1 | 27,070,626 / 11,607.0 | 1,432,267 / 229.4 |
| + evict（參考解） | 46,619 / 1,987.6 | 27,235,501 / 10,578.3 | 1,431,842 / 217.6 |

觀察（報告可寫）：ibm01 微調到最後，有幾百個 cell 的位移都卡在最大值附近（像一道牆）：熱點裡的 cells 要一起往外移才能再降，一次只動一兩個 cell 的 local move 做不到。

這一步需要 `RowMap`（Milestone 6 也會用到），可以先做 M6 的 `RowMap` 再回來。

---

### Milestone 6：模擬退火（`annealer.cpp`、`rowmap.hpp`）

#### 6-1 `RowMap`：保證每一步都合法

每個 segment 一個依 site 排序的 cell 清單。補完兩個函式：

- `freeAround(v)`：v 拿起來之後的空間 = 左鄰居結尾 ~ 右鄰居開頭。
- `cellAt(s, t, gap)`：site t 被誰蓋住？沒人就回傳 -1，並給出 t 所在的空位。
  提示：`upper_bound` 找「開頭 ≤ t 的最後一個 cell」，看它的結尾有沒有超過 t。

#### 6-2 先做一個會動的 SA

```
cur = totalHPWL; best = cur; 記下 best 的 placement
T = T0
for step in 1..S:                    ← 每個 step 一個溫度
    repeat K 次:
        挑 cell a；目標點 = a 附近 window 內的隨機點
        目標是空位而且放得下 → move = a 搬過去
        目標上有 cell b → move = a、b 交換（a 放得進 freeAround(b)、b 放得進 freeAround(a) 才行；
                                             a、b 在同一個 segment 相鄰的話直接對調順序）
        d = 新 HPWL − 舊 HPWL（只重算 a、b 所在的 nets！）
        if d <= 0 或 rand() < exp(-d / T): 接受（更新 RowMap、cur += d）
        else: 還原
        if cur < best: best = cur，記下來
    T *= alpha
```

重點：
- **只重算受影響的 nets**（`nodeNets[a]`、`nodeNets[b]`，同一條 net 不要算兩次）。整個重算一次要幾毫秒，會慢上千倍。
- 每個 move 一定合法 → 不需要 overlap penalty，cost 就是 HPWL。
- **輸出最好的解**，不是最後的解。最簡單：`cur < best` 就把整個 `seg/site` 複製一份（一開始可以這樣，之後覺得慢再優化）。
- `--trace`：每隔 (總次數 / 2000) 次寫一行 `iteration,current,best,temperature,accept_rate`。

#### 6-3 讓它變好（每一項都可以寫進報告，做對照實驗）

| 改進 | 做法 | ibm01 / ibm05 少了它會差多少（參考解） |
|---|---|---|
| **range limiter**（講義 TimberWolf） | 目標點只在 a 附近的 window 裡找；window 依接受率調整（VPR：`window *= 1 − 0.44 + 接受率`） | +46.9% / +11.1% |
| **optimal region** | 一半的 move 把目標設在 a 的「最佳區域」：a 的每條 net 去掉 a 之後的 bounding box，所有 box 的左右邊界取**中位數區間**（x、y 分開） | +36.5% / +8.5% |
| **初始溫度** | 先試做幾千個 move，Δ̄ = 變差的 move 的平均變差量，`T0 = −Δ̄ / ln(p0)`，參考解 p0 = 0.8 | 純貪心（T≈0）：+10.8% / +3.2% |
| **時間控制** | 先固定總次數；跑完第一個溫度量速度，時間不夠就每個溫度少做一點，但溫度一樣降到底 | |
| **best 不要整份複製** | undo journal：記「上次最好之後，每個 cell 第一次被動到時的位置」，結束時還原 | 速度 |

參考解最後的結果：ibm01 90.9M → **60.0M**（−34%）、ibm05 12.0M → **10.31M**（−14%）。

**檢查點**：`check_pl.py` 對 `out/X.pl` 也要 LEGAL；`Total HPWL (SA)` 一定 ≤ `Total HPWL (legal)`（輸出的是 best）。

---

### Milestone 7：實驗與報告

實驗腳本在 `Report/`（跟 Lab2 一樣一行跑完）：

```bash
cd Lab3/Report
./run_everything.sh              # 你的程式（M11502102）：main + holdout → Report/{results,tables,figures}
./run_everything.sh reference    # 參考解的全部實驗 → AI_Workspace/reference_report/（約 5 分鐘）
ONLY=main,stage1 ./run_everything.sh     # 指定要跑哪幾組
```

`stage1`、`sa`、`effort`、`seeds` 這幾組會用到參考解的選項（`--no-refine`、`--p0`…），你的程式要支援，
或改 `Report/scripts/run_experiments.py` 最上面的 `STAGE1`、`SA` 兩個表。詳細見 `Report/README.md`。

holdout（`Testcases/holdout/`）的 global placement 是合成的，SA 降 HPWL 的幅度會比 ibm 大很多（參考解 30 ~ 66%），
它主要是用來確認程式遇到 macro、split row、pad、小數座標也不會出錯。

---

## 4. 報告（≤ 3 頁 PDF）

題目要求（第 10 點）：
1. 說明**初始合法解怎麼建**、**SA 怎麼在維持合法的前提下最佳化**，用分析、實驗、trade-off 支持。
2. 報告 Stage 1 的**總位移、最大位移**；比較**同一組輸入** SA 前後的 HPWL。
3. **畫 current 與 best-so-far HPWL 對「嘗試次數」的圖**，解釋 SA 的設計和觀察到的行為。
4. GUI、分析工具、特別功能加分。

建議架構（參考 `AI_Workspace/reference_report/reference_report.md`），草稿放 `Report/draft/`、定稿放 `Report/completed/`：

| 節 | 內容 | 篇幅 |
|---|---|---|
| I. Stage 1 | Tetris → Abacus（cluster 公式）→ 微調；為什麼一定合法；表格：各方法的總 / 最大位移 | 0.9 頁 |
| II. Stage 2 | 狀態、move、為什麼每一步都合法、增量計算、溫度、range limiter | 0.8 頁 |
| III. 實驗 | 主表（位移、HPWL legal → SA、時間）；SA 曲線圖；SA 對照表（greedy、沒有 range limiter…）；effort 曲線 | 1 頁 |
| IV. 觀察 | 熱點決定最大位移、位移 vs HPWL 的 trade-off、Tetris 為什麼在 ibm01 失敗 | 0.3 頁 |

可以寫的觀察（參考解的實驗都有數據）：
- Stage 1 位移越小，legal HPWL 不一定越小（ibm01：Abacus 87.3M，微調後 90.9M）→ 兩個 stage 的目標不同。
- SA 的 current 曲線一開始會**往上衝**（ibm01 從 91M 衝到 177M，接受變差的 move），之後才降到比貪心更低（60.0M vs 66.5M）：這就是 SA 跳出 local minimum 的證據。
- 沒有 range limiter 時，高溫下的大範圍 move 把 placement 打亂到 7 倍，時間內回不來。

---

## 5. 繳交 checklist

- [ ] 用乾淨的資料夾從頭編譯一次（`rm -rf build && cmake -S . -B build && cmake --build build`），沒有 warning
- [ ] `dp` 不帶參數會印用法；輸入檔不存在會印錯誤，不會 crash
- [ ] `local_grade.py` 跑 `testcases` + `holdout` 全部 PASS（合法、7 個輸出檔都有、stdout 的數字跟 `check_pl.py` 一致）
- [ ] stdout 4 行格式跟題目一樣
- [ ] `Total HPWL (SA) <= Total HPWL (legal)`
- [ ] ReadMe（純文字）寫怎麼編譯、怎麼執行、選項
- [ ] 打包：檔名 = 學號

```bash
cd Lab3
tar czf M11502102.tgz --exclude=build --exclude=out M11502102
tar tzf M11502102.tgz            # 確認裡面只有 src/、CMakeLists.txt、Makefile、README
```

（`local_grade.py` 也會在 `Grader/local_grade_M11502102/submissions/M11502102.tgz` 產生一份一樣的。）

- [ ] 報告 PDF 另外上傳（≤ 3 頁）
