# v1 — 合法化：Tetris → Abacus（Milestone 1 ~ 4）

> 第一個版本只做 Stage 1：讀檔、評分、寫檔，再加上合法化。先做講義 L07 的 **Tetris**，它在 ibm01、L1、L2 都放不下（no room），
> 所以改成 **Abacus** [Spindler, Schlichtmann, Johannes, ISPD 2008]。Tetris 留著當比較（`--legalizer tetris`）。
> ibm01、ibm05、L1 都合法了；**L2（100 萬 cell）300 秒內跑不完**。Stage 2 還沒做（`Total HPWL (SA)` = Stage 1 的 HPWL）。

## 編譯與執行

```bash
cmake -S . -B build
cmake --build build
./build/dp INPUT_FILE OUTPUT_FILE [-v] [--legalizer tetris] [--own-cost]
```

stdout 印題目要求的 4 行，`-v` 會在 stderr 印 `[read]`、`[legal]` 紀錄。

| 選項 | 說明 |
|---|---|
| `--legalizer tetris` | 改用 Tetris（比較用；預設 `abacus`） |
| `--own-cost` | Abacus 選 row 時只看新 cell 自己的位移（比較用，見下面） |

`--seed`、`--time`、`--trace` 先保留，v2 才有作用。

## 測資說明

| 代號 | 資料夾 | 內容 |
|---|---|---|
| toy | `Testcases/testcases/toy/` | 31 個 cell，只拿來對 TUTORIAL 的檢查點 |
| **ibm01** | `Testcases/testcases/ibm01/` | 12,028 cells，使用率 85%，左邊界有 5 倍過載的熱點 |
| **ibm05** | `Testcases/testcases/ibm05/` | 28,146 cells，使用率 80%，1,201 個 fixed pad |
| **L1** | `Testcases/large/L1/` | 20 萬 cells，有熱點、macro、split row |
| **L2** | `Testcases/large/L2/` | 100 萬 cells |

依 `Lab3/README.md` 的量測規則：**一次只跑一個 dp**，照 10/28 的方式 `./dp input001 output001`，兩個輸出都用 `Grader/tools/check_pl.py` 檢查。
腳本：`Testcases/results/MyCode/run_versions.py`，原始紀錄：`Testcases/results/MyCode/records.csv`。
所有數字越小越好（位移、HPWL 的單位都是座標單位）。

## 這一版做了什麼（和骨架相比）

### M1：讀檔（`placedb.cpp`）

| 檔案 | 讀什麼 | 要注意 |
|---|---|---|
| `.nodes` | 名字、寬、高、`terminal` | 跳過 `NumNodes`、`NumTerminals`；`terminal_NI` 也算 fixed |
| `.pl` | GP 座標、方向、`/FIXED` | ibm05 的 pad 沒有 `/FIXED`，只有 `.nodes` 的 `terminal` |
| `.nets` | `NetDegree : k [名字]` 後面 k 行 | 同一個 node 在一條 net 出現兩次 → `sort` + `unique` 去掉 |
| `.scl` | 每個 `CoreRow` 的 y、高度、site 間距、每個 `SubrowOrigin` 和 `NumSites` | 關鍵字轉小寫（ibm05 寫 `Numsites`）；一個 row 可以有好幾個 subrow |

**fixed 物件挖洞**：有面積的 fixed 物件蓋到 row 時，把被擋住的 sites 挖掉，subrow 切成好幾個 segment
（site `k` 被擋住 ⇔ `x0 + k·sp < ox + ow` 且 `x0 + (k+1)·sp > ox`）。課程的測資沒有這種情況，L1、L2 有（L1：481 rows → 582 segments；L2：794 → 970）。

位置存成「第幾個 segment 的第幾個 site」，所以 cell 一定在 row 上、一定對齊 site；之後所有程式只需要管「不超出 segment、不重疊」。

### M2：評分與寫檔（`placedb.cpp`）

- `displacement`：Σ / max |x − gx| + |y − gy|（左下角）。
- `totalHPWL`：每條 net 的 pin = cell 中心，bounding box 寬 + 高，fixed 也算 pin，不看 `.wts`。
- `checkLegal`：每個 cell 在自己的 segment 裡；每個 segment 依 site 排序後前一個的結尾 ≤ 下一個的開頭。`main` 輸出前一定會呼叫它，不合法就不寫檔。
- `writePl`：`name<TAB>x<TAB>y<TAB>: orient [/FIXED]`，fixed 的原樣寫回。

檢查點：GP 本身的 HPWL 要等於 `check_pl.py` 算的（toy 2,698.3、ibm01 54,504,337.3、ibm05 10,669,960.6）。

### M3：Tetris（`legalizer.cpp` 的 `tetris`）

```
cells 依 GP 的 x 排序
每個 segment 記 edge = 已經用到第幾個 site
for 每個 cell v：
    從離 v 最近的 row 往上下展開（forRowsNear；|dy| 已經 >= 目前最好就停）：
        每個 segment s：site = max(edge[s], v 想去的 site)，放得下就算 cost = |dx| + |dy|
    放到 cost 最小的地方，edge[s] = site + 寬度
```

只會往右放、不會回頭：一個 segment 左邊空出來的位置，之後的 cell 再也用不到。

### M4：Abacus（`legalizer.cpp` 的 `Abacus`）

一樣依 x 排序、一樣加在 row 的最右邊，但 row 裡**緊貼在一起的一串 cells（cluster）會一起移動**到「每個 cell 離自己想去的位置的平方和最小」的地方。cluster 只要記 4 個數：

