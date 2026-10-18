# CodexParetoDP：位移受限匹配與 SA 後精確局部最佳化

這是 `Lab3/AI_Solutions/` 下的獨立改進版資料夾。沿用 `ClaudeOpus55_v2` 的
Bookshelf 資料結構、Abacus／spreading／row optimization 與 SA 核心，新增
`src/optimizer.cpp`。原有三個解法與 `M11502102` 皆未修改。

不是把既有程式換名稱：新增兩個有可檢查不變量的最佳化階段，並提供小問題
窮舉測試、隨機幾何測試、四版本同條件比較腳本。這是工程上的演算法組合，
不宣稱學術上的全新演算法或全域最佳解。

## 編譯與使用

在本資料夾執行：

```bash
make
./dp ../../Testcases/testcases/ibm01/ibm01 results/manual/ibm01 \
  --seed 1 --time 60 --effort 2000 --trace results/manual/ibm01_trace.csv -v
python3 ../../Grader/tools/check_pl.py ../../Testcases/testcases/ibm01/ibm01 results/manual/ibm01_legal.pl
python3 ../../Grader/tools/check_pl.py ../../Testcases/testcases/ibm01/ibm01 results/manual/ibm01.pl
```

只需 C++17 標準函式庫；也支援 `cmake -S . -B build && cmake --build build`。
輸出遵循題目：`OUT.{aux,nodes,nets,wts,scl,pl}` 及 `OUT_legal.pl`，stdout
只印總位移、最大位移、legal HPWL、最終 HPWL 四行。位移以 GP 左下角為基準；
HPWL 以 cell 中心計算，不使用 pin offsets 或 net weights。

## 讀過現有程式後發現的改進空間

| 現有解法 | 主要方法 | 本次處理的限制 |
|---|---|---|
| CodexGapSA | 寬 cell 優先找空隙、雙 cell 修整、最佳解重啟 SA | 擁擠區只能局部搬移；多顆 cell 需同時交換才能改善的情況難處理 |
| ClaudeOpus55 | Abacus、insert/push、swap、evict、optimal-region SA | 小規模 move 容易停在局部解 |
| ClaudeOpus55_v2 | 再加 spreading、固定順序 row 最佳化、同寬 matching、三顆隨機重排 | Stage 1 的懲罰函式可能以增加總位移換取最大位移改善；Stage 2 尚未用無共同 net 的批次精確匹配 |

## Stage 1：總位移預算下的瓶頸指派

先得到 v2 的合法結果，再對**物理寬度與高度相同**的 cell 分組。用目前位置與 GP
位置的中點作空間排序，每組最多 32 顆，兩輪使用不同 row band 與錯開的分組。
僅在全設計 site spacing 相同時執行跨 row 指派；相同尺寸的 cell 交換原有位置
不會改變占用外框，因此維持合法。

令 `D[i,j]` 是 cell i 放到原本屬於 cell j 的 slot 後，對 GP 的 Manhattan 位移。
原配置總位移為 `S0 = Σ D[i,i]`，最大位移為 `B0 = max D[i,i]`。

對一個候選上限 B，只允許 `D[i,j] <= B` 的邊；Hungarian 求最小總成本完整指派。
若沒有完整指派，或最小成本大於 S0，該上限不可接受。B 增加只會加入允許的邊，
所以最小總成本不會增加，可在不大於 B0 的所有不同距離值上二分搜尋。

結果是在**此固定 group 與 slot 集合**內：

1. 總位移不超過原配置。
2. 在條件 1 下，最大位移達到最低可行值。
3. 在該最大位移上限下，再取最低總位移。

identity assignment 一定滿足 B0 與 S0，因此至少有原配置可選。每組只接受總位移、
最大位移均不增加且至少一項下降的配置；全設計兩個位移指標因此不會因本新增階段
而變差（浮點容忍值除外）。這比把兩個目標混成單一懲罰成本更容易解釋與驗證。
新增階段前後還會完整重算兩項指標，若不變量被破壞便報錯。

每個 group 的矩陣有 O(k²) 個距離，二分搜尋 O(log k) 次 O(k³) 指派，因此最壞
O(k³ log k)。使用小 group 控制時間，但 group 外的搬移鏈仍可能找不到。

## Stage 2：SA 加上四種確定性下降步驟

保留 v2 的 SA：optimal-region 與隨機目標、合法 move/swap/reorder、溫度校正、
range limiter、最佳解還原。之後對其最佳結果執行至多三輪：

### 1. 單 cell 精確水平滑動

固定其他 cell，取得該 cell 左右鄰居間的合法空間。每條 incident net 去掉此 cell
後的 bounding box 固定，總 HPWL 對 x 為凸分段線性函式。以離散差分二分搜尋
找到合法整數 site 區間的最低值；只接受嚴格下降，避免等成本漂移。

### 2. 跨 row 的 median-region 搬移與交換

對每顆 cell，檢查目前 row、最佳中位數區域附近 row 及相鄰 rows，在目標周圍
比較多個空隙及交換對象。單顆搬移使用 incident-net 外框成本；兩顆交換重算
net 聯集的真實成本。不同寬度只有在彼此空間都放得下時才交換，同 row 相鄰
的情況留給後面的三顆排列搜尋。每次選嚴格降低 HPWL 的最佳候選。
這補足只做水平滑動無法改善垂直位置的限制，不是窮舉全晶片最佳交換。

### 3. 無共同 net 的同尺寸指派

從小型空間 pool 中選出同尺寸、**彼此完全不共享 net** 的 cell，最多 32 顆。
以 cell i 放到 slot j 的 incident-net HPWL 作成本，求完整最小成本指派。