| | 意義 |
|---|---|
| `e` | 權重總和（每個 cell 權重 1 → cell 數） |
| `q` | Σ e_i · (u_i − offset_i)，u_i = cell i 想去的 site，offset_i = cell i 在 cluster 裡的起點 |
| `w` | 總寬度（sites） |
| `x` | cluster 左緣 = `clamp(round(q / e), 0, numSites − w)` |

cluster 左緣在 x 時 cell i 在 `x + offset_i`，要最小化 Σ (x + offset_i − u_i)²，微分 = 0 → **x = Σ(u_i − offset_i) / n = q / e**。

```
加一個 cell v（寬 w_v、想去 u_v）：先自己當一個 cluster {e=1, q=u_v, w=w_v}
while 跟左邊的 cluster c 重疊（c.x + c.w > x）：
    c.q += q_new − e_new × c.w      ← 接在 c 後面，新的 cells 的 offset 都多了 c.w
    c.e += e_new;  c.w += w_new
    c.x  = clamp(round(c.q / c.e), 0, numSites − c.w)
```

合併只會往左連鎖，所以**只有最後一個 cluster 的 cells 位置會變**。選 row：`trial(v, s, dy)` 用區域變數模擬同樣的合併（不改資料），回傳 cost；
從最近的 row 往上下展開，挑 cost 最小的 row 再 `commit`。cost 有兩種：

| cost | 程式 |
|---|---|
| (a) 只算 v 自己的位移 | `--own-cost` |
| (b) v 的位移 + 被合併的 cells 位移的**變化量** | 預設：把被合併的 cells 走一遍，加總 \|新位置 − u\| − \|舊位置 − u\| |

## 實驗結果

### Tetris → Abacus

| 測資 | Tetris 總位移 | **Abacus 總位移** | Tetris 最大位移 | **Abacus 最大位移** | **Abacus HPWL** | Abacus 時間 |
|---|---:|---:|---:|---:|---:|---:|
| toy | 60,495.6 | **48,976.8** | 2,913.0 | **2,226.0** | 93,114 | 0.0 s |
| ibm01 | 失敗：`no room for a6614` | **36,759,608.4** | | **17,782.7** | 87,346,806 | 0.05 s |
| ibm05 | 2,606,792.6 | **1,771,546.9** | 1,027.0 | **330.4** | 12,029,591.5 | 0.14 s |
| L1 | 失敗：`no room for c174360`（0.8 s） | **4,113,200,221.0** | | **117,643.6** | 4,767,648,614.6 | 10.4 s |
| L2 | 失敗：`no room for c918951`（5.5 s） | 300 秒沒跑完 | | | | > 300 s |

Tetris 的數字是同一份程式加 `--legalizer tetris`（紀錄的版本名 `v1_tetris_mode`）。
toy、ibm01、ibm05 都和 TUTORIAL Milestone 3、4 的檢查點完全一樣。ibm05：總位移 −32%、最大位移 −68%。

### Abacus 的兩種 cost

| cost | toy 總 / 最大 | ibm01 總 / 最大 | ibm05 總 / 最大 |
|---|---|---|---|
| (a) 只算 v 自己 | 49,700.6 / 2,548.0 | 40,796,017.3 / 26,570.8 | 1,804,387.5 / 282.3 |
| **(b) + 被合併 cells 的變化（預設）** | **48,976.8 / 2,226.0** | **36,759,608.4 / 17,782.7** | **1,771,546.9** / 330.4 |

(b) 的總位移比較小（ibm01 −10%），因為 (a) 只要新 cell 自己近就好，不管把整串 cluster 推走多遠。
ibm05 的最大位移是 (a) 比較好：(b) 為了總和，會讓少數 cell 走遠一點。

## 觀察

- **Tetris 為什麼放不下：** ibm01 使用率 85%，左邊界有一塊 5.4 倍過載。Tetris 只往右放，熱點的 cells 把附近 rows 的 `edge` 推得很右，
  後面的 cells 只能放得更右；最後幾個 cell 發現**每個 row 的右邊都滿了**，可是左邊其實還有空位。L1、L2 也一樣（有熱點）。
  ibm05 熱點只有 3 倍，放得下，但最大位移 1,027：先放的 cells 不會讓位，後來的只能被擠到很遠。
- **Abacus 為什麼放得下：** 新 cell 加在最右邊時，左邊的 cluster 可以整串往左移，空出來的位置被重新利用。
  只要還有某個 segment 的空 sites 總數 ≥ cell 寬，Abacus 就放得下（cluster 最左可以推到 site 0，空位一定會被擠到右邊）。
- **最大位移還是集中在熱點：** ibm01 最大 17,783（約 35 個 row 高），因為 x 小的 cells 先挑位置，熱點裡最後來的 cells 只能去很遠的 row。v3 的微調專門處理這個。
- **L2 為什麼跑不完：** L2 的熱點裡 cluster 有上萬個 cells。(b) 每次 `trial` 都把被合併的 cells 走一遍，`commit` 也每次重寫整個最後一個 cluster，
  兩個都是 O(cluster 長度)，乘上 cell 數就是 O(n²)。L1 熱點比較小，10 秒跑完。v3 才修。
- **HPWL 從 GP 的 5,450 萬變成 8,735 萬（ibm01）：** 合法化只管位移，不管 net。v2 用 SA 把 HPWL 拉回來。