關鍵是 independent-set 條件：每條受影響的 net 至多含一顆被指派的 cell，
所以各 cell 成本相加就是這批 net 的真實 HPWL。若忽略此條件，共享 net 的成本
會互相影響，逐顆計算的 gain 不能直接相加。未選入的 cell 留在 pool，後續另組，
不會因為當次有衝突就永遠漏掉。

### 4. 相鄰三顆 cell 的六種排列

在原有聯合外框與兩個 gap 大小內，窮舉六種排列。不同寬 cell 也可以處理。
這裡容許共享 net，但以受影響 net 的**聯集**一次重算，避免重複／錯估成本。
選成本最低的排列，然後滑動視窗。

四步均含原位置／原排列作候選且只接受下降，每輪後完整重算 HPWL 與合法性。
所以新增下降階段不會使同一次 SA 的最佳結果變差。**Stage 1 改變會影響 SA 路徑，
不代表任何 seed、時間預算下都必定勝過另一份程式。** 實際效果見測試表。

`Total HPWL (SA)` 是整個 Stage 2 的最終值，包含 SA 後的局部下降。為避免混淆：

- `--trace F` 的 F 只記錄 SA 嘗試次數、current、best、溫度等。
- `F.polish.csv` 另記每輪 slide／relocate／matching／reorder 的真實 HPWL。
- stderr 的 `[polish-total]` 清楚記錄純 SA → 完成後處理的數字。

## 新增選項與成本

| 選項 | 預設 | 用途 |
|---|---:|---|
| `--pareto-passes N` | 2 | 新增位移匹配的輪數 |
| `--no-pareto` | — | 關閉新增位移匹配 |
| `--polish-passes N` | 3 | SA 後局部下降輪數 |
| `--no-polish` | — | 關閉新增 HPWL 下降 |
| `--group-size N` | 32 | 指派群組上限，允許 2–128 |
| `--min-moves N` | 5,000,000 | SA 最少嘗試數，便於小測資控制實驗 |
| `--no-sa` | — | 關閉整個 Stage 2，保留 Stage 1 結果 |

沿用 v2 的 `--seed`、`--time`、`--effort`、`--spread`、`--no-rowopt`、`--no-matching`
等選項。預設 seed=1、effort=2000、SA time=60 秒。
**`--time` 只限制 SA 本體，不包含合法化與新增後處理**，不是程式總時間上限。
SA 次數為 `max(min-moves, effort × movable cells)`，另有時間限制；時間不足時會
縮短每個溫度的嘗試數。固定 seed 且未觸及時間限制才適合做可重現比較。

本版本另外處理：固定物件遮住整條 row 時 SA 跳過該空 row、完全沒有 movable
時跳過合法化、沒有有效 net 時不做無效 SA、檢查 cell 高度、禁止輸出／trace
覆蓋輸入，並提高 SA trace 數字精度。這些修改都只在新資料夾內。

限制：沿用 Bookshelf reader 的解析範圍；對任意損壞輸入並非完整格式驗證器。
假設 horizontal、單 row 高度 movable cells，題目測資的 sitewidth=spacing。
不支援多高度 movable macro、旋轉尺寸換算、timing/routability 最佳化。

## 測試與重跑

```bash
# 四版本相同 -O2 編譯，循序執行，外部 checker 驗證。
# 預設 v2 與新版本跑 11 筆；前兩版本跑 3 筆課程測資。
python3 tests/compare.py --effort 200 --time 1000

# 小矩陣窮舉與隨機 placement 性質測試
mkdir -p build
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  tests/optimizer_test.cpp src/optimizer.cpp src/placedb.cpp -o build/optimizer_test
ASAN_OPTIONS=detect_leaks=0 ./build/optimizer_test

# 新增邊界情況與輸入檔保護
python3 tests/edge_cases.py
```

比較腳本使用 seed=1、共同 effort 與充分的 SA 時限，並保存各版本的 source SHA-256、
編譯資訊、stdout、stderr、七個輸出檔及獨立 checker 紀錄。
v2 與新版本都有原先預設的五百萬次最少嘗試；v1 與 CodexGapSA 沒有同樣的
最低次數設定，因此三種早期基準的實際嘗試數不能假稱完全相同。

詳細結果見 `TEST_RESULTS.md`；原始紀錄在 `results/comparison/`。

## 程式來源與研究參考

- `src/placedb.*`、`rowmap.hpp`、`legalizer.*`、`annealer.*`、`main.cpp`、建置檔
  以本地 `ClaudeOpus55_v2` 為基底，保留原有註解。新增及修改部分如上所列。
- `src/optimizer.*`、`tests/`、本文與比較報告為本次新增。
- [ABCDPlace, TCAD 2020](https://yibolin.com/publications/papers/ABCDPLACE_TCAD2020_Lin.pdf)：
  提供 independent-set matching 與 local reordering 的理論與流程參考。本版是
  依題目撰寫的循序 CPU 實作，沒有複製 DREAMPlace 程式，也沒有聲稱 GPU 加速。
- [RippleLG, TCAD 2022](https://chengengjie.github.io/papers/J4-TCAD22-RippleLG.pdf)：
  參考同型 cell 交換原 slot 維持合法的思路。本版新增的是總位移預算限制下的
  瓶頸值搜尋，不把論文懲罰函式等同於此雙條件保證。
- 題目 `Lab3/Project03.pdf`、兩份本地 Bookshelf 說明與既有 `Grader/tools/check_pl.py`
  決定本題的輸入、幾何與指標規則。檢查器是儲存庫自製版本，不是官方評分器。

較詳細的原始碼閱讀與設計紀錄見 `READING_NOTES.md`。
