# Claude 對話紀錄

# 我

幫我閱讀所有檔案，並詳細解釋Lab2 每個小專案的演算法 時間由遠至近

---

# Claude

我正在逐一讀 Lab2 各專案的 README 和原始碼，接著會整理出各版演算法的說明。

---

# Claude

參考解的建構與放置已讀完，接著讀平衡（balance）、退火（optimizer）與 main 流程。

---

# Claude

已讀到 Ultracode_2 的平行分工；接著看 AstraUltra 的 FLUTE 式建樹和 Ultracode_3 的分群改動，然後開始寫說明。

---

# Claude

Lab2 共有 9 個專案資料夾，下面按時間由遠到近說明。時間依據是資料夾修改時間和 git log。這些專案的程式碼大致是兩條路線：

- **路線 A：基本版。** Morton 排序分群，加上 hill climbing（只隨機移動 buffer）。包括 `my_cbi`、`M11502102`、`GPT61Sol`。
- **路線 B：三階段版。** 建構、插繞路 buffer 平衡、模擬退火。從 `claude_reference` 開始，後面每一版都在前一版上改。

```
claude_reference ─► Ultracode ─► AstraMedium ─► Ultracode_2 ─► AstraUltra ─► Ultracode_3
    (10/8)          (10/12 04:54)  (10:07)        (12:16)         (12:57)        (15:06)
my_cbi 骨架 ─► M11502102（空骨架）   GPT61Sol（照 TUTORIAL 基本版寫）
```

---

## 0. 題目重點（所有版本都要處理）

- **輸入：** SRC 和 N 個 sink 的座標、buffer 型號庫（每型號有 fanout 上限 F、總線長上限 L、成本 C）、`w_skew`。
- **要做的事：** 插入 buffer，組成一棵以 SRC 為根的樹。
- **限制：** 每個 parent 的 child 數 ≤ F，且到所有 child 的 Manhattan 距離**總和** ≤ L。
- **到達時間：** sink 的 arrival = 路徑邊長總和，buffer 本身沒有延遲。
- **目標：** **Score = Σ buffer cost + w_skew × (T_max − T_min)**，越小越好。
- **關鍵觀念：** buffer 的位置和 children 決定之後，「最便宜的合法型號」就確定了，所以演算法只需要決定「樹的形狀」和「buffer 的座標」。

---

## 1. `my_cbi` 骨架（10/8）與 `M11502102`（10/12 02:25）

- 這兩個是同一份 229 行的骨架，`diff` 結果完全相同，`readProblem`、`evaluate`、`buildInitial`、`optimize` 都還是 TODO。
- [TUTORIAL.md](Lab2/TUTORIAL.md) 給的「基本版」演算法就是下一節 GPT61Sol 實際寫出來的內容。
- 骨架的 `main` 已經寫好一個保險：最佳化後如果不合法或分數變差，就退回初始解。

## 2. `claude_reference`（10/8，第一個完整解）

後面每一版都以它為基礎，所以這裡講最詳細。

### 資料結構（`tree.h/.cpp`）：增量計算 + transaction

- **每個節點記錄子樹統計：**
  - `lo` / `hi`：從自己往下到子樹內 sink 的最短 / 最長路徑。
  - `cnt`：子樹內的 sink 數。
  - `s1` / `s2`：latency 的總和與平方和，用來算標準差 σ。
- **整棵樹的值直接從根讀：** `T_max = nd[0].hi`、`T_min = nd[0].lo`。
- **只往上重算：** 修改一個節點後，`refreshUp` 沿祖先往上重算，統計不再改變就停，不用整棵 O(N) 重算。
- **型號自動選：** `recompute` 會挑「能撐住目前 fanout 與總線長」的最便宜型號，並同步更新 `totalCost`。
- **Transaction：** `begin()` 之後的所有修改都記進 journal，包括節點內容、佔用座標、free list。`rollback()` 完整還原，`commit()` 確定修改。所以任何 move 都可以「先試做 → 檢查 → 不行就還原」，全程保持合法。

### Stage 1 建構（`construct.cpp`）：由下往上分群

1. `frontier` 一開始是所有 sinks。
2. 如果 frontier 數量 ≤ SRC 的 F，且總長 ≤ SRC 的 L，就全部接到 SRC，結束。
3. 否則做**遞迴二分**（`partition`）：
   - 沿 bounding box 的長軸排序，切成兩半；
   - 一直切到每群 ≤ Fg 個，且在中位數放 buffer 時總長 ≤ 某型號的 L。
   - Fg 取 `C/(F−1)` 最小的型號的 F。原因是 N 個 sink 大約需要 N/(F−1) 個 buffer，這個比值越小越省。
4. 每群放一個 parent buffer（`placeGroup`），放不下就再切半。新 buffer 成為下一輪的 frontier。
5. 如果一群都合不起來（點彼此太遠），就在離 SRC 遠的節點上方插 **relay buffer**（`relayToward`），往 SRC 方向前進一步，步長取 `C/L` 最便宜型號的 L。
6. 收尾有兩種，兩種都做，最後取好的：
   - attach-early：能接 SRC 就直接接；
   - single-root：先合成單一根，讓 SRC 的子樹在根部就能平衡。

### Buffer 位置（`placement.cpp: findParentSpot`）

- **不是只取中位數。** 它在 children 的 bounding box 內先用 12×12 格點粗搜，保留最好的 3 個起點，再把步長減半，在 5×5 鄰域細搜。
- **目標函數：** `w·(max(d+hi) − min(d+lo)) + 型號 cost`，也就是讓各 child 的 latency 對齊。
- **只搜 bounding box 的理由：** 在 box 外移動時，到每個 child 的距離同步增加，range 不會變好。

### Stage 2 平衡（`balance.cpp`）：插繞路 buffer 拉高 T_min

- **兩個量：** 對節點 v，
  - `slack = T_max − (A_v + hi_v)`：整棵子樹最多還能延後多少；
  - `need = low − (A_v + lo_v)`：要讓子樹最早的 sink 達到目標 `low`，至少要延後多少。
- **找插入點：** 從上往下走，第一個 `need ≤ slack` 的節點，只要插**一個** delay buffer 就能解決整棵子樹。
- **找繞路點（`pointOnDiamond`）：** 繞路點 n 要滿足 `d(n,v)=a`、`d(p,n)=b`，也就是兩個 Manhattan 菱形的交點。
- **奇偶性：** 繞路長度一定是偶數，所以 **sink 的 arrival 奇偶性由 (x+y) 決定，永遠改不了**。sinks 的奇偶不一致時，skew ≥ 1。範例的 37 = 36 + 1，已經碰到這個下限。
- **一次插不夠：** 先插一個盡量長的，剩下的再串一串 buffer。
- **選 `low`：** 掃描 128 個均勻值，加上抽樣的 sink arrival，估計 `插入成本 + w·(T_max − low)`，取最小的方案。

### Stage 3 模擬退火（`optimizer.cpp`）

- **能量函數：** `E = cost + w·(skew + 0.3σ)`。skew 只看最大最小值，大部分 move 改不到它（能量是平的），所以加 σ 提供方向感。最佳解仍然以真正的 Score 記錄。
- **溫度尺度：** 以「最便宜 buffer 的 cost」為單位，指數降溫。
- **7 種 move：**

| Move | 機率 | 作用 |
|---|---|---|
| Shift | 40% | buffer 隨機移動，半徑 1、2、4…（30% 挑關鍵路徑上的） |
| Replace | 10% | 用 `findParentSpot` 移到最佳位置 |
| Reparent | 20% | 節點換到附近 buffer 底下；原 parent 變空就連鎖刪除，省 cost |
| Swap | 10% | 兩個節點交換 parent |
| Dissolve | 7% | 刪掉一個 buffer，children 分給附近的 buffer |
| Pad | 9% | 在太早到的節點上方插繞路 buffer |
| Split | 4% | 把一個 buffer 的部分 children 拉出來給新 buffer |

- **找附近 buffer：** 用空間格網，定期重建。
- **最後驗證：** 每個階段結束都用獨立的 `evaluate()` 從頭驗證，失敗就退回上一階段的解。

## 3. `M11502102_GPT61Sol`（10/12 03:32）：基本版

就是 TUTORIAL 的 Milestone 3 + 4，寫在單一檔案 [main.cpp](Lab2/M11502102_GPT61Sol/src/main.cpp)，共 998 行。

- **建構：**
  - frontier 用 **Morton（Z-order）key** 排序，也就是把 x、y 的 bit 交錯，讓空間上相鄰的點排在一起。
  - 從左到右貪心地加點，直到放不下 buffer。放 buffer 的方法是從中位數往外最多 8 圈找空位。
  - 一輪都合不起來，就插 relay 往 SRC 靠近。
- **最佳化：** 純 hill climbing。隨機挑一個 buffer、隨機移動半徑 1～32，**每次整棵樹重算**，合法且 Score 不變差才保留。
- **弱點**（第二次對話的比較結果）：
  - 不會改樹的結構；
  - 沒辦法讓太早到的 sink 變晚；
  - 整棵重算很慢；
  - 找空位時要掃過所有節點，10 萬個 sinks 要 55 秒；
  - 建樹卡住時直接 `exit(1)`，不寫輸出檔。
- **分數：** 平均是 claude_reference 的 1.67～2.11 倍。

## 4. `M11502102_ClaudeOpus55Ultracode`（04:54）

以 claude_reference 為基礎，分析後發現 buffer cost 佔 Score 的 90～99%，而且搜尋速度是瓶頸。

- **4 種新 move，加上 Reparent 新增 relocate 版本，共 11 種：**
  - **Merge：** 兩個鄰近 buffer 合成一個，或把 buffer child 收進 parent，直接省一個 buffer。先用「中位數總線長 > 最大 L」快速排除不可能的組合。
  - **Rebalance：** 兩個鄰近 buffer 的 children 沿兩者的連線排序，枚舉切點重新分成兩群，再各自重新放置。
  - **Retarget：** 關鍵路徑上的 buffer 以「絕對目標 T_max」重新放置。超過目標的懲罰是不足的 2 倍。
  - **SkewRepar：** 關鍵路徑上的節點換到「arrival 最合適」的鄰近 buffer 底下，不加 buffer 就能平衡 skew。
  - **Reparent 的 relocate 版：** 換完 parent 後，新舊 parent 都重新放置。
- **速度約 4 倍**（每秒 70～90 萬個 move）：
  - 佔用表從 hash set 改成 bitmap；
  - `searchSpot` 改成 template，讓評估函式 inline；
  - 最佳化時用便宜的 `fastSearch`（只保留 1 個起點、3×3 鄰域）。
- **時間上限：** 改成 `10 + N/100` 秒，最多 30 秒。
- **效果：** 比第一版好約 2.7%，約是 GPT61Sol 的 0.46～0.58 倍。

## 5. `M11502102_Chatgpt6AstraMedium`（10:07）

以 Ultracode 為基礎，只改約 200 行。

1. **全樹精確的 Retarget：**
   - 沿祖先收集兄弟子樹的最早 / 最晚 arrival，得到「不受這個 buffer 移動影響」的範圍。
   - 每個候選座標只要 O(fanout) 就能算出**全樹**的 skew 和 σ，成本也計入 parent 型號的變化。
   - 這種評估只佔 Retarget 的 25%，其餘 75% 用原版方法。
2. **`diamond.h`：** 用解析解求兩個 Manhattan 菱形的交點，取代逐點掃描。複雜度與半徑無關，選出的點和原版相同，所以只加速、不改變分數。
3. **立即保存最佳解：** 每次 Score 創新低就存一次。原版是每隔一段操作才存。

結果和 Ultracode 打平（1.0001）。

## 6. `M11502102_ClaudeOpus55Ultracode_2`（12:16）：平行化

評分 VM 是 2 核心，但前面的版本都只用 1 核心。

- **分工模式**（N ≥ 1500 且最大 L ≤ 0.65 × 晶片邊長時啟用，`parallel.cpp`）：
  1. `makeCut`：從 SRC 往下，反覆展開 sink 最多的節點，直到每棵子樹 ≤ N/3 個 sink。上層節點凍結。
  2. 剩下的子樹根沿 x 或 y 排序，按 sink 數分成兩組，兩條執行緒各負責一組。
  3. 每個 move 用 transaction journal 檢查有沒有碰到別組的節點，越界就 rollback。
  4. `mergeCut`：每個 epoch 結束把兩半合併，新 buffer 重新編號，再整棵重新驗證。
  5. 奇數 epoch 和最後 30% 的時間，兩條鏈都看整棵樹，因為上層凍結時 skew 修不好。
- **其他測資：** 兩條鏈用不同 seed 從同一個解出發，每個 epoch 取較好的那條繼續。
- **降溫不中斷：** 同步之後接著原本的溫度繼續降，不重新加熱。重新加熱的版本實驗上完全沒有幫助。
- **效果：** 整體好 0.6%，大且線長短的測資好 1.4%（8 筆全勝）。

## 7. `M11502102_Chatgpt6AstraUltra`（12:57）

以 Ultracode_2 為基礎。

1. **保存所有創新低的候選：** 只要合法候選的 Score 創新低就存，即使退火因為 σ 拒絕了它。用 sparse checkpoint，只複製有變動的節點。
2. **合併後保留三個候選：** chain 0、chain 1、合併解分別驗證，取最好的。
3. **FLUTE 啟發的分群**（`construct_spatial.cpp`）：
   - 試 x、y 兩個方向和多個切點，用「每群平均最小總線長」篩選；
   - 底層最多試 6 個候選，實際建出兩層子樹，算成本與 skew 後 rollback，挑最好的。
   - 它沒有套用 FLUTE 的查表或 RSMT 最佳解，原因是本題不共用線段，而且每個分支點都是要付錢的 buffer。
4. **精確座標搜尋**（`exact_placement.cpp`）：已實作並測試過，但沒有得分收益，預設關閉。

結果約和 Ultracode_2 打平（0.998）。

## 8. `M11502102_ClaudeOpus55Ultracode_3`（15:06，目前最強）

以 Ultracode_2 為基礎，逐項測過 AstraUltra 和 FLUTE 的想法後：

1. **多方向、多切點分群**（借用 FLUTE net breaking 的「試多種切法、取最好」）：
   - 每一刀試 4 個方向：x、y、x+y、x−y。對角線方向在 Manhattan 距離下也重要。
   - 每個方向試 3 個切點：平均切點，以及前後各移一群。
   - 最後一刀直接比兩群的實際型號成本；上層則比兩半的外框半周長總和，越小越緊密。
   - 起點的 Score 好 2～3%。
2. **修正「重新放置一定會移動」的問題：** 以前 buffer 自己的座標被當成已佔用，所以即使原地就是最佳也會被移走。現在只在新位置更好時才移動，並用 children 的指紋快取上次的結果。每秒 move 數約多 18%。
3. **採用 AstraUltra 的三候選合併。**
4. **大測資（N ≥ 1500）的時間上限從 30 秒放寬到 45 秒。**

測過但沒採用的：Hanan grid 起點、AstraUltra 的 FLUTE 式建樹、保存被拒絕的最佳候選、大鄰域重建（ruin-and-rebuild）。

結果：111 筆測資上，比 Ultracode_2 好 0.37%，比 AstraUltra 好 0.2%。10 萬個 sinks 的 Score 是 294863。

## 9. `my_cbi` 目前狀態（最新）

- HEAD（commit a523927）裡的 `my_cbi/src/main.cpp` 和 GPT61Sol **完全相同**。
- 工作目錄裡它已被改回空骨架，還沒 commit。看起來你正準備自己從頭寫。

---

## 演進重點

| 版本 | 主要貢獻 | 相對前一版 |
|---|---|---|
| GPT61Sol | Morton 分群 + hill climbing | 基準（最弱） |
| claude_reference | 增量樹 + transaction、bounding box 位置搜尋、繞路平衡、7 種 move 的退火 | 約好 40～50% |
| Ultracode | 4 種新 move + 搜尋快 4 倍 | 約好 2.7% |
| AstraMedium | 全樹精確 Retarget、解析菱形交點 | 持平 |
| Ultracode_2 | 2 執行緒分工 / 雙鏈 | 約好 0.6% |
| AstraUltra | 保存創新低候選、FLUTE 式分群 | 持平 |
| Ultracode_3 | 4 方向 × 3 切點分群、修正重新放置、大測資 45 秒 | 約好 0.4% |

結論：真正的大躍進只有兩次。一次是 GPT61Sol → claude_reference，原因是演算法本身變了（可以改結構、可以插繞路）；另一次是 Ultracode 的新 move 加上提速。之後各版的差距都在 1% 以內，小、中型測資基本上已經收斂。

**兩件要注意的事：**
- **Ultracode_3 的 README 過期了。** [M11502102_ClaudeOpus55Ultracode_3/README.md](Lab2/M11502102_ClaudeOpus55Ultracode_3/README.md) 在 commit a523927 被換成 Ultracode_2 的內容，寫的是 30 秒上限和 Ultracode_2 的數據。但程式碼（[main.cpp:129](Lab2/M11502102_ClaudeOpus55Ultracode_3/src/main.cpp:129)）實際用的是 45 秒。正確的版本在 commit bec91b1，要交這版的話記得還原。
- **報告可用的觀察：** 奇偶性下限（skew ≥ 1）、buffer cost 佔 Score 90% 以上，都適合寫進報告。

---

# 我

進入 Conda 環境 指令

---

# Claude

要進入這次建的 `eda` 環境，執行：

```bash
conda activate eda
```

離開環境：

```bash
conda deactivate
```

如果出現 `conda: command not found`，或提示要先執行 `conda init`，先跑這行（只需要做一次），然後重開 terminal：

```bash
conda init bash
```

`eda` 環境裡有 Python 3.12、cmake 3.31 和 matplotlib。評分機上不需要這個環境，只有本機編譯和跑 `tools/` 裡的 Python 工具時才要啟用。

---

# 我

Ultracode_3 的 README 過期了。 M11502102_ClaudeOpus55Ultracode_3/README.md 在 commit a523927 被換成 Ultracode_2 的內容，寫的是 30 秒上限和 Ultracode_2 的數據。但程式碼（main.cpp:129）實際用的是 45 秒。正確的版本在 commit bec91b1，要交這版的話記得還原。

幫我改正確

---

# Claude

我已經從 commit bec91b1 把 [M11502102_ClaudeOpus55Ultracode_3/README.md](Lab2/M11502102_ClaudeOpus55Ultracode_3/README.md) 還原成正確版本，現在和程式碼一致：

- **時間上限：** N ≥ 1500 時最多 45 秒，和 [main.cpp:129](Lab2/M11502102_ClaudeOpus55Ultracode_3/src/main.cpp:129) 相同。
- **改進說明：** 寫的是相對 Ultracode_2 的四項改進：多方向切分、修正「重新放置一定會移動」、三候選合併、45 秒上限。
- **其他內容：** 也包含 Ultracode_3 自己的實驗數據、FLUTE 的比較表，以及環境變數 `CBI_PART`。

這個修改已經 stage，但還沒 commit。要提交的話：

```bash
git commit -m "Restore Ultracode_3 README" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

工作目錄裡另外還有你原本未 commit 的修改：`.gitignore`、`conversation.md`、`my_cbi`。這些我都沒動，上面的指令也不會把它們一起提交。

---

# 我

好現在請先解釋 數據會如何輸入 我想從 Main 開始寫

---

# Claude

資料的路徑是：**命令列參數 → `main` 拿到檔名 → `readProblem` 解析文字檔 → 存進 `Problem` → `makeTree` 轉成節點陣列**。以下照這個順序說明，對照的是你的骨架 [my_cbi/src/main.cpp](Lab2/my_cbi/src/main.cpp)。

---

## 1. 程式怎麼被呼叫

評分機會這樣執行：

```bash
./build/cbi inputs/input001.cbi results/output001.cbi
```

`main(int argc, char** argv)` 收到的參數：

| 變數 | 內容 |
|---|---|
| `argc` | 3 |
| `argv[0]` | `./build/cbi`（程式本身） |
| `argv[1]` | 輸入檔路徑 |
| `argv[2]` | 輸出檔路徑 |

所以 `main` 第一件事是檢查參數，這是題目規定：

```cpp
if (argc < 3) {
    std::fprintf(stderr, "error: missing arguments\nusage: %s INPUT_FILE OUTPUT_FILE\n", argv[0]);
    return 1;
}
```

接著把 `argv[1]` 交給 `readProblem(argv[1], P)`。路徑直接用 `std::ifstream` 開，相對路徑會自動以目前目錄為基準，不需要特別處理。

---

## 2. 輸入檔長什麼樣子

以 [testcases/input001.cbi](Lab2/testcases/input001.cbi) 為例：

```
.limit
fanout 2		# maximum fanouts for SRC
length 100	# maximum length for SRC
.dimx 110	# chip dimension in x-axis
.dimy 100	# chip dimension in y-axis
.e

.buflib 3
# type	#F	#L	#C #fanout/length/cost
SMALL	3	90	10
MEDIUM	6	120	16
LARGE	9	190	22
.e

.objective
w_skew 1
.e

.pin 6		# number of pins
97 38		# clock SRC coordinates
49 62		# clock sink S1 coordinates
...
.e
```

檔案分成 4 個區塊，每個區塊以 `.xxx` 開頭、以 `.e` 結束：

| 區塊 | 每行內容 | 存到 `Problem` 的哪裡 |
|---|---|---|
| `.limit` | `fanout 2`、`length 100`、`.dimx 110`、`.dimy 100` | `srcF`、`srcL`、`dimx`、`dimy` |
| `.buflib 3` | `名稱 F L C` | `lib`（每行一個 `BufType`） |
| `.objective` | `w_skew 1` | `wSkew` |
| `.pin 6` | `x y`，**第 1 行是 SRC**，之後依序是 S1、S2… | `srcX/srcY`，其餘 push 進 `sinkX/sinkY` |

**要注意的坑：**
- **註解：** `#` 之後全部要丟掉，而且有一整行都是註解的情況（`.buflib` 的第一行）。
- **分隔字元：** 欄位之間是 **tab** 而不是空白，有些行還有好幾個 tab。用 `istringstream >>` 讀就會自動跳過所有空白和 tab。
- **`.dimx` / `.dimy` 的位置：** 它們寫在 `.limit` 區塊**裡面**，而且開頭有點。
- **最後一行：** 檔尾的 `.e` 後面沒有換行，`getline` 照樣讀得到，不用特別處理。
- **`.pin 6` 的 6：** 包含 SRC，所以 sink 數 = 6 − 1 = 5。也可以不信這個數字，直接數讀到幾行座標。
- **SRC 可以在邊界上：** 例如 input011 的 SRC 是 `(2077, 0)`。「必須在晶片內部」的限制只針對 buffer。

---

## 3. `readProblem` 的解析流程

做法是「一行一行讀 + 記住目前在哪個區塊」：

```
section = ""                       // 目前區塊
for 每一行 line:
    砍掉 '#' 之後的字
    用 istringstream 切出第一個 token
    如果是空行 → 跳過
    如果 token 是 ".limit" / ".buflib" / ".objective" / ".pin" → section = token，continue
    如果 token 是 ".e" → section = ""，continue
    依 section 處理這一行：
        .limit     : token 是 fanout / length / .dimx / .dimy → 讀下一個數字存起來
        .buflib    : token 是型號名稱 → 再讀 F L C，push 進 lib
        .objective : token 是 w_skew → 讀數字
        .pin       : token 就是 x → 再讀 y；第一筆給 SRC，之後給 sinks
```

最關鍵的兩個工具：

```cpp
std::string line;
while (std::getline(in, line)) {
    line = line.substr(0, line.find('#'));   // find 找不到會回傳 npos，substr 就保留整行
    std::istringstream ss(line);
    std::string tok;
    if (!(ss >> tok)) continue;              // 空行
    // ... 依 tok 和 section 處理
}
```

最後 `return true;`，讓 `main` 知道讀檔成功。

**檢查點：** 讀完後在 `main` 暫時印出 `P.lib.size()`、`P.sinkX.size()`、`P.wSkew`。範例應該是 `3 5 1`，`testcases/input011.cbi` 應該有 10000 個 sinks。

---

## 4. 讀完之後：`makeTree` 把資料變成節點陣列

`Problem` 存的是「題目」，`Tree` 存的是「解」。骨架已經寫好 `makeTree`，它定的**節點編號規則**是後面所有程式的基礎：

| 編號 | 節點 |
|---|---|
| `0` | SRC |
| `1 .. N` | sink S1 .. SN（注意 `P.sinkX[0]` 是 S1，所以要差 1） |
| `N+1 ..` | 你之後用 `addBuffer` 新增的 buffer |

這樣輸出時，sink 的名字直接用 `"S" + 編號`；buffer 則要依 BFS 順序重新編號成 B1、B2…。

---

## 5. 整個 `main` 的骨架流程（之後要寫的部分）

```
main
 ├─ 檢查 argc
 ├─ readProblem(argv[1], P)        ← Milestone 1（現在要寫的）
 ├─ T = makeTree(P)                ← 已寫好
 ├─ buildInitial(P, T)             ← Milestone 3：先求合法
 ├─ evaluate(P, T)                 ← Milestone 2：檢查 + 算分
 ├─ optimize(P, T, 秒數)           ← Milestone 4：降低 Score
 ├─ writeOutput(P, T, argv[2])     ← Milestone 2
 └─ printf("T_max: %lld, T_min: %lld, Score: %lld\n", ...)
```

建議先把 `readProblem` 寫完，用上面的檢查點確認數字正確，再往 Milestone 2 走。寫好可以貼給我看。

---

# 我

請看我 的 M11502102 目前甚麼都沒有 請你把我當成是小白教我 C++ 跟這些演算法

---

# Claude

好，我們從零開始，一課一課來。每課都是「先講觀念 → 給你一段可以直接打的程式碼 → 你自己跑 → 做一個小練習」。你寫完練習貼給我看，我們再進下一課。

先說兩件事：

- **我不會動你的 `main.cpp`。** 你的 `src/` 裡有 `.main.cpp.swp`，代表你正用 vim 開著它。我如果同時改這個檔案會跟你的編輯衝突，所以程式碼都由你自己打。親手打一遍也比較學得會。
- **時間：** 截止是 10/14 23:59，還有兩天多。我建議這樣分：
  - 今天：第 1～2 課，讀檔和計分；
  - 明天：第 3～4 課，建樹和最佳化；
  - 後天：報告。

## 課程地圖

| 課 | C++ 會學到 | 演算法 / 題目 |
|---|---|---|
| **1（今天）** | 編譯執行、`main` 參數、`struct`、`vector`、`string`、讀檔 | 把輸入檔讀進程式 |
| 2 | 函式、迴圈、BFS 用的 queue | 計算 arrival、skew、Score，判斷合法，寫輸出檔 |
| 3 | 排序、lambda | 由下往上分群，建出第一棵合法的樹 |
| 4 | 亂數、計時 | Hill climbing：隨機移動 buffer，讓分數變低 |
| 5 | （進階） | 繞路 buffer、奇偶性下限、模擬退火 |

---

# 第 1 課：讓程式跑起來，並讀懂輸入檔

## 1-1. C++ 程式怎麼變成可以執行的東西

C++ 寫的是**原始碼**（`main.cpp`），電腦看不懂。要先經過**編譯**，變成**執行檔**（`cbi`），才能執行。

你的資料夾已經有 `CMakeLists.txt`，它告訴 cmake：「把 `src/` 裡所有 `.cpp` 編譯成名叫 `cbi` 的執行檔」。你只要記兩個指令，在 `M11502102/` 資料夾裡執行。

第一次要先產生建置設定（新增 `.cpp` 檔之後也要重跑）：

```bash
cmake -S . -B build
```

每次改完程式就跑這個來編譯：

```bash
cmake --build build
```

編譯成功後，執行檔在 `build/cbi`。記得先 `conda activate eda`，因為 cmake 裝在那個環境裡。

## 1-2. `main`：程式的起點

你現在的 `main.cpp` 只有：

```cpp
int main() {
    return 0;
}
```

- `int main()` 是程式開始的地方。每個 C++ 程式剛好有一個 `main`。
- `return 0;` 代表「程式正常結束」，回傳 1 之類的非 0 值代表「出錯了」。

可是我們的程式要這樣被呼叫：`./build/cbi 輸入檔 輸出檔`。要拿到後面那兩個檔名，`main` 要改寫成：

```cpp
int main(int argc, char** argv)
```

- `argc` 是參數的**個數**，包含程式名稱本身。
- `argv` 是參數的**內容**。`argv[0]` 是程式名稱，`argv[1]` 是輸入檔，`argv[2]` 是輸出檔。

所以執行 `./build/cbi a.cbi b.cbi` 時，`argc` 是 3。題目規定參數不夠要印錯誤訊息，也就是 `argc < 3` 時要報錯。

## 1-3. `struct`：把相關的資料綁在一起

一個 buffer 型號有 4 個屬性：名稱、F、L、C。如果用 4 個獨立的變數存，很快就會亂掉。`struct` 讓你自己定義一種「資料型態」：

```cpp
struct BufType {
    std::string name;   // 名稱，例如 "SMALL"
    int F = 0;          // fanout 上限
    long long L = 0;    // 總線長上限
    long long C = 0;    // 成本
};
```

之後就可以 `BufType b; b.name = "SMALL"; b.F = 3;`，用 `.` 取出裡面的欄位。

**幾種型態：**
- `int`：整數，大約到 ±21 億。
- `long long`：更大的整數。線長加總起來可能很大，用它比較安全。
- `std::string`：一串文字。
- `std::vector<X>`：**可以變長的陣列**，裡面裝很多個 X。
  - `v.push_back(a)`：把 a 加到尾端。
  - `v.size()`：目前有幾個。
  - `v[i]`：取第 i 個，**從 0 開始數**。

整個題目的資料可以裝進一個 `struct Problem`，下面的程式碼裡有。

## 1-4. 讀檔的三個工具

| 工具 | 用途 |
|---|---|
| `std::ifstream in(path);` | 打開檔案，`if (!in)` 代表打不開 |
| `std::getline(in, line)` | 讀一整行到 `line`，讀到檔尾就回傳 false，所以可以放進 `while` |
| `std::istringstream ss(line); ss >> tok;` | 把一行字「一個字一個字」切開，空白和 tab 都會自動跳過 |

還有一個技巧：`line.substr(0, line.find('#'))` 會把 `#` 之後的註解砍掉。

## 1-5. 動手：打下這段程式

把你的 `src/main.cpp` 整個換成下面這段。我已經在別的地方實際編譯、執行過，確定能跑：

```cpp
#include <fstream>    // ifstream：讀檔
#include <iostream>   // cout / cerr：印東西到螢幕
#include <sstream>    // istringstream：把一行切成好幾個字
#include <string>
#include <vector>

struct BufType {
    std::string name;
    int F = 0;          // fanout 上限
    long long L = 0;    // 總線長上限
    long long C = 0;    // 成本
};

struct Problem {
    int srcF = 0;            // SRC 的 fanout 上限
    long long srcL = 0;      // SRC 的總線長上限
    int dimx = 0, dimy = 0;  // 晶片大小
    std::vector<BufType> lib;          // 所有 buffer 型號
    long long wSkew = 1;
    int srcX = 0, srcY = 0;            // SRC 座標
    std::vector<int> sinkX, sinkY;     // sinks 座標（S1 放在第 0 格）
};

bool readProblem(const std::string& path, Problem& P) {
    std::ifstream in(path);
    if (!in) return false;            // 檔案打不開

    std::string line;
    std::string section;              // 現在在哪個區塊，例如 ".limit"
    while (std::getline(in, line)) {  // 一次讀一行，讀到檔尾就停
        line = line.substr(0, line.find('#'));   // 砍掉註解
        std::istringstream ss(line);
        std::string tok;
        if (!(ss >> tok)) continue;   // 讀不到任何字 = 空行，跳過

        std::cout << "[" << section << "] 第一個字 = " << tok << "\n";   // 暫時印出來觀察

        if (tok == ".e") { section = ""; continue; }   // 區塊結束
        if (tok == ".limit" || tok == ".buflib" || tok == ".objective" || tok == ".pin") {
            section = tok;            // 進入新區塊
            continue;
        }
        // TODO：依照 section 把數字存進 P（這是你的練習）
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "error: missing arguments\n";
        std::cerr << "usage: " << argv[0] << " INPUT_FILE OUTPUT_FILE\n";
        return 1;
    }
    Problem P;
    if (!readProblem(argv[1], P)) {
        std::cerr << "error: cannot read " << argv[1] << "\n";
        return 1;
    }
    return 0;
}
```

存檔之後編譯：

```bash
cmake -S M11502102 -B M11502102/build && cmake --build M11502102/build
```

編譯時會出現一個 `unused parameter 'P'` 的警告。這是正常的，因為我們還沒用到 P，做完練習它就會消失。

接著執行看看（在 `Lab2/` 資料夾裡跑）：

```bash
M11502102/build/cbi testcases/input001.cbi out.cbi
```

你應該會看到：

```
[] 第一個字 = .limit
[.limit] 第一個字 = fanout
[.limit] 第一個字 = length
...
[.pin] 第一個字 = 97
[.pin] 第一個字 = 49
```

這就是 parser 的核心想法：**每一行都知道「自己在哪個區塊」和「第一個字是什麼」**，接下來只要決定怎麼存。

也試一下不給參數，確認錯誤訊息有印出來：

```bash
M11502102/build/cbi
```

## 1-6. 你的練習

把 `// TODO` 那一行換成真正的處理，讓數字存進 `P`。提示：`ss >> 變數` 會從同一行繼續讀下一個字，並且自動轉成數字。

1. **`.limit` 區塊：**
   - 如果 `tok == "fanout"`，就 `ss >> P.srcF;`
   - `length`、`.dimx`、`.dimy` 也照樣處理。
2. **`.buflib` 區塊：** `tok` 就是型號名稱。建一個 `BufType b; b.name = tok;`，再 `ss >> b.F >> b.L >> b.C;`，最後 `P.lib.push_back(b);`
3. **`.objective` 區塊：** `tok == "w_skew"` 時讀進 `P.wSkew`。
4. **`.pin` 區塊**（最需要動腦）：
   - `tok` 是 x 座標，但它是字串，要用 `std::stoi(tok)` 轉成整數，y 則再 `ss >>` 一次讀出來。
   - **第一筆是 SRC，之後才是 sinks。** 怎麼知道是不是第一筆？可以宣告一個變數記錄「讀過幾筆座標了」。

**檢查點：** 做完後，把 `cout` 那行觀察用的輸出刪掉，在 `main` 讀檔成功之後加：

```cpp
std::cout << P.lib.size() << " " << P.sinkX.size() << " " << P.wSkew << "\n";
```

- 範例 `testcases/input001.cbi` 要印出 `3 5 1`。
- `testcases/input011.cbi` 的第二個數字要是 `10000`。

卡住就把你的程式碼或錯誤訊息貼上來，不用怕問太基本的問題。

---

# 我

請查看 M11502102 資料夾看我哪裡寫錯 並 寫出 Problem 在幹嘛

---

# Claude

我實際編譯了你的 [main.cpp](Lab2/M11502102/src/main.cpp)，共有 **4 個錯誤**。另外還有一個不算錯、但最好改的地方。下面一個一個講。

## 你的程式哪裡錯

### 錯誤 1：用了 `std::string` 卻沒有 `#include <string>`（第 5 行）

```
error: 'string' in namespace 'std' does not name a type
```

C++ 的工具都放在不同的「標頭檔」裡，要用之前得先 `#include` 進來。這就像在說：「我要用文字功能，請幫我把它載入」。

**改法：** 在最上面加 `#include <string>`。

### 錯誤 2：`struct` 結尾少了分號（第 9 行）

```
error: expected ';' after struct definition
```

這是 C++ 新手最常犯的錯。**`struct` 的大括號後面一定要加 `;`**，函式的大括號後面則不用：

```cpp
struct Bufferlibrary {
    ...
};   // ← struct 要分號

int main() {
    ...
}    // ← 函式不用
```

### 錯誤 3：`std::print` 不能用（第 2、13 行）

```
error: 'print' is not a member of 'std'
note: 'std::print' is only available from C++23 onwards
```

`std::print` 是 C++23 才有的新功能，但你的 `CMakeLists.txt` 設定的是 C++17，所以找不到它。

**而且就算改成 C++23 也不能用。** 評分機是 Ubuntu 24.04，內建 GCC 13，`<print>` 要到 GCC 14 才有。在評分機上會**編譯失敗，整份作業 0 分**。

**改法：** 用 `<iostream>` 的 `std::cerr`。錯誤訊息印到 `cerr`，正常輸出印到 `cout`：

```cpp
#include <iostream>
...
std::cerr << "error: missing arguments\n";
```

`<<` 可以想成「把右邊的東西送進左邊」，可以一直接下去：`std::cerr << "usage: " << argv[0] << "\n";`

### 錯誤 4：`a` 沒有定義（第 13 行）

```
error: 'a' was not declared in this scope
```

C++ 的變數一定要**先宣告才能用**，程式裡從沒出現過叫 `a` 的東西。如果你想印一段文字，要用雙引號括起來：`"error: missing arguments\n"`。沒有引號的話，C++ 會把它當成變數名稱去找。

### 建議修改（不是錯誤）

- **`main` 的參數名稱：** `main(int file_num, char** file_name)` 可以編譯，名字確實可以自己取，但是取得不太準確：
  - `file_num` 是「參數總數」，**包含程式名稱本身**，不是檔案數；
  - `file_name[0]` 是程式名稱，不是檔名。

  大家慣用 `argc` / `argv`（argument count / argument vector），建議照用，別人看你的程式也比較好懂。
- **`int length` / `int cost`：** 可以用，但線長總和在大測資可能很大，建議跟 `Problem` 一致都用 `long long`，之後做加總時比較不會溢位。

## 修好的版本

你的命名可以保留，我只改錯誤的地方：

```cpp
#include <iostream>
#include <string>
#include <vector>

struct Bufferlibrary {
    std::string type;
    int fanout = 0;
    long long length = 0;
    long long cost = 0;
};   // ← 分號

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "error: missing arguments\n";
        std::cerr << "usage: " << argv[0] << " INPUT_FILE OUTPUT_FILE\n";
        return 1;
    }
    return 0;
}
```

---

## `Problem` 是在做什麼

### 一句話

**`Problem` 是一個「箱子」，把輸入檔裡所有的資料裝在一起。** 讀檔時把東西放進去，之後所有函式都從這個箱子拿資料。

### 為什麼需要它

輸入檔裡有十幾種資料：SRC 限制、晶片大小、型號表、w_skew、所有座標。如果每一種都是獨立的變數，之後每個函式都要傳十幾個參數，例如 `evaluate(srcF, srcL, dimx, dimy, lib, wSkew, ...)`，很容易傳錯。

把它們裝進一個 `Problem` 之後，只要傳一個：`evaluate(P, ...)`。

### 箱子裡的每一格，對應到輸入檔的哪裡

```cpp
struct Problem {
    // ---- 來自 .limit 區塊 ----
    int srcF = 0;            // fanout 2     → SRC 最多接幾個 child
    long long srcL = 0;      // length 100   → SRC 到所有 child 的距離總和上限
    int dimx = 0, dimy = 0;  // .dimx 110 / .dimy 100 → 晶片大小，buffer 必須放在裡面

    // ---- 來自 .buflib 區塊 ----
    std::vector<Bufferlibrary> lib;   // 每一行型號（SMALL / MEDIUM / LARGE）存成一個 Bufferlibrary

    // ---- 來自 .objective 區塊 ----
    long long wSkew = 1;     // w_skew 1 → Score = 成本 + wSkew × skew

    // ---- 來自 .pin 區塊 ----
    int srcX = 0, srcY = 0;           // 第一行座標 97 38 = SRC
    std::vector<int> sinkX, sinkY;    // 之後每一行 = S1, S2, ...
};
```

讀完範例 `input001.cbi` 之後，箱子裡是：

| 欄位 | 值 |
|---|---|
| `srcF`, `srcL` | 2, 100 |
| `dimx`, `dimy` | 110, 100 |
| `lib` | `[{SMALL,3,90,10}, {MEDIUM,6,120,16}, {LARGE,9,190,22}]`，`lib.size()` = 3 |
| `wSkew` | 1 |
| `srcX`, `srcY` | 97, 38 |
| `sinkX` | `[49, 17, 56, 72, 9]` |
| `sinkY` | `[62, 25, 82, 27, 91]` |

**注意：** S1 存在 `sinkX[0]`，因為 vector 從 0 開始數。所以 S3 的座標是 `(sinkX[2], sinkY[2])`。

### 兩個 struct 的關係

```
Problem（整份題目）
 ├─ srcF, srcL, dimx, dimy, wSkew, srcX, srcY   ← 一個值
 ├─ sinkX, sinkY                                ← 很多個 int
 └─ lib                                         ← 很多個 Bufferlibrary
      ├─ Bufferlibrary {type="SMALL",  fanout=3, length=90,  cost=10}
      ├─ Bufferlibrary {type="MEDIUM", fanout=6, length=120, cost=16}
      └─ Bufferlibrary {type="LARGE",  fanout=9, length=190, cost=22}
```

也就是說，**struct 裡面可以再裝 struct 的 vector**，這樣就能描述有層次的資料。

### 怎麼用

在 `main` 裡宣告一個空箱子，交給讀檔函式填：

```cpp
Problem P;                       // 空箱子，裡面全是預設值 0
readProblem(argv[1], P);         // 讀檔，把東西放進去
std::cout << P.lib.size();       // 之後用 P.欄位 取出來
```

`readProblem` 的參數要寫成 `Problem& P`，**`&` 很重要**。有 `&` 的話，函式改的是你在 `main` 裡那個箱子本身。沒有 `&` 的話，函式拿到的是一份**複製品**，它在裡面填得再滿，函式結束複製品就丟掉了，`main` 裡的 `P` 還是空的。

---

## 下一步

1. 照上面修好錯誤，把 `struct Problem` 加到 `Bufferlibrary` 下面（欄位名稱可以自己取）。
2. 編譯，確認沒有 error：

```bash
cmake --build build
```

3. 接著加上第 1 課的 `readProblem`，做 1-6 的練習。

**提醒：** 你的 vim 還開著這個檔案，`src/` 裡有 `.main.cpp.swp`，所以我沒有幫你改檔案。改完存檔後再叫我看就好。

---

# 我

請你幫我看 M11502102 且我 CMake 已經刪除了

---

# Claude

`CMakeLists.txt` 已經補回原本的內容，設定可以正常產生。`Problem` 這次寫得很好：每個欄位都有對到輸入檔，註解也清楚。只剩下一些小錯誤要修。

## 一、`CMakeLists.txt` 為什麼一定要有

評分機只會執行這兩行：

```bash
cmake -S . -B build
cmake --build build
```

**沒有 `CMakeLists.txt`（或檔案是空的），第一行就失敗，整份作業編譯不出來，所有測資都 0 分。** 交作業的 zip 裡也規定一定要有它。

逐行解釋：

| 這一行 | 意思 |
|---|---|
| `cmake_minimum_required(VERSION 3.10)` | 至少要 cmake 3.10 才能處理這個檔案 |
| `project(cbi CXX)` | 專案叫 cbi，使用 C++（CXX 就是 C++） |
| `set(CMAKE_CXX_STANDARD 17)` | 用 C++17 標準，所以 `std::print`（C++23）不能用 |
| `if(NOT CMAKE_BUILD_TYPE) ... Release` | 沒指定模式時自動用 Release，開啟 `-O3` 最佳化，程式快 5～10 倍 |
| `file(GLOB SOURCES .../src/*.cpp)` | 自動抓 `src/` 裡所有 `.cpp` |
| `add_executable(cbi ${SOURCES})` | 把它們編譯成叫 `cbi` 的執行檔，題目規定這個名字 |
| `target_compile_options(cbi PRIVATE -Wall -Wextra)` | 打開警告，讓編譯器幫你抓可疑的寫法 |

你可以把它想成「給編譯器的說明書」，寫好一次之後就不用再動。

## 二、`main.cpp` 的 5 個錯誤

我實際編譯過。第 1 個錯誤會讓編譯器直接停下來，所以修好第 1 個之後才會看到後面 4 個。

### 錯誤 1：`<sting>` 拼錯（第 2 行）

```
fatal error: sting: No such file or directory
```

少了一個 `r`，要寫 `#include <string>`。編譯器找不到叫 `sting` 的標頭檔，所以直接停止。

### 錯誤 2：用了 `std::vector` 但沒有 include（第 19、25 行）

```
error: 'vector' in namespace 'std' does not name a template type
```

和上次 `string` 的錯誤是同一種：**用什麼就要 include 什麼**。加上 `#include <vector>`。

對照表，之後會常用到：

| 你用了 | 要 include |
|---|---|
| `std::string` | `<string>` |
| `std::vector` | `<vector>` |
| `std::cout`, `std::cerr` | `<iostream>` |
| `std::ifstream`（讀檔） | `<fstream>` |
| `std::istringstream`（切字） | `<sstream>` |

### 錯誤 3：`argument_count` 不存在（第 31 行）

```
error: 'argument_count' was not declared in this scope
```

你在第 29 行把參數取名為 `argc`，第 30 行的註解寫了它的全名，但第 31 行用的是全名。**註解只是給人看的，編譯器完全忽略它**，所以 `argument_count` 這個名字在程式裡根本不存在。

**改法：** `if (argc < 3)`。

### 錯誤 4：`readProblem` 還沒寫（第 36 行）

```
error: 'readProblem' was not declared in this scope
```

C++ 的函式要**先定義（或宣告），才能在後面呼叫**。編譯器是從上往下讀的，讀到第 36 行時，它還沒看過任何叫 `readProblem` 的東西。

**改法：** 在 `struct Problem` 之後、`main` 之前寫這個函式。下面第三節有一個可以先通過編譯的空殼。

### 錯誤 5：`P.lib` 不存在（第 37 行）

```
error: 'struct Problem' has no member named 'lib'
```

你在 `Problem` 裡把型號表取名為 `buflib`，這是你自己取的好名字，但這裡卻用了我教材裡的 `lib`。**欄位名稱必須和 struct 裡寫的一模一樣**，要用 `P.buflib.size()`。

順便補一個小地方：印完最好加 `"\n"` 換行，不然輸出會和 terminal 的提示字黏在一起：

```cpp
std::cout << P.buflib.size() << "\n";
```

## 三、怎麼修

照上面改完之後，`main.cpp` 的上半部應該長這樣（`Problem` 保持你寫的不變）：

```cpp
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct Bufferlibrary { ... };   // 你寫的，不用改

struct Problem { ... };         // 你寫的，不用改

bool readProblem(const std::string& path, Problem& P) {
    std::ifstream in(path);
    if (!in) return false;
    // 第 1 課 1-5 的 while 迴圈放這裡，再做 1-6 的練習
    return true;
}

int main(int argc, char** argv) {
    if (argc < 3) { ... }
    Problem P;
    if (!readProblem(argv[1], P)) {          // 讀檔失敗也要處理
        std::cerr << "error: cannot read " << argv[1] << "\n";
        return 1;
    }
    std::cout << P.buflib.size() << "\n";
    return 0;
}
```

**注意：** 第 1 課的範例用的是我的欄位名稱（`P.srcF`、`P.lib`），你要換成你自己的（`P.fanout`、`P.buflib`、`P.sink_x`…）。

改完之後，在 `M11502102/` 裡編譯（記得先 `conda activate eda`）：

```bash
cmake -S . -B build && cmake --build build
```

再跑跑看。空殼版本還沒讀任何資料，所以會印出 `0`。做完 1-6 的練習之後，要印出 `3`：

```bash
./build/cbi ../testcases/input001.cbi out.cbi
```

改好再叫我看。

---

# Claude

進步很多，`#include` 的問題都修好了，`CMakeLists.txt` 也能正常運作（`Configuring done`、`Generating done`）。剩下的兩個錯誤都是上次提過的第 4、5 點，還沒改到。

## 錯誤 1：`readProblem` was not declared（第 41 行）

```
error: 'readProblem' was not declared in this scope; did you mean 'Problem'?
```

你在 `main` 裡**呼叫**了 `readProblem`，但整個檔案裡**沒有寫這個函式**。第 30～31 行是空的，`main` 前面只有兩個 struct。

**觀念：呼叫函式之前，它必須已經存在。** C++ 不會自動生出 `readProblem`，名字也不是內建的，是我們自己要寫的函式。編譯器從上往下讀，讀到第 41 行時沒看過這個名字，所以報錯。它猜你是不是想打 `Problem`，但不是。

**改法：** 在第 30 行（`struct Problem` 的 `};` 下面、`int main` 上面）貼上這段：

```cpp
bool readProblem(const std::string& path, Problem& P) {
    std::ifstream in(path);              // 打開檔案
    if (!in) return false;               // 打不開就回傳 false

    std::string line;
    std::string section;                 // 現在在哪個區塊
    while (std::getline(in, line)) {     // 一行一行讀
        line = line.substr(0, line.find('#'));   // 砍掉註解
        std::istringstream ss(line);
        std::string tok;
        if (!(ss >> tok)) continue;      // 空行跳過

        if (tok == ".e") { section = ""; continue; }
        if (tok == ".limit" || tok == ".buflib" || tok == ".objective" || tok == ".pin") {
            section = tok;
            continue;
        }

        // TODO（練習 1-6）：依 section 把資料存進 P
    }
    return true;
}
```

逐行看懂這個函式的開頭：

```cpp
bool readProblem(const std::string& path, Problem& P)
```

| 部分 | 意思 |
|---|---|
| `bool` | 回傳「真 / 假」，讀成功是 `true`，失敗是 `false`。所以 `main` 裡可以寫 `if (!readProblem(...))` |
| `readProblem` | 函式名稱，要和 `main` 裡呼叫的名字一模一樣 |
| `const std::string& path` | 第 1 個參數：檔名。`const` = 函式裡不會改它 |
| `Problem& P` | 第 2 個參數：要填的箱子。`&` = 直接改 `main` 裡那個 P，而不是改複製品 |

## 錯誤 2：`Problem` has no member named `lib`（第 45 行）

```
error: 'struct Problem' has no member named 'lib'
```

你的 struct 第 22 行寫的是 `buflib`，這裡卻用了 `lib`。**名字要和 struct 裡一字不差**，改成：

```cpp
std::cout << P.buflib.size() << "\n";
```

## 小建議：錯誤訊息（第 42 行）

這不是錯誤，但建議補上檔名和換行，出錯時才知道是哪個檔案讀不到：

```cpp
std::cerr << "error: cannot read " << argv[1] << "\n";
```

---

## 改完之後

```bash
cmake --build build
```

如果出現 `Built target cbi`，代表編譯成功，接著執行：

```bash
./build/cbi ../testcases/input001.cbi out.cbi
```

這時會印出 **`0`**。這是對的，因為 TODO 還沒做，箱子裡還沒放任何型號。

接下來就是練習 1-6：把 TODO 換成真正存資料的程式。記得用你的欄位名稱，`P.fanout`、`P.length`、`P.dimx`、`P.buflib`、`P.w_skew`、`P.src_x`、`P.sink_x` 等等。第一步可以先只做 `.buflib`：

```cpp
if (section == ".buflib") {
    Bufferlibrary b;
    b.type = tok;                            // tok 是第一個字，例如 "SMALL"
    ss >> b.fanout >> b.length >> b.cost;    // 同一行接著讀 3 個數字
    P.buflib.push_back(b);                   // 放進箱子
}
```

加上這段之後，執行結果應該從 `0` 變成 **`3`**。看到 3 就代表你的 parser 成功讀進第一種資料了，接著用同樣的方式做 `.limit`、`.objective`、`.pin`。

---

# Claude

**編譯成功了！** 看到 `[100%] Built target cbi`，代表 `build/cbi` 這個執行檔已經產生。

## 那個黃色的 warning 是什麼

```
warning: unused parameter 'P' [-Wunused-parameter]
```

這是**警告，不是錯誤**，程式照樣編譯得出來。意思是「你的函式收了一個參數 `P`，卻從來沒用到它」。這是事實，因為迴圈裡只判斷了區塊，還沒把任何東西存進 `P`。編譯器在提醒你：是不是忘了做什麼？

**下一步把資料存進 `P`，這個警告就會自動消失。**

另外有個小地方：第 62 行的 `"error: cannot read"` 後面少一個空白，印出來會變成 `cannot read../testcases/...`，黏在一起了，改成 `"error: cannot read "`。

## 先跑跑看

```bash
./build/cbi ../testcases/input001.cbi out.cbi
```

應該會印出 `0`，因為還沒把型號放進 `P.buflib`。

---

## 下一步：把資料存進 P

在第 47 行的 `}` 之後、第 48 行的 `}` 之前（也就是迴圈的最後面）加程式。到這裡時，程式已經知道兩件事：

- `section`：現在在哪個區塊；
- `tok`：這一行的第一個字。

所以結構就是「**依區塊分開處理**」：

```cpp
        if (section == ".limit") {
            // 這一行可能是 fanout / length / .dimx / .dimy
        }
        else if (section == ".buflib") {
            // 這一行是一個型號：SMALL 3 90 10
        }
        else if (section == ".objective") {
            // 這一行是 w_skew 1
        }
        else if (section == ".pin") {
            // 這一行是一組座標：97 38
        }
```

### 第一步：`.buflib`（直接給你）

```cpp
        else if (section == ".buflib") {
            Bufferlibrary b;                       // 建一個空的型號
            b.type = tok;                          // 第一個字是名稱 "SMALL"
            ss >> b.fanout >> b.length >> b.cost;  // 同一行繼續讀 3 個數字
            P.buflib.push_back(b);                 // 放進箱子的型號表
        }
```

`ss >>` 的運作方式：`ss` 裡面是 `SMALL 3 90 10`，前面的 `ss >> tok` 已經拿走了 `SMALL`，所以接下來依序拿到 3、90、10。而且 `>>` 會依照變數的型態**自動把文字轉成數字**。

加完之後重新編譯、執行，應該印出 **`3`**。

### 第二步：`.limit`（你自己試）

這個區塊有 4 種行，第一個字分別是 `fanout`、`length`、`.dimx`、`.dimy`。用 `if (tok == "fanout")` 判斷是哪一種，再 `ss >>` 讀進 `Problem` 對應的欄位：

| 第一個字 `tok` | 存到 |
|---|---|
| `fanout` | `P.fanout` |
| `length` | `P.length` |
| `.dimx` | `P.dimx` |
| `.dimy` | `P.dimy` |

### 第三步：`.objective`（你自己試）

和 `.limit` 一樣的做法，`tok == "w_skew"` 時讀進 `P.w_skew`。

### 第四步：`.pin`（最需要動腦）

這裡的 `tok` 不是名稱，而是 **x 座標本身**，例如 `"97"`。有兩個問題要解決：

1. **`tok` 是字串，不是數字。** 用 `std::stoi(tok)` 把它轉成整數（string to int）。y 則再 `ss >>` 一次就好。
2. **第一行是 SRC，之後才是 sinks。** 要怎麼分辨？
   - **提示：** 在 `while` 迴圈**外面**宣告一個計數器，例如 `int pinCount = 0;`。每讀到一組座標就加 1。
   - 讀到時，如果是第 0 組，就存進 `P.src_x`、`P.src_y`；
   - 否則就 `P.sink_x.push_back(...)`，`sink_y` 也一樣。

   為什麼計數器要放在迴圈外面？因為放在裡面的話，每一行都會重新歸零，永遠記不住讀過幾組。

---

## 檢查點

四步都做完後，把 `main` 最後的輸出改成一次印出多個值：

```cpp
std::cout << P.buflib.size() << " " << P.sink_x.size() << " " << P.w_skew << "\n";
std::cout << "SRC = (" << P.src_x << ", " << P.src_y << ")\n";
```

用 `input001` 跑應該印出：

```
3 5 1
SRC = (97, 38)
```

用 `input011` 跑，第一行的中間要是 `10000`：

```bash
./build/cbi ../testcases/input011.cbi out.cbi
```

可以一步做完就編譯一次，不用等四步全做完。卡住了就把錯誤訊息截圖給我。

---

# 我

給我用case 處理

---

# Claude

可以用 `switch-case`，但有一個 C++ 的限制要先知道：**`switch` 不能直接用在字串上**。

```cpp
switch (section) {        // section 是 std::string
    case ".limit": ...    // ❌ 編譯錯誤：case 只能是整數
}
```

`switch` 只吃整數類的東西（`int`、`char`、`enum`）。解法是先用 **`enum`** 把每個區塊變成一個「有名字的整數」，再拿它來 `switch`。

## `enum` 是什麼

```cpp
enum Section { NONE, LIMIT, BUFLIB, OBJECTIVE, PIN };
```

這行定義了一個新型態 `Section`，它只能是這 5 個值之一。電腦內部其實存成 0、1、2、3、4，但你寫程式時用名字，比較好讀，也不會打錯字。用字串的話，`".limt"` 打錯了編譯器不會發現；用 enum 的話，`LIMT` 打錯會直接編譯錯誤。

## 完整的 `readProblem`（switch 版）

把你第 31～50 行的 `readProblem` 整個換成下面這段。我已經用**你的 struct 和欄位名稱**實際編譯並跑過，沒有任何 warning：

```cpp
enum Section { NONE, LIMIT, BUFLIB, OBJECTIVE, PIN };

bool readProblem(const std::string& path, Problem& P) {
    std::ifstream in(path);
    if (!in) return false;

    std::string line;
    Section section = NONE;   // 現在在哪個區塊（一開始不在任何區塊）
    int pinCount = 0;         // 讀過幾組座標（放在迴圈外，才不會每行歸零）

    while (std::getline(in, line)) {
        line = line.substr(0, line.find('#'));   // 砍掉註解
        std::istringstream ss(line);
        std::string tok;
        if (!(ss >> tok)) continue;              // 空行跳過

        // 1. 區塊的開頭 / 結尾：把字串換成 enum
        if (tok == ".e")         { section = NONE;      continue; }
        if (tok == ".limit")     { section = LIMIT;     continue; }
        if (tok == ".buflib")    { section = BUFLIB;    continue; }
        if (tok == ".objective") { section = OBJECTIVE; continue; }
        if (tok == ".pin")       { section = PIN;       continue; }

        // 2. 一般的資料行：依區塊分別處理
        switch (section) {
            case LIMIT:                                   // fanout 2 / length 100 / .dimx 110 / .dimy 100
                if      (tok == "fanout") ss >> P.fanout;
                else if (tok == "length") ss >> P.length;
                else if (tok == ".dimx")  ss >> P.dimx;
                else if (tok == ".dimy")  ss >> P.dimy;
                break;

            case BUFLIB: {                                // SMALL 3 90 10
                Bufferlibrary b;
                b.type = tok;
                ss >> b.fanout >> b.length >> b.cost;
                P.buflib.push_back(b);
                break;
            }

            case OBJECTIVE:                               // w_skew 1
                if (tok == "w_skew") ss >> P.w_skew;
                break;

            case PIN: {                                   // 97 38
                int x = std::stoi(tok);                   // tok 是字串 "97"，轉成整數
                int y = 0;
                ss >> y;
                if (pinCount == 0) {                      // 第一組 = SRC
                    P.src_x = x;
                    P.src_y = y;
                } else {                                  // 之後 = S1, S2, ...
                    P.sink_x.push_back(x);
                    P.sink_y.push_back(y);
                }
                ++pinCount;
                break;
            }

            case NONE:                                    // 不在任何區塊裡的字，忽略
                break;
        }
    }
    return true;
}
```

## switch 的三個重點

**1. 每個 `case` 最後一定要 `break;`**

`break` 代表「這個 case 做完了，跳出 switch」。忘了寫的話，程式會**繼續往下執行下一個 case**，這叫 fall-through。例如 `LIMIT` 少了 `break`，處理完 limit 之後會接著把這一行當成 buffer 型號再處理一次，這是很難抓的 bug。

**2. 在 case 裡宣告變數時，要加大括號 `{ }`**

```cpp
case BUFLIB: {          // ← 有大括號
    Bufferlibrary b;    // 在 case 裡宣告新變數
    ...
    break;
}
```

`BUFLIB` 和 `PIN` 都有大括號，`LIMIT` 和 `OBJECTIVE` 沒有，差別在於前兩個在裡面**宣告了新變數**（`b`、`x`、`y`）。不加大括號的話，這些變數的範圍會延伸到後面的 case，編譯器會報錯：`jump to case label crosses initialization`。

**3. 為什麼要寫 `case NONE:`**

就算它什麼都不做，也要列出來。原因是用 `enum` 做 switch 時，如果有某個值沒處理到，`-Wall` 會警告你 `enumeration value 'NONE' not handled`。把每個值都列出來，編譯器就能幫你檢查有沒有漏掉。

## if-else 和 switch 的分工

注意上面的程式**兩種都有用**：

- **區塊判斷**（`.limit`、`.pin`…）用 if 把字串換成 enum。這一步只能用 if，因為 switch 不能比較字串。
- **依區塊處理**用 switch，比一長串 `if (section == ...)` 清楚。
- **`LIMIT` 裡面**判斷 `fanout` / `length` 也還是用 if，因為 `tok` 也是字串。

---

## 檢查點

把 `main` 最後的 `std::cout` 換成：

```cpp
std::cout << P.buflib.size() << " " << P.sink_x.size() << " " << P.w_skew << "\n";
std::cout << "SRC = (" << P.src_x << ", " << P.src_y << ")\n";
std::cout << "limit: fanout=" << P.fanout << " length=" << P.length
          << " dim=" << P.dimx << "x" << P.dimy << "\n";
```

編譯並執行：

```bash
cmake --build build && ./build/cbi ../testcases/input001.cbi out.cbi
```

應該印出（我這邊實測的結果）：

```
3 5 1
SRC = (97, 38)
limit: fanout=2 length=100 dim=110x100
```

`input011` 應該是：

```
3 10000 1
SRC = (2077, 0)
limit: fanout=2 length=4500 dim=5000x5000
```

兩個都對，**第 1 課（讀檔）就完成了**。接下來是第 2 課：計算每個 sink 的 arrival time 和 Score，並判斷一棵樹是否合法。

---

# 我

還是用 if 就好

---

# Claude

好，改回 if。這樣也比較簡單：不用 `enum`，也不用擔心 `break` 和 case 裡的大括號。

## 完整的 `readProblem`（if 版）

把你第 31～50 行的 `readProblem` 整個換成下面這段。我用你的 struct 和欄位名稱實際編譯並跑過，沒有任何 warning：

```cpp
bool readProblem(const std::string& path, Problem& P) {
    std::ifstream in(path);
    if (!in) return false;

    std::string line;
    std::string section;      // 現在在哪個區塊，例如 ".limit"
    int pinCount = 0;         // 讀過幾組座標（放在迴圈外，才不會每行歸零）

    while (std::getline(in, line)) {
        line = line.substr(0, line.find('#'));   // 砍掉註解
        std::istringstream ss(line);
        std::string tok;
        if (!(ss >> tok)) continue;              // 空行跳過

        // 1. 區塊的開頭 / 結尾
        if (tok == ".e") { section = ""; continue; }
        if (tok == ".limit" || tok == ".buflib" || tok == ".objective" || tok == ".pin") {
            section = tok;
            continue;
        }

        // 2. 一般的資料行：依區塊分別處理
        if (section == ".limit") {                 // fanout 2 / length 100 / .dimx 110 / .dimy 100
            if      (tok == "fanout") ss >> P.fanout;
            else if (tok == "length") ss >> P.length;
            else if (tok == ".dimx")  ss >> P.dimx;
            else if (tok == ".dimy")  ss >> P.dimy;
        }
        else if (section == ".buflib") {           // SMALL 3 90 10
            Bufferlibrary b;
            b.type = tok;
            ss >> b.fanout >> b.length >> b.cost;
            P.buflib.push_back(b);
        }
        else if (section == ".objective") {        // w_skew 1
            if (tok == "w_skew") ss >> P.w_skew;
        }
        else if (section == ".pin") {              // 97 38
            int x = std::stoi(tok);                // tok 是字串 "97"，轉成整數
            int y = 0;
            ss >> y;
            if (pinCount == 0) {                   // 第一組 = SRC
                P.src_x = x;
                P.src_y = y;
            } else {                               // 之後 = S1, S2, ...
                P.sink_x.push_back(x);
                P.sink_y.push_back(y);
            }
            ++pinCount;
        }
    }
    return true;
}
```

## 讀這段程式的重點

**`if` / `else if` 的差別：**
- 一連串的 `else if` 中，**只會執行第一個成立的**，後面的就不會再檢查。
- 一行資料只屬於一個區塊，所以用 `else if` 剛好：找到對應區塊就處理，不會重複處理。

**`continue`：**
- 第 1 部分的 `continue` 代表「這一行處理完了，直接讀下一行」。
- 區塊標題本身（`.limit`）不是資料，所以記下區塊名稱後就跳過，不會跑到第 2 部分。

**`.pin` 為什麼和其他區塊不一樣：**
- 其他區塊的第一個字是「名稱」（`fanout`、`SMALL`、`w_skew`），數字在後面；
- `.pin` 的第一個字**本身就是數字**（x 座標），而 `tok` 是字串，所以要用 `std::stoi` 轉換。y 還在 `ss` 裡，用 `ss >> y` 讀出來。

## 檢查點

`main` 最後的輸出換成：

```cpp
std::cout << P.buflib.size() << " " << P.sink_x.size() << " " << P.w_skew << "\n";
std::cout << "SRC = (" << P.src_x << ", " << P.src_y << ")\n";
std::cout << "limit: fanout=" << P.fanout << " length=" << P.length
          << " dim=" << P.dimx << "x" << P.dimy << "\n";
```

編譯並執行：

```bash
cmake --build build && ./build/cbi ../testcases/input001.cbi out.cbi
```

我實測的結果，`input001`：

```
3 5 1
SRC = (97, 38)
limit: fanout=2 length=100 dim=110x100
```

`input011`：

```
3 10000 1
SRC = (2077, 0)
limit: fanout=2 length=4500 dim=5000x5000
```

你那邊也跑出一樣的數字，**第 1 課（讀檔）就完成了**。完成後告訴我，我們進第 2 課：計算 arrival time、skew 和 Score，並檢查一棵樹合不合法。

---

# 我

已完成

---

# Claude

你的第 1 課我實際編譯並跑過了：`input001` 和 `input011` 的結果都正確，也沒有任何 warning。**讀檔完成！**

小提醒：第 93 行 `"error: cannot read"` 後面還是少一個空白，順手補上就好。

---

# 第 2 課：用程式表示一棵樹，並幫它打分數

讀完檔之後，我們有了「題目」。這一課要做兩件事：

1. **表示「答案」：** 也就是一棵樹，記錄 buffer 放在哪裡、誰接誰。
2. **幫答案打分數：** 檢查這棵樹合不合法，算出 T_max、T_min、Score。

為什麼先做打分數，而不是先建樹？因為**沒有打分數的函式，你寫出任何樹都不知道對不對**。這個函式之後會用上幾萬次：建樹時用來檢查，最佳化時用來比較好壞。

## 2-1. 先用眼睛看懂一棵樹

PDF 範例的 `output1` 長這樣：

```
                 SRC (97,38)
                /           \
        B1 (60,61)        B2 (77,38)
        /   |   \           /    \
      S1   S3   S5        S2     S4
```

- 每個節點最多只有**一個 parent**，但可以有**很多個 children**。
- **arrival time** = 從 SRC 走到那個 sink，沿路每條邊的 Manhattan 距離加起來。
- 例如 S1 = d(SRC,B1) + d(B1,S1) = (37+23) + (11+1) = 60 + 12 = 72。

## 2-2. 節點編號：所有東西都放進同一個 vector

我們用一個 `std::vector<Node>` 裝所有節點，**用位置（index）當作節點的編號**：

| 編號 | 是什麼 | 範例 |
|---|---|---|
| `0` | SRC | 節點 0 |
| `1 .. N` | sink S1..SN | 節點 1～5 = S1～S5 |
| `N+1 ..` | buffer，建樹時一個一個加 | 節點 6 = B1、節點 7 = B2 |

這樣做的好處是：**「誰接誰」只要存編號（整數）就好**。例如 B1 的 children 就是 `{1, 3, 5}`。

## 2-3. 動手：樹的資料結構

把下面這段貼在 `readProblem` 的 `}` 之後、`main` 之前。名稱已經照你的風格取了：

```cpp
// ===== 第 2 課：樹 =====
enum Kind { SRC = 0, SINK = 1, BUF = 2 };   // 節點種類

struct Node {
    int x = 0, y = 0;            // 座標
    int parent = -1;             // parent 的編號，-1 代表還沒有 parent
    int kind = BUF;              // SRC / SINK / BUF
    std::vector<int> children;   // 所有 children 的編號
};

struct Tree {
    std::vector<Node> nodes;     // nodes[0] = SRC, nodes[1..N] = sinks, 之後是 buffers
    int num_sinks = 0;           // N
};

// 兩個節點之間的 Manhattan 距離 |x1-x2| + |y1-y2|
long long dist(const Node& a, const Node& b) {
    return std::llabs((long long)a.x - b.x) + std::llabs((long long)a.y - b.y);
}

// 建立一棵「只有 SRC 和 sinks、還沒有任何連線」的樹
Tree makeTree(const Problem& P) {
    Tree T;
    T.num_sinks = (int)P.sink_x.size();
    T.nodes.resize(T.num_sinks + 1);         // 開 N+1 個空節點
    T.nodes[0].x = P.src_x;
    T.nodes[0].y = P.src_y;
    T.nodes[0].kind = SRC;
    for (int i = 1; i <= T.num_sinks; ++i) {
        T.nodes[i].x = P.sink_x[i - 1];      // 注意：S1 存在 sink_x[0]
        T.nodes[i].y = P.sink_y[i - 1];
        T.nodes[i].kind = SINK;
    }
    return T;
}

// 新增一個 buffer，回傳它的編號
int addBuffer(Tree& T, int x, int y) {
    Node b;
    b.x = x;
    b.y = y;
    b.kind = BUF;
    T.nodes.push_back(b);
    return (int)T.nodes.size() - 1;          // 剛放進去的就是最後一個
}

// 讓 child 接在 parent 底下（兩邊都要記）
void link(Tree& T, int child, int parent) {
    T.nodes[parent].children.push_back(child);
    T.nodes[child].parent = parent;
}
```

最上面的 `#include` 要再加兩個：

```cpp
#include <algorithm>   // std::sort, std::max, std::min
#include <cstdlib>     // std::llabs（long long 的絕對值）
```

**新觀念：**
- **`(long long)a.x`** 是「型態轉換」，先把 int 轉成 long long 再相減。座標很大時，兩個 int 相減可能超出 int 的範圍。
- **`const Node& a`** 和 `Problem& P` 一樣用 `&`，可以避免複製一整個 Node。`const` 代表只讀不改。
- **`link` 要改兩邊：** parent 的 children 加上 child，child 的 parent 設成 parent。只改一邊的話，資料就會不一致。

## 2-4. 練習：`cheapestType`（自己寫）

題目的關鍵觀念：**buffer 的型號不用自己決定**。只要知道一個 buffer 有幾個 children（k），以及到所有 children 的總距離（len），**能撐得住的型號中最便宜的那個**就是答案。

```cpp
// 回傳「fanout >= k 且 length >= len」的型號中，cost 最小的 index；都不行就回傳 -1
int cheapestType(const Problem& P, int k, long long len) {
    int best = -1;
    // TODO：用 for 迴圈看過 P.buflib 的每一個型號
    //   - 撐不住的（fanout < k 或 length < len）就跳過（continue）
    //   - 撐得住的，如果 best 還是 -1，或者它比 P.buflib[best] 便宜，就把 best 換成它
    return best;
}
```

**為什麼回傳 index 而不是型號本身？** 因為 -1 可以代表「找不到」。之後要用型號時再寫 `P.buflib[t]`。

**自我檢查**，用範例的型號庫（SMALL 3/90/10、MEDIUM 6/120/16、LARGE 9/190/22）：

| 呼叫 | 應該回傳 | 原因 |
|---|---|---|
| `cheapestType(P, 3, 80)` | `0`（SMALL） | 三個都撐得住，SMALL 最便宜 |
| `cheapestType(P, 3, 100)` | `1`（MEDIUM） | SMALL 的 L=90 不夠 |
| `cheapestType(P, 7, 50)` | `2`（LARGE） | 只有 LARGE 的 F ≥ 7 |
| `cheapestType(P, 10, 50)` | `-1` | 沒有型號的 F ≥ 10 |

## 2-5. `evaluate`：從頭檢查並計分

這是本課的核心。它要照 TA verifier 的規則一條一條檢查，任何一條不過就回傳「不合法」和原因：

```cpp
struct Eval {
    bool legal = false;     // 合不合法
    std::string why;        // 不合法的原因（debug 用）
    long long tmax = 0, tmin = 0, cost = 0, score = 0;
};

Eval evaluate(const Problem& P, const Tree& T) {
    Eval e;
    int n = (int)T.nodes.size();

    // 規則 1：所有座標都不能重疊
    //   把 (x, y) 壓成一個數字，排序後「相鄰兩個相同」就代表重疊
    std::vector<long long> keys;
    for (const Node& v : T.nodes) keys.push_back(((long long)v.x << 32) | (unsigned int)v.y);
    std::sort(keys.begin(), keys.end());
    for (int i = 1; i < n; ++i) {
        if (keys[i] == keys[i - 1]) { e.why = "overlapping coordinates"; return e; }
    }

    // 規則 2、3：每個 buffer 都要在晶片「內部」、至少一個 child、找得到型號
    for (int v = T.num_sinks + 1; v < n; ++v) {
        const Node& b = T.nodes[v];
        if (b.x <= 0 || b.x >= P.dimx || b.y <= 0 || b.y >= P.dimy) { e.why = "buffer outside chip"; return e; }
        if (b.children.empty()) { e.why = "buffer without children"; return e; }
        long long len = 0;
        for (int c : b.children) len += dist(b, T.nodes[c]);      // 到所有 children 的距離總和
        int t = cheapestType(P, (int)b.children.size(), len);
        if (t == -1) { e.why = "buffer over fanout/length limit"; return e; }
        e.cost += P.buflib[t].cost;                                // 順便累加成本
    }

    // 規則 4：SRC 用 .limit 的限制
    long long srcLen = 0;
    for (int c : T.nodes[0].children) srcLen += dist(T.nodes[0], T.nodes[c]);
    if ((int)T.nodes[0].children.size() > P.fanout || srcLen > P.length) { e.why = "SRC over limit"; return e; }

    // 規則 5：從 SRC 做 BFS，每個節點都要走得到；順便算 arrival
    std::vector<long long> arrival(n, 0);
    std::vector<int> queue;
    queue.push_back(0);
    for (int i = 0; i < (int)queue.size(); ++i) {
        int u = queue[i];
        for (int c : T.nodes[u].children) {
            arrival[c] = arrival[u] + dist(T.nodes[u], T.nodes[c]);   // 子 = 父 + 邊長
            queue.push_back(c);
        }
    }
    if ((int)queue.size() != n) { e.why = "some nodes unreachable"; return e; }

    // 計分：所有 sink 的最大 / 最小 arrival
    e.tmax = arrival[1];
    e.tmin = arrival[1];
    for (int s = 1; s <= T.num_sinks; ++s) {
        e.tmax = std::max(e.tmax, arrival[s]);
        e.tmin = std::min(e.tmin, arrival[s]);
    }
    e.score = e.cost + P.w_skew * (e.tmax - e.tmin);
    e.legal = true;
    return e;
}
```

### 重點解說：BFS（廣度優先搜尋）

這是本課最重要的演算法。想像 SRC 發出一個訊號往下傳：

```
queue: [0]                → 處理 SRC：arrival[B1]=60, arrival[B2]=20，把 B1、B2 排進去
queue: [0, 6, 7]          → 處理 B1：算 S1、S3、S5 的 arrival，排進去
queue: [0, 6, 7, 1, 3, 5] → 處理 B2：算 S2、S4，排進去
queue: [0, 6, 7, 1, 3, 5, 2, 4] → sinks 沒有 children，什麼都不做
```

- **為什麼一定算得對？** 處理一個節點時，它的 parent 一定**更早**被處理過，所以 `arrival[u]` 已經算好了，子節點只要「父 + 邊長」。
- **為什麼能檢查「全部走得到」？** 每個走得到的節點都會進 queue 剛好一次。如果最後 `queue.size()` 比節點總數少，就代表有節點沒接到樹上。
- **`for (int c : T.nodes[u].children)`** 叫「範圍 for 迴圈」，意思是「對 children 裡的每一個 c 做…」，比寫 `for (int j = 0; j < ...; ++j)` 簡潔。

### 重點解說：為什麼 buffer 是 `x <= 0 || x >= P.dimx` 就不合法

TA 的 verifier 要求 buffer **嚴格在內部**，也就是 `0 < x < dimx`，**壓在邊界上也算錯**。這是比 PDF 更嚴格的地方，很多人會在這裡被扣分。SRC 和 sinks 沒有這個限制，題目本身就可能把 SRC 放在 y=0。

## 2-6. 檢查點：自我測試

把範例 output1 的樹「手動」建出來，看分數對不對。加一個 `selfTest` 函式（放在 `evaluate` 之後）：

```cpp
int selfTest(const Problem& P) {
    Tree T = makeTree(P);
    int b1 = addBuffer(T, 60, 61);
    int b2 = addBuffer(T, 77, 38);
    link(T, b1, 0);
    link(T, b2, 0);
    link(T, 1, b1); link(T, 3, b1); link(T, 5, b1);   // B1:{S1 S3 S5}
    link(T, 2, b2); link(T, 4, b2);                   // B2:{S2 S4}
    Eval e = evaluate(P, T);
    std::cout << "legal=" << e.legal << " T_max=" << e.tmax << " T_min=" << e.tmin
              << " cost=" << e.cost << " Score=" << e.score << " " << e.why << "\n";
    std::cout << "應該是 legal=1 T_max=141 T_min=36 cost=26 Score=131\n";

    T.nodes[b1].x = 0;   // 故意把 B1 移到邊界上，看會不會被抓到
    Eval bad = evaluate(P, T);
    std::cout << "移到邊界後: legal=" << bad.legal << " why=" << bad.why << "\n";
    return 0;
}
```

在 `main` 裡，讀檔成功之後加一行，帶 `--selftest` 參數時就跑測試。可以把第 1 課那三行 `cout` 刪掉了：

```cpp
if (argc >= 4 && std::string(argv[3]) == "--selftest") return selfTest(P);
```

編譯並執行：

```bash
cmake --build build && ./build/cbi ../testcases/input001.cbi out.cbi --selftest
```

我這邊用你的程式加上這一課的程式實測，結果是：

```
legal=1 T_max=141 T_min=36 cost=26 Score=131
應該是 legal=1 T_max=141 T_min=36 cost=26 Score=131
移到邊界後: legal=0 why=buffer outside chip
```

**第一行和第二行一模一樣，就代表你的 `cheapestType` 和 `evaluate` 都寫對了。** 數字和 PDF 範例的 `T_max: 141, T_min: 36, Score: 131` 也完全吻合。

如果 cost 不是 26，通常是 `cheapestType` 寫錯了：B1 要選 MEDIUM（16），B2 要選 SMALL（10）。卡住就截圖給我。下一課（2B）是把樹寫成輸出檔，之後就能交給 TA 的 verifier 檢查了。

---

# 我

那你覺得此題 樹最好用哪種方式(資料結構)儲存

---

# Claude

我建議用**現在這種方式**：所有節點放在同一個 `std::vector<Node>` 裡，用位置（index）當編號，每個節點同時記 **`parent`（一個編號）** 和 **`children`（編號的 vector）**。所有參考解，包括最強的 Ultracode_3，用的都是這個結構。下面說明為什麼，以及之後要往裡面加什麼。

## 先看這題會對樹做哪些操作

選資料結構要看**你最常對它做什麼**：

| 操作 | 什麼時候用 | 需要 |
|---|---|---|
| 從上往下走（BFS） | 算 arrival、寫輸出檔 | 從節點找到 **children** |
| 從下往上走 | 改了一個 buffer，重算它的祖先 | 從節點找到 **parent** |
| 換 parent（reparent） | 最佳化：把 sink 移到別的 buffer 底下 | 兩個方向都要能快速改 |
| 新增 / 刪除 buffer | 建樹、最佳化 | 節點數量會變 |
| 整棵複製 | 存「目前最好的解」，或試做失敗後還原 | 複製要快 |
| 查座標是否被佔用 | 每次放 buffer 都要查 | 另外一個表 |

## 幾種寫法的比較

| 寫法 | 長相 | 評價 |
|---|---|---|
| **vector + index，parent 和 children 都存**（推薦） | `nodes[i].parent`、`nodes[i].children` | ✅ 往上、往下都快；複製就是複製 vector；編號就是輸出的名字 |
| 只存 parent 陣列 | `parent[i]` | ❌ 往上很快，但要找 children 得掃過全部 N 個節點，BFS 會變成 O(N²) |
| 只存 children | `children[i]` | ❌ 往下很快，但「改了這個節點，往上重算」時找不到 parent |
| 指標（`Node*`、`new`） | `node->children[0]->parent` | ❌ 複製整棵樹時指標全部要重新接；刪節點容易出現懸空指標；新手很容易當掉 |
| 相鄰矩陣 | `adj[i][j] = 1` | ❌ 10 萬個節點要 10¹⁰ 格，記憶體直接爆掉 |
| first-child / next-sibling | 每個節點只記第一個 child 和下一個兄弟 | ⚠️ 省記憶體，但「拿到所有 children」要一個一個走，程式也比較難寫，這題不需要 |

## 推薦寫法的三個優點

**1. 雙向都是 O(1) 起步。**
- 往下：`for (int c : nodes[u].children)`。
- 往上：`u = nodes[u].parent`，一直到 -1。

這題兩個方向都會大量使用：BFS 往下，最佳化後往上重算。所以兩個方向都要存。

**2. 用整數編號，不用指標。**
- 複製整棵樹只要 `Tree backup = T;`，一行就完成，所有 parent/children 的編號照樣有效。指標版則要整棵重新接一遍。
- 「最佳化試做 → 不好就還原」是這題的核心技巧，這點非常重要。

**3. 編號直接對應題目的名字。**
- 節點 `1..N` 就是 S1..SN，輸出時不用查表。
- buffer 只要在輸出時依 BFS 順序重新編號成 B1、B2…。

**唯一要付的代價：** `parent` 和 `children` 是兩份資料，**一定要一起改**，否則會不一致。所以我們寫了 `link()`，之後也會寫 `unlink()`。**永遠只透過這兩個函式改連線，不要在別的地方直接改 `parent` 或 `children`。**

## 隨著進度，Node 裡要加的東西

現在的 `Node` 夠用到第 3 課。之後做最佳化時，會逐步加入這些欄位。參考解的 [tree.h](Lab2/claude_reference/src/tree.h) 就是最終的樣子：

| 加什麼 | 解決什麼問題 | 第幾課 |
|---|---|---|
| `unlink(child)` 函式 | 換 parent 之前，要先從舊 parent 的 children 裡拿掉 | 4 |
| `alive` 旗標 + free list | 刪掉的 buffer 不要真的從 vector 移除（會讓後面的編號全部錯位），只標記成「已刪除」，之後新增時重複使用這個編號 | 4 |
| `sumLen` | 快取「到所有 children 的距離總和」，不用每次重算 | 4 |
| `lo` / `hi` | **子樹裡到 sink 的最短 / 最長路徑**。整棵樹的 T_max 就是 `nodes[0].hi`，移動一個 buffer 只要沿祖先往上更新，不用整棵 BFS，可以快上百倍 | 5 |
| 已佔用座標表（`unordered_set` 或 bitmap） | 判斷「這個位置能不能放 buffer」從掃過全部節點 O(N) 變成 O(1) | 3 |

**最後一點特別重要。** GPT61Sol 找空位時，每個候選位置都掃過全部節點，10 萬個 sinks 光建樹就要約 50 秒，差點超過時限。那一版用的就是這個樹結構，問題出在沒有另外建佔用表，不是樹本身的設計。

## 結論

- **樹本體：** `vector<Node>` + 整數編號 + 雙向連結（`parent` + `children`），現在的寫法就是正確答案，不用改。
- **輔助結構：** 佔用座標表、子樹統計 `lo/hi`，這兩個才是效能的關鍵，之後的課程會一步一步加上去。

我們繼續第 2 課的 `cheapestType` 和 `evaluate` 吧。跑完 selftest 再截圖給我。

---

# 我

指標（Node*、new） 會更快 更省嗎

---

# Claude

**不會。** 在這一題，指標版在速度上沒有優勢，記憶體反而用得比較多，還多了很多容易當掉的風險。我寫了一個小測試，直接量給你看。

## 實測：100 萬個節點的樹，兩種寫法各做一次 BFS

兩邊的 `Node` 都只有座標、parent、children，BFS 做的事完全一樣（算出相同的 arrival 總和），都用 `-O2` 編譯：

| | vector + index（現在的寫法） | 指標（`Node*` + `new`） |
|---|---|---|
| BFS 一次 | **0.0296 秒** | 0.0306 秒 |
| 記憶體 | **56.6 MB** | 75.1 MB（多 33%） |
| 建樹 | **0.048 秒** | 0.077 秒（慢 60%） |

速度幾乎一樣，差 3% 只是雜訊；指標版的記憶體多 1/3，建樹也比較慢。

## 為什麼指標不會比較快

很多人以為「指標直接指到記憶體位置，所以比較快」，但：

- **`nodes[i]` 本身就是一次位址計算：** 起點 + i × 每個節點的大小。CPU 一個指令就做完，和「沿著指標找過去」的成本幾乎一樣。
- **真正決定速度的是快取（cache），不是取值的方式。** CPU 讀記憶體時，一次會把附近一整塊載進快取。
  - `vector` 的節點**連續排在一起**，讀到一個節點時，旁邊的也順便載進來了。
  - `new` 出來的節點**散在記憶體各處**。剛建好時通常還算連續，但最佳化過程中不斷新增、刪除 buffer 以後，會越來越分散。

## 為什麼指標比較耗記憶體

| 原因 | vector + index | 指標 |
|---|---|---|
| children 裡每一格 | `int`：4 bytes | `Node*`：8 bytes（64 位元電腦） |
| 每個節點的額外成本 | 0 | 每次 `new` 都有約 16 bytes 的管理資料 |
| 用編號找節點 | 直接 `nodes[i]` | 要另外開一個 `vector<Node*>` 對照表 |

最後一項這題躲不掉：輸出時要用「S3」找到第 3 個 sink，所以**指標版最後還是得有一個用編號查的陣列**，等於兩種結構都要存。

## 指標在這題的真正麻煩

比速度和記憶體更重要的是，這題最常做的動作對指標很不友善：

**1. 整棵複製（存最佳解、試做失敗還原）**
- index 版：`Tree backup = T;`，一行完成，100 萬個節點 0.028 秒。
- 指標版：複製出來的節點，裡面的 `parent` 和 `children` **還指著舊樹的節點**。你得自己寫一個函式，整棵走一遍重新接線，一不小心就會改到舊的那棵。

**2. 刪除 buffer**
- index 版：標記成「已刪除」就好，編號還在，不會出事。
- 指標版：`delete` 之後，如果還有任何地方存著這個指標，再去讀它就會發生**未定義行為**：可能當掉，也可能讀到垃圾值卻不報錯。後者最可怕，因為你的程式會「看起來正常」，交出一個錯的答案。

**3. 記得釋放記憶體**
- 每個 `new` 都要有對應的 `delete`，忘了就會記憶體洩漏。index 版的 `vector` 會自動處理。

## 什麼時候指標才比較好

當節點**沒有固定編號**，又需要頻繁在中間插入或刪除時（例如 linked list、平衡二元搜尋樹），指標比較自然。

但這題剛好相反：節點**天生就有編號**（S1、S2…、B1、B2…），數量也只是穩定增加，非常適合放在 `vector` 裡。

**結論：繼續用現在的 vector + index 寫法。** 它在這題又快又省，也比較不容易出錯。

---

# 我

那給我 vector + index 寫法

---

# Claude

下面是完整的 vector + index 樹，用的是你的命名風格。比第 2 課 2-3 多了兩個函式：
- **`unlink`：** 把節點從 parent 拔下來，第 4 課換 parent 時一定要用。
- **`printTree`：** 把整棵樹印出來，debug 用。

我把它接在你目前的 `main.cpp` 後面實際編譯並跑過，沒有任何 warning。

## 1. 最上面的 `#include` 加兩行

```cpp
#include <algorithm>   // std::sort, std::max, std::min
#include <cstdlib>     // std::llabs
```

## 2. 貼在 `readProblem` 的 `}` 之後、`main` 之前

```cpp
// =====================================================================
// 樹：vector + index
//   nodes[0]       = SRC
//   nodes[1..N]    = sinks S1..SN
//   nodes[N+1..]   = buffers（輸出時再依 BFS 順序重新編號成 B1, B2, ...）
// =====================================================================
enum Kind { SRC = 0, SINK = 1, BUF = 2 };

struct Node {
    int x = 0, y = 0;            // 座標
    int parent = -1;             // parent 的編號，-1 = 還沒有 parent
    int kind = BUF;              // SRC / SINK / BUF
    std::vector<int> children;   // children 的編號
};

struct Tree {
    std::vector<Node> nodes;     // 所有節點，位置就是編號
    int num_sinks = 0;           // N
};

// Manhattan 距離 |x1-x2| + |y1-y2|
long long dist(const Node& a, const Node& b) {
    return std::llabs((long long)a.x - b.x) + std::llabs((long long)a.y - b.y);
}

// 建立只有 SRC 和 sinks、還沒有任何連線的樹
Tree makeTree(const Problem& P) {
    Tree T;
    T.num_sinks = (int)P.sink_x.size();
    T.nodes.resize(T.num_sinks + 1);         // 開 N+1 個節點
    T.nodes[0].x = P.src_x;
    T.nodes[0].y = P.src_y;
    T.nodes[0].kind = SRC;
    for (int i = 1; i <= T.num_sinks; ++i) {
        T.nodes[i].x = P.sink_x[i - 1];      // S1 存在 sink_x[0]
        T.nodes[i].y = P.sink_y[i - 1];
        T.nodes[i].kind = SINK;
    }
    return T;
}

// 新增一個 buffer，回傳它的編號
int addBuffer(Tree& T, int x, int y) {
    Node b;
    b.x = x;
    b.y = y;
    b.kind = BUF;
    T.nodes.push_back(b);
    return (int)T.nodes.size() - 1;          // 剛放進去的 = 最後一個
}

// child 接到 parent 底下（兩邊都要記）
void link(Tree& T, int child, int parent) {
    T.nodes[parent].children.push_back(child);
    T.nodes[child].parent = parent;
}

// 把 child 從它的 parent 拔下來（兩邊都要清）
void unlink(Tree& T, int child) {
    int p = T.nodes[child].parent;
    if (p < 0) return;                       // 本來就沒有 parent
    std::vector<int>& ch = T.nodes[p].children;
    for (int i = 0; i < (int)ch.size(); ++i) {
        if (ch[i] == child) {
            ch[i] = ch.back();               // 用最後一個蓋掉它
            ch.pop_back();                   // 再把最後一個刪掉
            break;
        }
    }
    T.nodes[child].parent = -1;
}

// 節點的名字（debug 用）：SRC、S3、buf6
std::string nodeName(const Tree& T, int v) {
    if (v == 0) return "SRC";
    if (v <= T.num_sinks) return "S" + std::to_string(v);
    return "buf" + std::to_string(v);
}

// 把整棵樹印出來（debug 用）
void printTree(const Tree& T) {
    for (int v = 0; v < (int)T.nodes.size(); ++v) {
        const Node& n = T.nodes[v];
        std::cout << nodeName(T, v) << " (" << n.x << "," << n.y << ")";
        if (n.parent >= 0) std::cout << " parent=" << nodeName(T, n.parent);
        if (!n.children.empty()) {
            std::cout << " children={";
            for (int c : n.children) std::cout << " " << nodeName(T, c);
            std::cout << " }";
        }
        std::cout << "\n";
    }
}
```

## 3. 在 `main` 裡測試

把 `main` 最後第 1 課的三行 `std::cout` 換成這段，手動建出範例 output1 的樹，再做一次「換 parent」：

```cpp
    Tree T = makeTree(P);
    int b1 = addBuffer(T, 60, 61);
    int b2 = addBuffer(T, 77, 38);
    link(T, b1, 0);
    link(T, b2, 0);
    link(T, 1, b1); link(T, 3, b1); link(T, 5, b1);   // B1:{S1 S3 S5}
    link(T, 2, b2); link(T, 4, b2);                   // B2:{S2 S4}
    printTree(T);

    std::cout << "--- 把 S5 從 B1 換到 B2 ---\n";
    unlink(T, 5);       // 先拔
    link(T, 5, b2);     // 再接
    printTree(T);
```

編譯並執行：

```bash
cmake --build build && ./build/cbi ../testcases/input001.cbi out.cbi
```

我實測的輸出：

```
SRC (97,38) children={ buf6 buf7 }
S1 (49,62) parent=buf6
S2 (17,25) parent=buf7
S3 (56,82) parent=buf6
S4 (72,27) parent=buf7
S5 (9,91) parent=buf6
buf6 (60,61) parent=SRC children={ S1 S3 S5 }
buf7 (77,38) parent=SRC children={ S2 S4 }
--- 把 S5 從 B1 換到 B2 ---
...
S5 (9,91) parent=buf7
buf6 (60,61) parent=SRC children={ S1 S3 }
buf7 (77,38) parent=SRC children={ S2 S4 S5 }
```

可以看到 S5 的 parent 從 buf6 變成 buf7，**兩邊的 children 也同時更新了**。這就是 `link` / `unlink` 要「兩邊一起改」的原因。

## 三個要記住的重點

**1. buffer 的編號從 N+1 開始。** 範例有 5 個 sink，所以第一個 buffer 是 `buf6`，不是 B1。正式的 B1、B2 名字等到寫輸出檔時才重新編。

**2. 換 parent 一定是「先 unlink 再 link」。** 少了 unlink，舊 parent 的 children 裡還會留著這個節點，變成「一個節點有兩個 parent」，evaluate 會算錯，也過不了 verifier。

**3. `unlink` 裡「用最後一個蓋掉」的技巧。** 從 vector 中間刪除一個元素，正常要把後面的全部往前移，是 O(n)。children 的順序不重要，所以直接拿最後一個來填空，再 `pop_back()`，只要 O(1)。

樹的部分確認沒問題後，接著做第 2 課的 **2-4 `cheapestType`** 和 **2-5 `evaluate`**。做完把 `main` 換成 2-6 的 `selfTest`，看到 `Score=131` 就代表計分也對了。

---

# 我

你可以用 物件導向設計

---

# Claude

可以。這題的樹很適合用物件導向：把「資料」和「改資料的規則」包在同一個 `class` 裡，**讓外面的程式沒辦法亂改**。parent 和 children 必須同步，正是最需要保護的地方。

下面這個版本我接在你目前的 `main.cpp` 後面實際編譯並跑過，沒有 warning。

## 物件導向版的 `Tree`

一樣先在最上面加：

```cpp
#include <algorithm>
#include <cstdlib>
```

然後貼在 `readProblem` 之後、`main` 之前：

```cpp
// =====================================================================
// Tree 類別：vector + index
//   編號 0 = SRC，1..N = sinks S1..SN，N+1.. = buffers
// =====================================================================
enum Kind { SRC = 0, SINK = 1, BUF = 2 };

struct Node {
    int x = 0, y = 0;
    int parent = -1;              // -1 = 沒有 parent
    int kind = BUF;
    std::vector<int> children;
};

class Tree {
public:
    // 建構子：建立只有 SRC 和 sinks 的樹（取代原本的 makeTree）
    explicit Tree(const Problem& P) {
        num_sinks_ = (int)P.sink_x.size();
        nodes_.resize(num_sinks_ + 1);
        nodes_[0].x = P.src_x;
        nodes_[0].y = P.src_y;
        nodes_[0].kind = SRC;
        for (int i = 1; i <= num_sinks_; ++i) {
            nodes_[i].x = P.sink_x[i - 1];
            nodes_[i].y = P.sink_y[i - 1];
            nodes_[i].kind = SINK;
        }
    }

    // ---- 讀取（const：保證不會改到樹）----
    int size() const { return (int)nodes_.size(); }
    int numSinks() const { return num_sinks_; }
    const Node& node(int v) const { return nodes_[v]; }   // 只能看，不能改

    long long dist(int a, int b) const {
        return std::llabs((long long)nodes_[a].x - nodes_[b].x) +
               std::llabs((long long)nodes_[a].y - nodes_[b].y);
    }

    // ---- 修改（只有這幾個函式能改樹）----
    int addBuffer(int x, int y) {
        Node b;
        b.x = x;
        b.y = y;
        b.kind = BUF;
        nodes_.push_back(b);
        return (int)nodes_.size() - 1;
    }

    void link(int child, int parent) {
        if (nodes_[child].parent >= 0) unlink(child);   // 已經有 parent → 自動先拔掉
        nodes_[parent].children.push_back(child);
        nodes_[child].parent = parent;
    }

    void unlink(int child) {
        int p = nodes_[child].parent;
        if (p < 0) return;
        std::vector<int>& ch = nodes_[p].children;
        for (int i = 0; i < (int)ch.size(); ++i) {
            if (ch[i] == child) {
                ch[i] = ch.back();
                ch.pop_back();
                break;
            }
        }
        nodes_[child].parent = -1;
    }

    void moveTo(int v, int x, int y) {
        nodes_[v].x = x;
        nodes_[v].y = y;
    }

    // ---- debug ----
    std::string name(int v) const {
        if (v == 0) return "SRC";
        if (v <= num_sinks_) return "S" + std::to_string(v);
        return "buf" + std::to_string(v);
    }

    void print() const {
        for (int v = 0; v < size(); ++v) {
            const Node& n = nodes_[v];
            std::cout << name(v) << " (" << n.x << "," << n.y << ")";
            if (n.parent >= 0) std::cout << " parent=" << name(n.parent);
            if (!n.children.empty()) {
                std::cout << " children={";
                for (int c : n.children) std::cout << " " << name(c);
                std::cout << " }";
            }
            std::cout << "\n";
        }
    }

private:
    std::vector<Node> nodes_;     // 外面看不到、改不到
    int num_sinks_ = 0;
};
```

在 `main` 裡測試，把第 1 課的三行 `cout` 換掉：

```cpp
    Tree T(P);                                   // 呼叫建構子
    int b1 = T.addBuffer(60, 61);
    int b2 = T.addBuffer(77, 38);
    T.link(b1, 0);
    T.link(b2, 0);
    T.link(1, b1); T.link(3, b1); T.link(5, b1);
    T.link(2, b2); T.link(4, b2);
    T.print();

    Tree backup = T;                             // 整棵複製
    std::cout << "--- 把 S5 換到 B2 ---\n";
    T.link(5, b2);                               // 不用先 unlink，link 會自動處理
    T.print();
    std::cout << "--- 還原 ---\n";
    T = backup;
    std::cout << "S5 parent = " << T.name(T.node(5).parent) << "\n";
```

我實測的結果：換完之後 `S5 parent=buf7`，還原後印出 `S5 parent = buf6`。

---

## 物件導向的觀念（對照上面的程式）

### 1. `class` = 資料 + 操作資料的函式

之前是「`struct Tree` 放資料」加上「外面一堆函式 `link(T, ...)`」。現在函式搬進 class 裡，叫做**成員函式**，呼叫方式也跟著變了：

| 之前 | 現在 |
|---|---|
| `Tree T = makeTree(P);` | `Tree T(P);` |
| `addBuffer(T, 60, 61)` | `T.addBuffer(60, 61)` |
| `link(T, 1, b1)` | `T.link(1, b1)` |
| `T.nodes[5].parent` | `T.node(5).parent` |

成員函式本來就屬於某個物件，所以**不用再把 `T` 傳進去**，函式裡直接用 `nodes_` 就是「自己的」節點。

### 2. `public` 和 `private`：封裝

- **`public:`** 底下的東西外面可以用，這是 class 對外的「操作介面」。
- **`private:`** 底下的東西只有 class 自己的函式能碰。

為什麼要藏起來？如果 `nodes_` 是公開的，你某天可能在最佳化程式裡順手寫 `T.nodes_[5].parent = 7;`，**只改了一邊**，樹就壞了，而且很難查出是哪裡壞的。設成 `private` 之後，這行**根本編譯不過**。我實際試過，編譯器會直接擋下來：

```
error: 'std::vector<Node> Tree::nodes_' is private within this context
```

外面想改樹，**只能透過 `link`、`unlink`、`addBuffer`、`moveTo`**，而這些函式保證兩邊一起改。這就是封裝的意義：**讓錯誤的用法寫不出來**。

### 3. 建構子（constructor）

```cpp
explicit Tree(const Problem& P) { ... }
```

- **名字和 class 相同、沒有回傳型態**的函式就是建構子，物件「誕生」的那一刻會自動執行。
- 寫 `Tree T(P);` 時，它會自動把 SRC 和 sinks 放好，不可能出現「忘了初始化的樹」。
- **`explicit`：** 防止 C++ 偷偷把 `Problem` 自動轉成 `Tree`，算是好習慣，先記得加上就好。

### 4. `const` 成員函式

```cpp
long long dist(int a, int b) const { ... }
```

- **函式後面的 `const`** 是一個承諾：「這個函式只看不改」。
- `evaluate` 會拿到 `const Tree& T`（唯讀的樹），這時**只能呼叫有 `const` 的函式**。不小心在 `evaluate` 裡呼叫 `T.link(...)`，編譯器就會報錯。
- **`node(v)` 回傳 `const Node&`：** 讓外面能讀到 parent、children、座標，但不能透過它修改。

### 5. 命名慣例：`nodes_` 後面的底線

這只是慣例，不是語法：**底線結尾代表 private 成員變數**，讀程式時一眼就分得出「這是 class 自己的資料」還是區域變數。

### 6. 附帶的改進：`link` 會自動 `unlink`

```cpp
if (nodes_[child].parent >= 0) unlink(child);
```

之前要記得「先 unlink 再 link」，忘了就出錯。現在 class 自己處理，**呼叫的人不可能用錯**。這也是物件導向的好處：把規則寫在 class 裡面，只要寫一次。

### 7. 複製依然只要一行

`Tree backup = T;` 和 `T = backup;` 照樣能用。C++ 會自動複製所有成員（包含 private 的），因為我們用的是 vector + index，複製出來的樹完全獨立。這點用指標的話就辦不到。

---

## 接下來 `evaluate` 要怎麼寫

`evaluate` 和 `cheapestType` 一樣是獨立函式（它們是「評分」，不是樹本身的操作），只是取樹的資料要改成透過成員函式：

| 第 2 課的寫法 | 物件導向版 |
|---|---|
| `T.nodes.size()` | `T.size()` |
| `T.num_sinks` | `T.numSinks()` |
| `T.nodes[v]` | `T.node(v)` |
| `dist(T.nodes[u], T.nodes[c])` | `T.dist(u, c)` |

例如 BFS 那段會變成：

```cpp
for (int c : T.node(u).children) {
    arrival[c] = arrival[u] + T.dist(u, c);
    queue.push_back(c);
}
```

先把 `Tree` 貼上去、跑一次上面的測試。確認輸出正確後，再照這張對照表寫第 2 課的 `cheapestType` 和 `evaluate`，有問題截圖給我。

---

# 我

把 print tree 刪掉

---

# Claude

好，`print()` 刪掉了。`name()` 只有 `print()` 在用，所以也一起刪掉，避免留下用不到的程式碼。

**注意：** 你的 `main.cpp` 目前還沒有貼上 `Tree`（我看了一下，裡面只有第 1 課的內容），所以直接貼下面這份就好。我實際編譯並跑過，沒有 warning。

## `Tree` 類別（不含 print）

最上面的 `#include` 加兩行：

```cpp
#include <algorithm>
#include <cstdlib>
```

貼在 `readProblem` 之後、`main` 之前：

```cpp
// =====================================================================
// Tree 類別：vector + index
//   編號 0 = SRC，1..N = sinks S1..SN，N+1.. = buffers
// =====================================================================
enum Kind { SRC = 0, SINK = 1, BUF = 2 };

struct Node {
    int x = 0, y = 0;
    int parent = -1;              // -1 = 沒有 parent
    int kind = BUF;
    std::vector<int> children;
};

class Tree {
public:
    // 建構子：建立只有 SRC 和 sinks 的樹
    explicit Tree(const Problem& P) {
        num_sinks_ = (int)P.sink_x.size();
        nodes_.resize(num_sinks_ + 1);
        nodes_[0].x = P.src_x;
        nodes_[0].y = P.src_y;
        nodes_[0].kind = SRC;
        for (int i = 1; i <= num_sinks_; ++i) {
            nodes_[i].x = P.sink_x[i - 1];
            nodes_[i].y = P.sink_y[i - 1];
            nodes_[i].kind = SINK;
        }
    }

    // ---- 讀取（const：只看不改）----
    int size() const { return (int)nodes_.size(); }
    int numSinks() const { return num_sinks_; }
    const Node& node(int v) const { return nodes_[v]; }

    long long dist(int a, int b) const {
        return std::llabs((long long)nodes_[a].x - nodes_[b].x) +
               std::llabs((long long)nodes_[a].y - nodes_[b].y);
    }

    // ---- 修改（只有這幾個函式能改樹）----
    int addBuffer(int x, int y) {
        Node b;
        b.x = x;
        b.y = y;
        b.kind = BUF;
        nodes_.push_back(b);
        return (int)nodes_.size() - 1;
    }

    void link(int child, int parent) {
        if (nodes_[child].parent >= 0) unlink(child);   // 已經有 parent → 先拔掉
        nodes_[parent].children.push_back(child);
        nodes_[child].parent = parent;
    }

    void unlink(int child) {
        int p = nodes_[child].parent;
        if (p < 0) return;
        std::vector<int>& ch = nodes_[p].children;
        for (int i = 0; i < (int)ch.size(); ++i) {
            if (ch[i] == child) {
                ch[i] = ch.back();
                ch.pop_back();
                break;
            }
        }
        nodes_[child].parent = -1;
    }

    void moveTo(int v, int x, int y) {
        nodes_[v].x = x;
        nodes_[v].y = y;
    }

private:
    std::vector<Node> nodes_;
    int num_sinks_ = 0;
};
```

## `main` 裡的測試（不用 print）

沒有 `print()` 之後，改成直接印出幾個關鍵數字來檢查。把第 1 課的三行 `cout` 換成：

```cpp
    Tree T(P);
    int b1 = T.addBuffer(60, 61);
    int b2 = T.addBuffer(77, 38);
    T.link(b1, 0);
    T.link(b2, 0);
    T.link(1, b1); T.link(3, b1); T.link(5, b1);
    T.link(2, b2); T.link(4, b2);
    std::cout << "nodes=" << T.size() << " sinks=" << T.numSinks() << "\n";
    std::cout << "S5 parent=" << T.node(5).parent << " buf6 children=" << T.node(b1).children.size() << "\n";

    T.link(5, b2);   // 把 S5 換到 B2
    std::cout << "S5 parent=" << T.node(5).parent << " buf6 children=" << T.node(b1).children.size()
              << " buf7 children=" << T.node(b2).children.size() << "\n";
```

編譯並執行：

```bash
cmake --build build && ./build/cbi ../testcases/input001.cbi out.cbi
```

我實測的輸出：

```
nodes=8 sinks=5
S5 parent=6 buf6 children=3
S5 parent=7 buf6 children=2 buf7 children=3
```

怎麼看這三行：
- **第 1 行：** 1 個 SRC + 5 個 sinks + 2 個 buffers = 8 個節點。
- **第 2 行：** S5 接在節點 6（第一個 buffer）底下，它有 3 個 children（S1、S3、S5）。
- **第 3 行：** 換完之後，S5 的 parent 變成 7；buf6 剩 2 個，buf7 變成 3 個，**兩邊同時更新了**。

數字一樣就代表 `Tree` 沒問題，接著寫第 2 課的 `cheapestType` 和 `evaluate`。

---

# 我

完成了 接下來 怎麼做

---

# Claude

你的 `Tree` 類別已經貼好了，可以正常編譯。有一個小地方要補：最上面還沒加 `#include <algorithm>` 和 `#include <cstdlib>`。現在能編譯，是因為別的標頭檔剛好間接帶進來了，換一台機器（例如評分機）可能就編譯失敗，所以請補上。

接下來是**第 2 課：幫樹打分數**。前面講過觀念，這次直接給你配合 `Tree` 類別的版本。下面的程式我接在你目前的檔案後面實際跑過，結果全部正確。

---

## 這一課要做什麼

寫一個函式 `evaluate`，給它一棵樹，它回答：
1. **這棵樹合法嗎？** 不合法的話，原因是什麼？
2. **分數多少？** T_max、T_min、buffer 總成本、Score。

之後每一課都會用它：建完樹檢查合不合法，最佳化時比較哪棵比較好。

## 步驟 1：`cheapestType`（選最便宜的型號）

**觀念：** buffer 的型號不用自己挑。知道它有幾個 children（k）、到所有 children 的總距離（len）之後，「撐得住的型號裡最便宜的」就是答案。

貼在 `class Tree` 的 `};` 之後：

```cpp
// =====================================================================
// 第 2 課：型號選擇與評分
// =====================================================================

// 能撐住「k 個 children、總長 len」的型號中，cost 最小的 index；都撐不住回傳 -1
int cheapestType(const Problem& P, int k, long long len) {
    int best = -1;
    for (int i = 0; i < (int)P.buflib.size(); ++i) {
        const Bufferlibrary& b = P.buflib[i];
        if (b.fanout < k || b.length < len) continue;              // 撐不住，跳過
        if (best == -1 || b.cost < P.buflib[best].cost) best = i;  // 第一個找到的，或更便宜的
    }
    return best;
}
```

**這是「找最小值」的標準寫法：** 用 `best = -1` 代表「還沒找到」，看到第一個合格的就先記下來，之後遇到更好的再替換。

## 步驟 2：`evaluate`（檢查 + 計分）

接在 `cheapestType` 後面：

```cpp
struct Eval {
    bool legal = false;     // 合不合法
    std::string why;        // 不合法的原因
    long long tmax = 0, tmin = 0, cost = 0, score = 0;
};

Eval evaluate(const Problem& P, const Tree& T) {
    Eval e;
    const int n = T.size();

    // 規則 1：所有座標不能重疊
    //   (x, y) 壓成一個數字 → 排序 → 相鄰相同 = 重疊
    std::vector<long long> keys;
    for (int v = 0; v < n; ++v)
        keys.push_back(((long long)T.node(v).x << 32) | (unsigned int)T.node(v).y);
    std::sort(keys.begin(), keys.end());
    for (int i = 1; i < n; ++i) {
        if (keys[i] == keys[i - 1]) { e.why = "overlapping coordinates"; return e; }
    }

    // 規則 2：每個 buffer 嚴格在晶片內部、至少 1 個 child、找得到型號
    for (int v = T.numSinks() + 1; v < n; ++v) {
        const Node& b = T.node(v);
        if (b.x <= 0 || b.x >= P.dimx || b.y <= 0 || b.y >= P.dimy) { e.why = "buffer outside chip"; return e; }
        if (b.children.empty()) { e.why = "buffer without children"; return e; }
        long long len = 0;
        for (int c : b.children) len += T.dist(v, c);
        int t = cheapestType(P, (int)b.children.size(), len);
        if (t == -1) { e.why = "buffer over fanout/length limit"; return e; }
        e.cost += P.buflib[t].cost;
    }

    // 規則 3：SRC 用 .limit 的限制
    long long srcLen = 0;
    for (int c : T.node(0).children) srcLen += T.dist(0, c);
    if ((int)T.node(0).children.size() > P.fanout || srcLen > P.length) { e.why = "SRC over limit"; return e; }

    // 規則 4：從 SRC 做 BFS，每個節點都要走到；順便算 arrival
    std::vector<long long> arrival(n, 0);
    std::vector<int> queue;
    queue.push_back(0);
    for (int i = 0; i < (int)queue.size(); ++i) {
        int u = queue[i];
        for (int c : T.node(u).children) {
            arrival[c] = arrival[u] + T.dist(u, c);   // 子 = 父 + 邊長
            queue.push_back(c);
        }
    }
    if ((int)queue.size() != n) { e.why = "some nodes unreachable"; return e; }

    // 計分
    e.tmax = arrival[1];
    e.tmin = arrival[1];
    for (int s = 1; s <= T.numSinks(); ++s) {
        e.tmax = std::max(e.tmax, arrival[s]);
        e.tmin = std::min(e.tmin, arrival[s]);
    }
    e.score = e.cost + P.w_skew * (e.tmax - e.tmin);
    e.legal = true;
    return e;
}
```

### 兩個重點

**1. BFS 為什麼算得出 arrival。** 以範例的樹為例：

```
queue: [SRC]                    → 處理 SRC：算出 B1、B2 的 arrival，排進 queue
queue: [SRC, B1, B2]            → 處理 B1：算出 S1、S3、S5
queue: [SRC, B1, B2, S1, S3, S5] → 處理 B2：算出 S2、S4
...
```

處理一個節點時，它的 parent 一定**更早**處理過，所以 `arrival[u]` 已經算好，子節點只要「父 + 邊長」就好。

最後如果 `queue` 的長度比節點總數少，代表有節點沒接到 SRC 上。

**2. `const Tree& T` 的保護。** `evaluate` 拿到的是唯讀的樹，只能呼叫 `size()`、`node()`、`dist()` 這些有 `const` 的函式。如果你不小心在這裡寫 `T.link(...)`，編譯器會直接擋下來。這就是上一課物件導向設計的好處。

## 步驟 3：自我測試

接在 `evaluate` 後面。它會手動建出 PDF 範例 output1 的樹，再**故意弄壞三次**，確認每種錯誤都會被抓到：

```cpp
int selfTest(const Problem& P) {
    Tree T(P);
    int b1 = T.addBuffer(60, 61);
    int b2 = T.addBuffer(77, 38);
    T.link(b1, 0);
    T.link(b2, 0);
    T.link(1, b1); T.link(3, b1); T.link(5, b1);   // B1:{S1 S3 S5}
    T.link(2, b2); T.link(4, b2);                   // B2:{S2 S4}

    std::cout << "cheapestType: " << cheapestType(P, 3, 80) << " " << cheapestType(P, 3, 100) << " "
              << cheapestType(P, 7, 50) << " " << cheapestType(P, 10, 50) << "   (應該是 0 1 2 -1)\n";

    Eval e = evaluate(P, T);
    std::cout << "legal=" << e.legal << " T_max=" << e.tmax << " T_min=" << e.tmin
              << " cost=" << e.cost << " Score=" << e.score << " " << e.why << "\n";
    std::cout << "應該是 legal=1 T_max=141 T_min=36 cost=26 Score=131\n";

    // 故意弄壞（每次都從正確的樹複製一份來改）
    Tree bad = T;
    bad.moveTo(b1, 0, 61);
    std::cout << "B1 移到邊界: " << evaluate(P, bad).why << "\n";

    bad = T;
    bad.unlink(4);
    std::cout << "S4 沒接上:   " << evaluate(P, bad).why << "\n";

    bad = T;
    bad.moveTo(b2, 72, 27);
    std::cout << "B2 疊在 S4:  " << evaluate(P, bad).why << "\n";
    return 0;
}
```

## 步驟 4：改 `main`

把整個 `main` 換成下面這樣，順便修好 `"cannot read"` 少空白的問題：

```cpp
int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "error: missing arguments\n";
        std::cerr << "usage: " << argv[0] << " INPUT_FILE OUTPUT_FILE\n";
        return 1;
    }
    Problem P;
    if (!readProblem(argv[1], P)) {
        std::cerr << "error: cannot read " << argv[1] << "\n";
        return 1;
    }
    if (argc >= 4 && std::string(argv[3]) == "--selftest") return selfTest(P);
    return 0;
}
```

## 檢查點

```bash
cmake --build build && ./build/cbi ../testcases/input001.cbi out.cbi --selftest
```

我實測的輸出：

```
cheapestType: 0 1 2 -1   (應該是 0 1 2 -1)
legal=1 T_max=141 T_min=36 cost=26 Score=131
應該是 legal=1 T_max=141 T_min=36 cost=26 Score=131
B1 移到邊界: buffer outside chip
S4 沒接上:   some nodes unreachable
B2 疊在 S4:  overlapping coordinates
```

- **第 2、3 行一模一樣：** 你的計分和 PDF 範例的 `T_max: 141, T_min: 36, Score: 131` 完全吻合。
- **最後三行：** 三種錯誤都被正確抓到。

## 之後的進度

| 課 | 內容 | 完成後你會有 |
|---|---|---|
| **2（現在）** | `evaluate` 計分 | 能判斷任何一棵樹的好壞 |
| 2B | `writeOutput` 寫輸出檔 | 能交給 TA 的 verifier 檢查 |
| 3 | `buildInitial` 由下往上分群建樹 | **第一個能交的版本**（全部測資 VALID） |
| 4 | `optimize` hill climbing | 分數變低 |

跑出上面的結果後告訴我，接著做 2B：寫輸出檔。

---

# 我



<pasted_content id="31f0">
(eda) evan@evan-linux:~/Desktop/NTUST/105_1/Class/Computer_Aided_Design_for_VLSI_System/Lab2/M11502102$ cmake --build build && ./build/cbi ../testcases/input001.cbi out.cbi --selftest
[ 50%] Building CXX object CMakeFiles/cbi.dir/src/main.cpp.o
[100%] Linking CXX executable cbi
[100%] Built target cbi
cheapestType: 0 1 2 -1   (應該是 0 1 2 -1)
legal=1 T_max=141 T_min=36 cost=26 Score=131
應該是 legal=1 T_max=141 T_min=36 cost=26 Score=131
B1 移到邊界: buffer outside chip
S4 沒接上:   some nodes unreachable
B2 疊在 S4:  overlapping coordinates
</pasted_content id="31f0">


---

# Claude

第 2 課完成！🎉 你的輸出和預期完全一樣：
- **計分正確：** 141 / 36 / 131 和 PDF 範例吻合；
- **抓錯正確：** 三種故意弄壞的樹都抓到了。

到這裡，你的程式已經能**判斷任何一棵樹的好壞**。

---

# 第 2B 課：把樹寫成輸出檔

TA 的評分程式只看你寫出來的檔案，所以格式一個字都不能錯。這一課的程式我實際跑過，**輸出和 PDF 範例的 output1 一字不差，TA 的 verifier 也判定 VALID**。

## 輸出檔的格式

```
.buffer 2
B1 MEDIUM 60 61
B2 SMALL 77 38
.e

.level 2
1 SRC:{B1 B2}
2 B1:{S1 S3 S5} B2:{S2 S4}
.e
```

| 區塊 | 內容 | verifier 的嚴格規定 |
|---|---|---|
| `.buffer m` | 每行：名字、型號、x、y | 名字必須是 **B1、B2…Bm 連續、依序** |
| `.level L` | 第 k 行：`k parent:{children}` | **第 k 行列出的 child，深度必須剛好是 k**；L 必須等於最大深度 |

## 要解決的兩個問題

**問題 1：buffer 要重新編號。** 你樹裡的 buffer 編號是 6、7…（從 N+1 開始），還可能因為之後刪掉 buffer 而不連續。輸出時要改名成 B1、B2… **連續的名字**。

**問題 2：要知道每個節點的深度。** SRC 深度 0，它的 children 深度 1，依此類推。`.level` 的第 k 行要列出「深度 k−1、而且有 children 的節點」。

**兩個問題都用 BFS 一次解決**，和 `evaluate` 一樣的走法，只是這次記的是深度，不是 arrival。

## 程式碼

把你原本的 `selfTest` 和 `main` **整段刪掉**，換成下面這些（`writeOutput` + 新的 `selfTest` + 新的 `main`）：

```cpp
// =====================================================================
// 第 2B 課：寫輸出檔
// =====================================================================
bool writeOutput(const Problem& P, const Tree& T, const std::string& path) {
    const int n = T.size();

    // 1. BFS：得到「由上往下」的順序 order，以及每個節點的深度
    std::vector<int> order;
    std::vector<int> depth(n, 0);
    order.push_back(0);
    for (int i = 0; i < (int)order.size(); ++i) {
        int u = order[i];
        for (int c : T.node(u).children) {
            depth[c] = depth[u] + 1;
            order.push_back(c);
        }
    }

    // 2. 決定每個節點輸出的名字；buffer 依 BFS 順序重新編號成 B1, B2, ...
    std::vector<std::string> name(n);
    name[0] = "SRC";
    for (int s = 1; s <= T.numSinks(); ++s) name[s] = "S" + std::to_string(s);
    int numBuf = 0;
    for (int v : order) {
        if (T.node(v).kind == BUF) {
            ++numBuf;
            name[v] = "B" + std::to_string(numBuf);
        }
    }

    int maxDepth = 0;
    for (int v : order) maxDepth = std::max(maxDepth, depth[v]);

    std::ofstream out(path);       // 打開檔案準備寫入
    if (!out) return false;

    // 3. .buffer 區塊：照 B1, B2, ... 的順序（也就是 BFS 順序）
    out << ".buffer " << numBuf << "\n";
    for (int v : order) {
        const Node& b = T.node(v);
        if (b.kind != BUF) continue;
        long long len = 0;
        for (int c : b.children) len += T.dist(v, c);
        int t = cheapestType(P, (int)b.children.size(), len);
        out << name[v] << " " << P.buflib[t].type << " " << b.x << " " << b.y << "\n";
    }
    out << ".e\n\n";

    // 4. .level 區塊：第 k 行 = 所有「深度 k-1 且有 children」的節點
    out << ".level " << maxDepth << "\n";
    for (int k = 1; k <= maxDepth; ++k) {
        out << k;
        for (int v : order) {
            if (depth[v] != k - 1 || T.node(v).children.empty()) continue;
            out << " " << name[v] << ":{";
            const std::vector<int>& ch = T.node(v).children;
            for (int j = 0; j < (int)ch.size(); ++j) {
                if (j > 0) out << " ";      // 第一個前面不加空白
                out << name[ch[j]];
            }
            out << "}";
        }
        out << "\n";
    }
    out << ".e\n";
    return true;
}

int selfTest(const Problem& P, const std::string& outPath) {
    Tree T(P);
    int b1 = T.addBuffer(60, 61);
    int b2 = T.addBuffer(77, 38);
    T.link(b1, 0);
    T.link(b2, 0);
    T.link(1, b1); T.link(3, b1); T.link(5, b1);
    T.link(2, b2); T.link(4, b2);
    Eval e = evaluate(P, T);
    std::cout << "legal=" << e.legal << " T_max=" << e.tmax << " T_min=" << e.tmin << " Score=" << e.score << "\n";
    if (!writeOutput(P, T, outPath)) {
        std::cerr << "error: cannot write " << outPath << "\n";
        return 1;
    }
    std::cout << "wrote " << outPath << "\n";
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "error: missing arguments\n";
        std::cerr << "usage: " << argv[0] << " INPUT_FILE OUTPUT_FILE\n";
        return 1;
    }
    Problem P;
    if (!readProblem(argv[1], P)) {
        std::cerr << "error: cannot read " << argv[1] << "\n";
        return 1;
    }
    if (argc >= 4 && std::string(argv[3]) == "--selftest") return selfTest(P, argv[2]);
    return 0;
}
```

## 重點解說

**1. 為什麼「依 BFS 順序」編號一定合法**

BFS 是一層一層往下走的，所以：
- `order` 裡的 buffer 依序拿到 1、2、3…，**保證連續、沒有空號**；
- 就算你的樹裡 buffer 編號是 6、9、15（中間的被刪掉了），輸出還是乾淨的 B1、B2、B3。

**2. 為什麼只有 buffer 要改名，sink 不用**

sink 的編號 1..N 本來就對應 S1..SN，不會變。buffer 則是演算法自己產生的，編號不固定。

**3. `std::to_string`**

把數字轉成字串，才能和 `"B"` 接起來：`"B" + std::to_string(3)` 會變成 `"B3"`。

**4. `ofstream` 和 `cout` 用法一樣**

`std::ofstream out(path);` 打開檔案之後，`out << ...` 就像 `std::cout << ...`，只是寫進檔案而不是螢幕。函式結束時，檔案會**自動關閉並存檔**。

**5. children 之間的空白**

`if (j > 0) out << " ";` 讓輸出是 `{S1 S3 S5}`，而不是 `{ S1 S3 S5}` 或 `{S1 S3 S5 }`。verifier 其實能容忍多餘的空白，但照格式寫最保險。

## 檢查點

```bash
cmake --build build && ./build/cbi ../testcases/input001.cbi out.cbi --selftest && cat out.cbi
```

應該印出：

```
legal=1 T_max=141 T_min=36 Score=131
wrote out.cbi
.buffer 2
B1 MEDIUM 60 61
B2 SMALL 77 38
.e

.level 2
1 SRC:{B1 B2}
2 B1:{S1 S3 S5} B2:{S2 S4}
.e
```

接著用 TUTORIAL 附的檢查工具驗證，它會逐條列出每條邊、每個 buffer 的用量。在 `M11502102/` 裡執行：

```bash
python ../tools/cbi_explain.py ../testcases/input001.cbi out.cbi
```

最後一行要看到：

```
Score = cost + w_skew*skew = 26 + 1*105 = 131
constraints: ALL OK   (declared .level = 2)
```

看到 `ALL OK` 就代表**你的輸出檔格式完全正確**。

---

## 下一課：第 3 課，第一個能交的版本

到目前為止，樹都是我們「手動」建的。第 3 課要讓程式**自己**從 sinks 開始，由下往上分群、放 buffer，建出一棵合法的樹：

```
所有 sinks → 鄰近的分成一群 → 每群放一個 buffer → buffers 再分群 → ... → 接到 SRC
```

做完之後，你的程式就能跑**全部 15 筆測資**並全部 VALID，這就是第一個可以交的版本。跑出 `ALL OK` 之後告訴我。

---

# 我

把樹寫成輸出檔 寫在樹裡

---

# Claude

好，把它改成 `Tree` 的成員函式 `write`。我用你目前的檔案實際改過一份並跑過：**輸出和 PDF 範例一字不差，`cbi_explain` 回報 `ALL OK`**。

改動分三步。

## 步驟 1：把 `cheapestType` 搬到 `class Tree` 上面

`write` 寫出 buffer 型號時要呼叫 `cheapestType`。但 **C++ 從上往下讀**，你的 `cheapestType` 現在寫在 `class Tree` **後面**（第 157 行），在 class 裡面呼叫它的話，編譯器會說找不到。

**做法：** 把整個 `cheapestType` 函式（第 157～165 行）**剪下**，貼到 `enum Kind` 那一行（第 85 行）的**上面**。

```cpp
bool readProblem(...) { ... }

int cheapestType(const Problem& P, int k, long long len) {   // ← 搬到這裡
    ...
}

enum Kind { SRC = 0, SINK = 1, BUF = 2 };
...
class Tree { ... };
```

這樣放也比較合理：`cheapestType` 只用到 `Problem`（查型號表），和樹無關，本來就該放在樹之前。

## 步驟 2：在 `class Tree` 裡加入 `write`

貼在 `moveTo` 函式之後、`private:` 之前：

```cpp
    // 把樹寫成輸出檔（const：只讀樹，不會改到它）
    bool write(const Problem& P, const std::string& path) const {
        const int n = size();

        // 1. BFS：由上往下的順序 order，以及每個節點的深度
        std::vector<int> order;
        std::vector<int> depth(n, 0);
        order.push_back(0);
        for (int i = 0; i < (int)order.size(); ++i) {
            int u = order[i];
            for (int c : nodes_[u].children) {
                depth[c] = depth[u] + 1;
                order.push_back(c);
            }
        }

        // 2. 輸出的名字：buffer 依 BFS 順序重新編號成 B1, B2, ...
        std::vector<std::string> name(n);
        name[0] = "SRC";
        for (int s = 1; s <= num_sinks_; ++s) name[s] = "S" + std::to_string(s);
        int numBuf = 0;
        for (int v : order) {
            if (nodes_[v].kind == BUF) {
                ++numBuf;
                name[v] = "B" + std::to_string(numBuf);
            }
        }

        int maxDepth = 0;
        for (int v : order) maxDepth = std::max(maxDepth, depth[v]);

        std::ofstream out(path);
        if (!out) return false;

        // 3. .buffer 區塊
        out << ".buffer " << numBuf << "\n";
        for (int v : order) {
            const Node& b = nodes_[v];
            if (b.kind != BUF) continue;
            long long len = 0;
            for (int c : b.children) len += dist(v, c);
            int t = cheapestType(P, (int)b.children.size(), len);
            out << name[v] << " " << P.buflib[t].type << " " << b.x << " " << b.y << "\n";
        }
        out << ".e\n\n";

        // 4. .level 區塊：第 k 行 = 所有「深度 k-1 且有 children」的節點
        out << ".level " << maxDepth << "\n";
        for (int k = 1; k <= maxDepth; ++k) {
            out << k;
            for (int v : order) {
                if (depth[v] != k - 1 || nodes_[v].children.empty()) continue;
                out << " " << name[v] << ":{";
                const std::vector<int>& ch = nodes_[v].children;
                for (int j = 0; j < (int)ch.size(); ++j) {
                    if (j > 0) out << " ";
                    out << name[ch[j]];
                }
                out << "}";
            }
            out << "\n";
        }
        out << ".e\n";
        return true;
    }
```

### 和寫在外面的版本差在哪

| 寫在外面 `writeOutput(P, T, path)` | 寫在 class 裡 `T.write(P, path)` |
|---|---|
| `T.size()` | `size()`（呼叫自己的成員函式） |
| `T.numSinks()` | `num_sinks_`（直接讀自己的 private 資料） |
| `T.node(v)` | `nodes_[v]` |
| `T.dist(v, c)` | `dist(v, c)` |

成員函式**本來就屬於這棵樹**，所以不用寫 `T.`，也可以直接碰 `private` 的 `nodes_`。

**函式後面的 `const`：** 寫檔只是「讀」樹，不會改它。加上 `const` 之後：
- 如果你在 `write` 裡不小心呼叫 `link()` 改到樹，編譯器會擋下來；
- `const Tree&` 的樹（例如 `evaluate` 拿到的那種）也可以呼叫 `write`。

## 步驟 3：換掉 `selfTest` 和 `main`

把你原本的 `selfTest`（第 222 行開始）和 `main` 整段換成：

```cpp
int selfTest(const Problem& P, const std::string& outPath) {
    Tree T(P);
    int b1 = T.addBuffer(60, 61);
    int b2 = T.addBuffer(77, 38);
    T.link(b1, 0);
    T.link(b2, 0);
    T.link(1, b1); T.link(3, b1); T.link(5, b1);
    T.link(2, b2); T.link(4, b2);
    Eval e = evaluate(P, T);
    std::cout << "legal=" << e.legal << " T_max=" << e.tmax << " T_min=" << e.tmin << " Score=" << e.score << "\n";
    if (!T.write(P, outPath)) {                      // ← 樹自己寫出檔案
        std::cerr << "error: cannot write " << outPath << "\n";
        return 1;
    }
    std::cout << "wrote " << outPath << "\n";
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "error: missing arguments\n";
        std::cerr << "usage: " << argv[0] << " INPUT_FILE OUTPUT_FILE\n";
        return 1;
    }
    Problem P;
    if (!readProblem(argv[1], P)) {
        std::cerr << "error: cannot read " << argv[1] << "\n";
        return 1;
    }
    if (argc >= 4 && std::string(argv[3]) == "--selftest") return selfTest(P, argv[2]);
    return 0;
}
```

`selfTest` 現在多了一個參數 `outPath`，所以 `main` 裡要傳 `argv[2]` 給它。

## 檢查點

```bash
cmake --build build && ./build/cbi ../testcases/input001.cbi out.cbi --selftest && cat out.cbi
```

應該印出：

```
legal=1 T_max=141 T_min=36 Score=131
wrote out.cbi
.buffer 2
B1 MEDIUM 60 61
B2 SMALL 77 38
.e

.level 2
1 SRC:{B1 B2}
2 B1:{S1 S3 S5} B2:{S2 S4}
.e
```

再用工具驗證：

```bash
python ../tools/cbi_explain.py ../testcases/input001.cbi out.cbi
```

最後一行看到 `constraints: ALL OK   (declared .level = 2)` 就完成了。

**如果編譯出現 `'cheapestType' was not declared in this scope`**，代表步驟 1 的 `cheapestType` 還在 `class Tree` 下面，要搬到上面。

完成後我們進第 3 課：讓程式自己建樹。

---

# 我



<pasted_content id="31f0">
(eda) evan@evan-linux:~/Desktop/NTUST/105_1/Class/Computer_Aided_Design_for_VLSI_System/Lab2/M11502102$ cmake --build build && ./build/cbi ../testcases/input001.cbi out.cbi --selftest && cat out.cbi
[ 50%] Building CXX object CMakeFiles/cbi.dir/src/main.cpp.o
/home/evan/Desktop/NTUST/105_1/Class/Computer_Aided_Design_for_VLSI_System/Lab2/M11502102/src/main.cpp: In member function ‘bool Tree::write(const Problem&, const std::string&) const’:
/home/evan/Desktop/NTUST/105_1/Class/Computer_Aided_Design_for_VLSI_System/Lab2/M11502102/src/main.cpp:189:21: error: ‘cheapestType’ was not declared in this scope
  189 |             int t = cheapestType(P, (int)b.children.size(), len);
      |                     ^~~~~~~~~~~~
/home/evan/Desktop/NTUST/105_1/Class/Computer_Aided_Design_for_VLSI_System/Lab2/M11502102/src/main.cpp: In function ‘int main(int, char**)’:
/home/evan/Desktop/NTUST/105_1/Class/Computer_Aided_Design_for_VLSI_System/Lab2/M11502102/src/main.cpp:313:75: error: too few arguments to function ‘int selfTest(const Problem&, const std::string&)’
  313 |     if (argc >= 4 && std::string(argv[3]) == "--selftest") return selfTest(P);
      |                                                                   ~~~~~~~~^~~
/home/evan/Desktop/NTUST/105_1/Class/Computer_Aided_Design_for_VLSI_System/Lab2/M11502102/src/main.cpp:283:5: note: declared here
  283 | int selfTest(const Problem& P, const std::string& outPath) {
      |     ^~~~~~~~
gmake[2]: *** [CMakeFiles/cbi.dir/build.make:79: CMakeFiles/cbi.dir/src/main.cpp.o] Error 1
gmake[1]: *** [CMakeFiles/Makefile2:87: CMakeFiles/cbi.dir/all] Error 2
gmake: *** [Makefile:91: all] Error 2
</pasted_content id="31f0">


---

# Claude

兩個錯誤都是漏做了上一則的某個步驟：**步驟 1 沒搬 `cheapestType`**，**步驟 3 沒改 `main`**。我看了你的檔案，各差一處。

## 錯誤 1：`'cheapestType' was not declared in this scope`（第 189 行）

你的檔案現在的順序是：

```
第  85 行  enum Kind
第  94 行  class Tree {
第 189 行      ... cheapestType(...)   ← 在這裡呼叫
           };
第 218 行  int cheapestType(...) {      ← 在這裡才定義
```

**C++ 從上往下讀。** 讀到第 189 行時，`cheapestType` 要到第 218 行才出現，所以編譯器說「沒看過這個名字」。

**改法：**
1. 把第 218 行開始的整個 `cheapestType` 函式（從 `int cheapestType(` 到它的 `}`，大約 9 行）**剪下**；
2. **貼到第 85 行 `enum Kind` 的上面**。

改完的順序應該是：

```
bool readProblem(...) { ... }

int cheapestType(...) { ... }    ← 搬到這裡

enum Kind { ... };
struct Node { ... };
class Tree { ... write 裡呼叫 cheapestType ... };
```

**注意是剪下，不是複製。** 如果兩個地方都留著，會出現新的錯誤 `redefinition of 'int cheapestType(...)'`，意思是同一個函式被定義了兩次。

## 錯誤 2：`too few arguments to function 'selfTest'`（第 313 行）

`selfTest` 已經改成新版、需要兩個參數：

```cpp
int selfTest(const Problem& P, const std::string& outPath)   // 第 283 行：要 P 和 outPath
```

但 `main` 還是舊的呼叫方式，只給了一個：

```cpp
return selfTest(P);                                          // 第 313 行：只給了 P
```

**參數個數要和函式定義的一樣。** 新的 `selfTest` 要知道把檔案寫到哪裡，所以要把輸出檔路徑 `argv[2]` 傳進去。

**改法：** 把第 313 行改成：

```cpp
    if (argc >= 4 && std::string(argv[3]) == "--selftest") return selfTest(P, argv[2]);
```

---

## 讀錯誤訊息的技巧

這兩個錯誤以後還會常常遇到，學會看就能自己修：

| 錯誤訊息 | 意思 | 通常的原因 |
|---|---|---|
| `'X' was not declared in this scope` | 這裡不認識 X | X 拼錯，或 X 寫在使用的地方**後面**，或忘了 `#include` |
| `too few / too many arguments to function` | 參數個數不對 | 改了函式定義，但沒改呼叫的地方 |
| `note: declared here` | 「函式定義在這裡，你去對照看看」 | 跟著上面那條錯誤一起看 |

錯誤訊息的格式是 **`檔名:行號:欄號: error: 原因`**，先看行號找到位置，再看原因。

---

改完再跑一次：

```bash
cmake --build build && ./build/cbi ../testcases/input001.cbi out.cbi --selftest && cat out.cbi
```

看到 `.buffer 2 ... B1 MEDIUM 60 61` 那段輸出就成功了。

---

# 我

那把 這也加到 tree 裡 還是其他程式也會用到

---

# Claude

「這」如果指的是 `cheapestType`：我建議**不要放進 `Tree`**，而是放進 **`Problem`**。原因是其他程式也會用到它，而且它本來就跟樹無關。

## 判斷方法：函式用到誰的資料，就放在誰裡面

看 `cheapestType` 做了什麼：

```cpp
int cheapestType(const Problem& P, int k, long long len) {
    ... P.buflib ...     // 只用到型號表
}
```

它只查 **`P.buflib`（型號表）**，完全沒碰到樹。它回答的問題是：「**k 個 children、總長 len，最便宜要用哪個型號？**」這是題目（型號庫）的知識，不是樹的知識。

## 誰會用到它

| 會用到的地方 | 什麼時候 | 有沒有樹 |
|---|---|---|
| `Tree::write` | 寫輸出檔時決定型號名稱 | 有 |
| `evaluate` | 計算 buffer 總成本 | 有 |
| **第 3 課 建樹** | 判斷「這群點放一個 buffer 撐不撐得住」 | **buffer 還沒放下去** |
| **第 4 課 最佳化** | 判斷「把 sink 移過去，那個 buffer 會不會超過限制」 | 只是在試算 |

第 3、4 課常常是**還沒真的改樹，只是先試算**：「如果放在這裡，總長是 85，有型號撐得住嗎？」這時只需要型號表，放在 `Tree` 裡反而綁手綁腳。

## 改法：變成 `Problem` 的成員函式

**1.** 把 `cheapestType` 整個函式刪掉（你剛搬到 `enum Kind` 上面的那個）。

**2.** 改放進 `struct Problem` 裡面，接在 `sink_x, sink_y` 那行之後、`};` 之前：

```cpp
struct Problem{
    // .limit
    int       fanout = 0;
    ...
    std::vector<int> sink_x, sink_y; // clock sink coordinates

    // 能撐住「k 個 children、總長 len」的型號中 cost 最小的 index；都撐不住回傳 -1
    int cheapestType(int k, long long len) const {
        int best = -1;
        for (int i = 0; i < (int)buflib.size(); ++i) {
            const Bufferlibrary& b = buflib[i];
            if (b.fanout < k || b.length < len) continue;
            if (best == -1 || b.cost < buflib[best].cost) best = i;
        }
        return best;
    }
};
```

和原本的差別：
- **參數少了 `const Problem& P`：** 它本來就在 Problem 裡面，直接用 `buflib` 就好，不用寫 `P.buflib`。
- **後面加 `const`：** 只查表、不改題目。這樣 `const Problem& P` 也能呼叫它（`evaluate` 和 `write` 拿到的都是 const 的 P）。

**3.** 把所有呼叫的地方改掉。共有 3 處：`write` 裡 1 處、`evaluate` 裡 1 處、`selfTest` 裡 4 處（就在同一行）：

| 原本 | 改成 |
|---|---|
| `cheapestType(P, (int)b.children.size(), len)` | `P.cheapestType((int)b.children.size(), len)` |
| `cheapestType(P, 3, 80)` | `P.cheapestType(3, 80)` |

在 vim 裡可以用取代指令一次改完，把 `cheapestType(P, ` 換成 `P.cheapestType(`：

```
:%s/cheapestType(P, /P.cheapestType(/g
```

## 好處：不用再擔心順序

`struct Problem` 寫在檔案最前面，所以 `Tree`、`evaluate`、之後的建樹程式，**全部都在它後面**，一定找得到 `P.cheapestType`。剛才那個 `was not declared in this scope` 的錯誤就不會再發生了。

## 整理：每個東西放哪裡

| 放在 | 內容 | 理由 |
|---|---|---|
| `Problem` | 題目資料 + `cheapestType` | 只跟題目有關 |
| `Tree` | 節點、`link` / `unlink` / `addBuffer` / `moveTo` / `write` | 改樹、輸出樹 |
| 獨立函式 | `readProblem`、`evaluate`、之後的 `buildInitial`、`optimize` | 演算法，同時用到題目和樹 |

改完照樣跑：

```bash
cmake --build build && ./build/cbi ../testcases/input001.cbi out.cbi --selftest && cat out.cbi
```

輸出應該和之前一樣（`.buffer 2`、`B1 MEDIUM 60 61`…）。功能沒有變，只是程式的結構更清楚了。

---

# 我

請直接幫我改正錯的地方且不要註解

---

# Claude

我已經直接改好你的 [main.cpp](Lab2/M11502102/src/main.cpp)，沒有加任何註解。改完編譯沒有任何 error 或 warning，selftest 的輸出和 PDF 範例一樣，`cbi_explain` 也回報 `ALL OK`。

一共改了三處：

| 行 | 原本 | 改成 |
|---|---|---|
| 199（`write` 裡） | `cheapestType(P, (int)b.children.size(), len)` | `P.cheapestType((int)b.children.size(), len)` |
| 251（`evaluate` 裡） | `cheapestType(P, (int)b.children.size(), len)` | `P.cheapestType((int)b.children.size(), len)` |
| 293（`selfTest` 裡） | 行尾有 `// ← 樹自己寫出檔案` | 刪掉了這個註解 |

錯誤的原因：`cheapestType` 已經移進 `Problem` 裡了，但這兩個地方還用舊的寫法呼叫它。

第 293 行那個註解是我之前給你的程式碼裡帶的，你說不要註解，所以一起拿掉了。其他註解都是你自己寫的，我沒動。

下一步是第 3 課：讓程式自己建出合法的樹，這樣 15 筆測資就都能跑了。準備好了跟我說。

---

# 我

好了

---

# Claude

15 筆正式測資都通過 TA verifier 了；我正在用 280 多筆極端測資壓力測試，有 5 筆失敗，先查原因再給你程式碼。

---

# Claude

第 3 課的程式我已經在你目前的程式碼上實際寫好並測試過：
- **15 筆正式測資**全部通過 TA verifier（全部 VALID），1 萬個 sinks 只要 0.02 秒。
- **283 筆極端測資**（很小的晶片、很短的 L、很弱的 SRC）中，有 278 筆 VALID。失敗的 5 筆，連目前最強的 Ultracode_3 也建不出來，應該是題目本身無解。例如有一筆的 SRC 在角落 (0,4)、length 只有 2，晶片內部距離 SRC ≤ 2 的位置只有一個，又剛好被 sink 佔走了。

照你的偏好，程式碼裡不放註解，說明都寫在外面。

---

# 第 3 課：讓程式自己建出合法的樹

## 3-1. 演算法：由下往上分群

想像把所有 sinks 一層一層「打包」：

```
第 1 輪：  S1 S3   S2 S4 S5   S6 S7 ...      ← 相鄰的 sinks 分成一群
            \/       \|/       \/
           buf      buf       buf           ← 每群放一個 buffer 當 parent
第 2 輪：   buf  buf  buf  ...               ← 這些 buffer 變成下一輪要分群的對象
              \   |   /
               buf
...
最後：         SRC                          ← 剩下的少到 SRC 接得住，就接上 SRC
```

**`frontier`**（前線）＝「目前還沒有 parent 的節點」。一開始是所有 sinks，每一輪：

1. **SRC 接得住嗎？** 數量 ≤ SRC 的 fanout，而且到 SRC 的距離總和 ≤ SRC 的 length，就全部接上 SRC，結束。
2. **排序：** 把 frontier 依空間位置排序，讓相鄰的點排在一起。
3. **貪心分群：** 從頭開始，一群一直加下一個點，直到「找不到合法的 buffer 位置」為止。
4. **放 buffer：** 每群（≥ 2 個點）放一個 buffer 當 parent，新 buffer 進入下一輪的 frontier；只有 1 個點的群，直接留到下一輪。
5. **都合不起來時：** 點彼此太遠，一群都組不成。這時幫每個點往 SRC 方向插一個 **relay buffer**，讓它更靠近 SRC，下一輪再試。

## 3-2. 四個關鍵技巧

### 技巧 1：Morton 順序，讓「排序後相鄰」≈「空間上相鄰」

如果只按 x 排序，x 很接近但 y 差很遠的點會被排在一起。**Morton（Z-order）** 把 x、y 的二進位位元交錯起來，變成一個數字：

```
x = 5 = 1 0 1
y = 3 = 0 1 1
交錯 →  0 1 1 0 1 1  （y2 x2 y1 x1 y0 x0）
```

照這個數字排序，路徑會在平面上走 Z 字形，**排在一起的點，空間上也大多靠在一起**，分群就會比較緊密。

### 技巧 2：buffer 放在中位數，總線長最短

為什麼放中位數，而不是平均？Manhattan 距離的 x 和 y 是分開算的。以 x 為例，要讓 Σ|x − xᵢ| 最小，答案就是 xᵢ 的**中位數**（這是數學性質）。y 也一樣，所以 (x 的中位數, y 的中位數) 就是**總線長最短的位置**。

這也帶來一個快速判斷：**如果連中位數的總長都沒有型號撐得住，其他位置一定也不行**，可以直接放棄，不用再搜尋。

### 技巧 3：螺旋找空位

中位數那格可能已經被 sink 或別的 buffer 佔了。這時從它往外一圈一圈找：先找距離 1 的 4 個點、再找距離 2 的 8 個點……第一個同時滿足下面三個條件的就用它：
- 在晶片**內部**；
- **沒被佔用**；
- 有型號撐得住。

### 技巧 4：佔用表，查「這格有沒有人」只要 O(1)

GPT61Sol 每次都掃過**所有節點**來檢查座標有沒有重疊，10 萬個 sinks 光建樹就要 50 秒。我們在 `Tree` 裡加一個 `unordered_set`（雜湊表），記下所有已佔用的座標，查詢只要 O(1)。

**這也是物件導向的好處：** 佔用表放在 `Tree` 的 `private` 裡，`addBuffer` 和 `moveTo` 會**自動更新**它，外面永遠不會忘記同步。

## 3-3. 為什麼一定會結束

每一輪一定發生下面兩件事之一：
- **有合併：** frontier 的數量變少；
- **沒合併：** 每個點都被 relay 拉到**嚴格更靠近** SRC 的位置。

數量不可能一直變少，距離也不可能一直變短（最少到 1），所以迴圈一定會停。萬一 relay 也找不到位置（題目無解），就回傳 `false` 報錯，不會無窮迴圈。另外還設了 10 萬輪的上限，雙重保險。

---

## 3-4. 修改程式

### 修改 1：`#include` 加一行

```cpp
#include <unordered_set>
```

### 修改 2：`Tree` 加上佔用表（5 個地方）

**(a)** 建構子最後，`for` 迴圈的 `}` 之後，加一行：

```cpp
            nodes_[i].kind = SINK;
        }
        for (const Node& v : nodes_) occupied_.insert(key(v.x, v.y));
    }
```

**(b)** `node()` 下面加一行：

```cpp
    const Node& node(int v) const { return nodes_[v]; }
    bool isFree(int x, int y) const { return occupied_.count(key(x, y)) == 0; }
```

**(c)** `addBuffer` 裡，`push_back` 後面加一行：

```cpp
        nodes_.push_back(b);
        occupied_.insert(key(x, y));
        return (int)nodes_.size() - 1;
```

**(d)** `moveTo` 的最前面加兩行（舊位置釋放、新位置佔用）：

```cpp
    void moveTo(int v, int x, int y) {
        occupied_.erase(key(nodes_[v].x, nodes_[v].y));
        occupied_.insert(key(x, y));
        nodes_[v].x = x;
        nodes_[v].y = y;
    }
```

**(e)** `private:` 改成：

```cpp
private:
    static long long key(int x, int y) { return ((long long)x << 32) | (unsigned int)y; }

    std::vector<Node> nodes_;
    int num_sinks_ = 0;
    std::unordered_set<long long> occupied_;
```

**`key` 的意思：** 雜湊表只能存一個值，所以把 (x, y) 壓成一個 64 位元的數字，x 放高 32 位、y 放低 32 位，不同座標一定得到不同的數字。

**`static`：** 這個函式不需要用到任何一棵樹的資料，只是純計算，所以加上 `static`。

### 修改 3：建樹的函式

貼在 `evaluate` 的 `}` 之後、`selfTest` 之前：

```cpp
bool insideChip(const Problem& P, long long x, long long y) {
    return x >= 1 && x <= P.dimx - 1 && y >= 1 && y <= P.dimy - 1;
}

long long groupLength(const Tree& T, const std::vector<int>& group, long long x, long long y) {
    long long len = 0;
    for (int v : group) len += std::llabs(x - T.node(v).x) + std::llabs(y - T.node(v).y);
    return len;
}

bool findBufferSpot(const Problem& P, const Tree& T, const std::vector<int>& group, int& bx, int& by) {
    std::vector<int> xs, ys;
    for (int v : group) {
        xs.push_back(T.node(v).x);
        ys.push_back(T.node(v).y);
    }
    std::sort(xs.begin(), xs.end());
    std::sort(ys.begin(), ys.end());
    int mx = xs[xs.size() / 2];
    int my = ys[ys.size() / 2];

    int k = (int)group.size();
    if (P.cheapestType(k, groupLength(T, group, mx, my)) == -1) return false;

    mx = std::max(1, std::min(P.dimx - 1, mx));
    my = std::max(1, std::min(P.dimy - 1, my));
    for (int r = 0; r <= 16; ++r) {
        for (int dx = -r; dx <= r; ++dx) {
            int dy = r - std::abs(dx);
            for (int sy : {1, -1}) {
                if (dy == 0 && sy == -1) continue;
                int x = mx + dx, y = my + sy * dy;
                if (!insideChip(P, x, y) || !T.isFree(x, y)) continue;
                if (P.cheapestType(k, groupLength(T, group, x, y)) == -1) continue;
                bx = x;
                by = y;
                return true;
            }
        }
    }
    return false;
}

bool findRelaySpot(const Problem& P, const Tree& T, int v, int& bx, int& by) {
    long long maxLen = 0;
    for (const Bufferlibrary& b : P.buflib) maxLen = std::max(maxLen, b.length);
    long long d = T.dist(v, 0);
    long long step = std::min(maxLen, d - 1);
    if (step <= 0) return false;

    const Node& a = T.node(v);
    long long tx = a.x, ty = a.y;
    long long mxStep = std::min(step, std::llabs((long long)P.src_x - a.x));
    tx += (P.src_x > a.x) ? mxStep : -mxStep;
    long long myStep = std::min(step - mxStep, std::llabs((long long)P.src_y - a.y));
    ty += (P.src_y > a.y) ? myStep : -myStep;
    tx = std::max(1LL, std::min((long long)P.dimx - 1, tx));
    ty = std::max(1LL, std::min((long long)P.dimy - 1, ty));

    for (int r = 0; r <= 32; ++r) {
        for (int dx = -r; dx <= r; ++dx) {
            int dy = r - std::abs(dx);
            for (int sy : {1, -1}) {
                if (dy == 0 && sy == -1) continue;
                long long x = tx + dx, y = ty + sy * dy;
                if (!insideChip(P, x, y) || !T.isFree((int)x, (int)y)) continue;
                long long toChild = std::llabs(x - a.x) + std::llabs(y - a.y);
                long long toSrc = std::llabs(x - P.src_x) + std::llabs(y - P.src_y);
                if (toChild > maxLen || toSrc >= d) continue;
                bx = (int)x;
                by = (int)y;
                return true;
            }
        }
    }
    return false;
}

long long mortonKey(int x, int y) {
    long long key = 0;
    for (int bit = 0; bit < 31; ++bit) {
        key |= (long long)((x >> bit) & 1) << (2 * bit);
        key |= (long long)((y >> bit) & 1) << (2 * bit + 1);
    }
    return key;
}

bool buildInitial(const Problem& P, Tree& T) {
    std::vector<int> frontier;
    for (int s = 1; s <= T.numSinks(); ++s) frontier.push_back(s);

    for (int round = 0; round < 100000; ++round) {
        long long srcLen = 0;
        for (int v : frontier) srcLen += T.dist(0, v);
        if ((int)frontier.size() <= P.fanout && srcLen <= P.length) {
            for (int v : frontier) T.link(v, 0);
            return true;
        }

        std::sort(frontier.begin(), frontier.end(), [&](int a, int b) {
            return mortonKey(T.node(a).x, T.node(a).y) < mortonKey(T.node(b).x, T.node(b).y);
        });

        std::vector<int> next;
        bool merged = false;
        int i = 0;
        while (i < (int)frontier.size()) {
            std::vector<int> group = {frontier[i]};
            int bx = 0, by = 0;
            int j = i + 1;
            while (j < (int)frontier.size()) {
                group.push_back(frontier[j]);
                int x, y;
                if (!findBufferSpot(P, T, group, x, y)) {
                    group.pop_back();
                    break;
                }
                bx = x;
                by = y;
                ++j;
            }
            if (group.size() >= 2) {
                int b = T.addBuffer(bx, by);
                for (int v : group) T.link(v, b);
                next.push_back(b);
                merged = true;
            } else {
                next.push_back(frontier[i]);
            }
            i = j;
        }

        if (!merged) {
            bool moved = false;
            for (int& v : next) {
                int x, y;
                if (!findRelaySpot(P, T, v, x, y)) continue;
                int b = T.addBuffer(x, y);
                T.link(v, b);
                v = b;
                moved = true;
            }
            if (!moved) return false;
        }
        frontier = next;
    }
    return false;
}
```

### 修改 4：`main` 換成真正的流程

`selfTest` 保留不動，只把 `main` 整個換掉：

```cpp
int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "error: missing arguments\n";
        std::cerr << "usage: " << argv[0] << " INPUT_FILE OUTPUT_FILE\n";
        return 1;
    }
    Problem P;
    if (!readProblem(argv[1], P)) {
        std::cerr << "error: cannot read " << argv[1] << "\n";
        return 1;
    }
    if (argc >= 4 && std::string(argv[3]) == "--selftest") return selfTest(P, argv[2]);

    Tree T(P);
    if (!buildInitial(P, T)) {
        std::cerr << "error: cannot build a legal tree\n";
        return 1;
    }
    Eval e = evaluate(P, T);
    std::cerr << "[stage1] legal=" << e.legal << " cost=" << e.cost << " skew=" << e.tmax - e.tmin
              << " score=" << e.score << " " << e.why << "\n";

    if (!T.write(P, argv[2])) {
        std::cerr << "error: cannot write " << argv[2] << "\n";
        return 1;
    }
    std::cout << "T_max: " << e.tmax << ", T_min: " << e.tmin << ", Score: " << e.score << "\n";
    return 0;
}
```

這就是題目要求的正式輸出：stdout 只印一行 `T_max: X, T_min: Y, Score: Z`。`[stage1]` 那行印到 stderr，之後做報告時 `stage_report.py` 會抓這個格式。

---

## 3-5. 新的 C++ 語法

| 語法 | 在哪裡 | 意思 |
|---|---|---|
| `[&](int a, int b) { return ...; }` | `std::sort` 的第三個參數 | **lambda**：當場寫一個小函式，告訴 sort「a 要排在 b 前面嗎」。`[&]` 讓它可以用外面的變數（`T`） |
| `for (int& v : next)` | relay 那段 | 有 `&` 的範圍 for：**v 就是 vector 裡那一格本身**，`v = b` 會直接改掉 vector 的內容。沒有 `&` 的話只是改到複製品 |
| `for (int sy : {1, -1})` | 螺旋搜尋 | 對 `{1, -1}` 這兩個值各跑一次，分別處理上半圈和下半圈 |
| `x >> bit & 1` | `mortonKey` | 取出 x 的第 bit 個位元（0 或 1） |
| `<< (2 * bit)` | `mortonKey` | 把位元放到第 2×bit 個位置 |
| `1LL` | relay | `long long` 型態的 1。`std::max` 要求兩邊型態一樣，寫 `1` 會是 int，編譯不過 |

---

## 3-6. 檢查點

```bash
cmake --build build && ./build/cbi ../testcases/input001.cbi out.cbi && cat out.cbi
```

我實測的結果：

```
[stage1] legal=1 cost=36 skew=95 score=131
T_max: 200, T_min: 105, Score: 131
.buffer 3
B1 SMALL 55 91
B2 MEDIUM 49 27
B3 SMALL 56 91
...
```

**全部 15 筆用 TA 的評分流程跑一次**，這和評分機的做法一樣：打包、編譯、每筆限時 60 秒、verifier 檢查。在 `Lab2/` 資料夾裡執行：

```bash
cd .. && python tools/local_grade.py M11502102 M11502102 testcases
```

應該看到 **15 筆全部 VALID**。到這裡就是**第一個可以交的版本**了，建議先 commit 存檔。

## 3-7. 目前的分數

| 測資 | sinks | 你的 Stage 1 | TUTORIAL 基本版（加上簡單最佳化） | 參考解 |
|---|---:|---:|---:|---:|
| 001 | 5 | 131 | 37 | 37 |
| 004 | 50 | 1756 | 614 | 400 |
| 008 | 1000 | 7446 | 5000 | 3574 |
| 011 | 10000 | 39465 | 39621 | 33114 |

目前分數還很高。原因是 Stage 1 只求「合法」，沒有考慮 skew：例如 001 的 skew 是 95，但最佳解只有 1。

下一課（第 4 課）加上**最佳化**：隨機移動 buffer，分數變好就保留，變差就還原。這是最簡單的 hill climbing，就能把 001 從 131 降到 37 左右。

---

# 我

請直接幫我修改  且不要註解

---

# Claude

我已經把第 3 課直接改進你的 [main.cpp](Lab2/M11502102/src/main.cpp)，新加的程式碼都沒有註解。檔案裡剩下的 11 行註解都在 `struct Problem` 裡，是你自己寫的，我沒動。

**結果：** 用 TA 的評分流程（打包 → cmake → 每筆限時 60 秒 → verifier）跑過 15 筆測資，**全部 VALID**，每筆都不到 0.1 秒。編譯沒有任何 error 或 warning。

| 測資 | Score | 測資 | Score |
|---|---:|---|---:|
| 001 | 131 | 009 | 13059 |
| 002 | 119 | 010 | 60794 |
| 003 | 589 | 011 | 39465 |
| 004 | 1756 | 012 | 1888 |
| 005 | 1709 | 013 | 1192 |
| 006 | 2875 | 014 | 10 |
| 007 | 3749 | 015 | 143 |
| 008 | 7446 | | |

**改了哪些地方：**
- 加上 `#include <unordered_set>`。
- `Tree` 加了佔用表：建構子、`isFree`、`addBuffer`、`moveTo`、`private` 裡的 `key` 和 `occupied_`。
- 在 `evaluate` 和 `selfTest` 之間加入建樹的 6 個函式：`insideChip`、`groupLength`、`findBufferSpot`、`findRelaySpot`、`mortonKey`、`buildInitial`。
- `main` 改成正式流程：讀檔 → 建樹 → 計分 → 寫檔 → 印出 `T_max: X, T_min: Y, Score: Z`。`--selftest` 還能用。
- 刪掉 `main` 裡你原本那行 `//argc argument_count...` 的註解，因為 `main` 整個換掉了。

你自己也可以用同樣的流程跑一次（在 `Lab2/` 執行）：

```bash
python tools/local_grade.py M11502102 M11502102 testcases
```

這已經是**可以交的版本**了，建議先 commit 存起來，之後改壞了還能回來：

```bash
git add Lab2/M11502102 && git commit -m "M11502102: legal initial construction"
```

目前分數偏高，是因為建樹時只求合法、不管 skew。下一課（第 4 課）加最佳化，可以讓分數明顯下降。準備好了跟我說。

---

# 我



<pasted_content id="31f0">
(eda) evan@evan-linux:~/Desktop/NTUST/105_1/Class/Computer_Aided_Design_for_VLSI_System/Lab2$ python tools/local_grade.py M11502102 M11502102 testcases
[1] packed 4 files -> /home/evan/Desktop/NTUST/105_1/Class/Computer_Aided_Design_for_VLSI_System/Lab2/local_grade_M11502102/submissions/M11502102.zip (5439 bytes)
[2] unpacked to /home/evan/Desktop/NTUST/105_1/Class/Computer_Aided_Design_for_VLSI_System/Lab2/local_grade_M11502102/vm_home/M11502102
[3] build OK in 1.3s -> /home/evan/Desktop/NTUST/105_1/Class/Computer_Aided_Design_for_VLSI_System/Lab2/local_grade_M11502102/vm_home/M11502102/build/cbi
[4] running testcases
    001: OK            0.0s
    002: OK            0.0s
    003: OK            0.0s
    004: OK            0.0s
    005: OK            0.0s
    006: OK            0.0s
    007: OK            0.0s
    008: OK            0.0s
    009: OK            0.0s
    010: OK            0.0s
    011: OK            0.0s
    012: OK            0.0s
    013: OK            0.0s
    014: OK            0.0s
    015: OK            0.0s
[5] verifier -> /home/evan/Desktop/NTUST/105_1/Class/Computer_Aided_Design_for_VLSI_System/Lab2/local_grade_M11502102/record.csv
student_id,testcase,status,score,t_max,t_min
M11502102,001,VALID,131,200,105
M11502102,002,VALID,119,114,17
M11502102,003,VALID,589,469,30
M11502102,004,VALID,1756,722,91
M11502102,005,VALID,1709,872,159
M11502102,006,VALID,2875,716,305
M11502102,007,VALID,3749,1940,723
M11502102,008,VALID,7446,1699,453
M11502102,009,VALID,13059,5632,1547
M11502102,010,VALID,60794,5608,1358
M11502102,011,VALID,39465,10407,5014
M11502102,012,VALID,1888,1475,835
M11502102,013,VALID,1192,342,76
M11502102,014,VALID,10,22,22
M11502102,015,VALID,143,164,21
</pasted_content id="31f0">


---

# Claude

接著做第 4 課：最佳化（hill climbing）。我先在你的程式上實作並測試，確認分數有下降、全部合法再改進去。

---

# Claude

測試完成：最佳化版本在 15 筆測資和極端測資上都合法（只剩那 5 筆本來就無解的）。現在把它改進你的檔案。

---

# Claude

我把第 4 課（最佳化）直接改進你的 [main.cpp](Lab2/M11502102/src/main.cpp)，新程式碼一樣沒有註解。

**結果：** 用 TA 的評分流程跑 15 筆測資，**全部 VALID**，最久的一筆 20 秒（限制 60 秒）。極端測資也全部合法，只剩那 5 筆本來就無解的。

## 分數變化（越低越好）

| 測資 | 第 3 課（只建樹） | **第 4 課（加最佳化）** | TUTORIAL 基本版 | 參考解 |
|---|---:|---:|---:|---:|
| 001 | 131 | **43** | 37 | 37 |
| 002 | 119 | **56** | 119 | 30 |
| 003 | 589 | **318** | 344 | 141 |
| 004 | 1756 | **592** | 614 | 400 |
| 005 | 1709 | **1225** | 1181 | 870 |
| 006 | 2875 | **2208** | 1960 | 763 |
| 007 | 3749 | **2793** | 2649 | 2112 |
| 008 | 7446 | **4514** | 5000 | 3574 |
| 009 | 13059 | **10454** | 9872 | 7243 |
| 010 | 60794 | **35320** | 48150 | 18876 |
| 011 | 39465 | **35256** | 39621 | 33114 |
| 012 | 1888 | **991** | 1152 | 839 |
| 013 | 1192 | **960** | 1001 | 884 |
| 014 | 10 | **10** | 10 | 10 |
| 015 | 143 | **113** | 143 | 41 |

每一筆都比第 3 課好，和 TUTORIAL 基本版比是 8 筆較好、5 筆較差、2 筆平手。

---

# 第 4 課在做什麼：Hill Climbing（爬山法）

## 核心想法

```
重複直到時間到：
    隨機對樹做一個小修改（move）
    如果修改後不合法 → 還原
    如果 Score 沒有變差 → 保留
    否則 → 還原
```

就像蒙著眼睛爬山：每次隨便踏一步，比較低就留下（這題要分數越低越好），比較高就退回去。

## 三種 move

| Move | 機率 | 做什麼 | 為什麼有用 |
|---|---|---|---|
| **Pad**（插繞路 buffer） | 20% | 在某個節點和它的 parent 中間插一個新 buffer | 讓太早到的 sink **故意繞遠路**，拉高 T_min、縮小 skew。002 從 119 降到 56、015 從 143 降到 113 主要靠它 |
| **Shift**（移動 buffer） | 40% | 隨機挑一個 buffer，往隨機方向移動 1、2、4、8…格 | 調整距離，讓各個 sink 的到達時間更接近 |
| **Reparent**（換 parent） | 40% | 把節點換到「祖父」或「parent 的兄弟」底下；舊 parent 沒有 children 了就**刪掉** | 刪掉 buffer 就**直接省 cost**。cost 通常佔分數的大部分 |

**Shift 的移動距離為什麼用 1、2、4、8…：** 小步可以微調，大步可以跳出死胡同，混著用比固定步長好。

## 怎麼保證全程合法

每個 move 改完之後，**只檢查被改到的節點**（`nodeOK`），不用整棵檢查：
- **Shift：** 只有被移動的 buffer 和它的 parent 的線長會變。
- **Reparent：** 只有新 parent 多了一個 child，舊的那邊只會變少，不會變不合法。
- **Pad：** 只有新 buffer 和原本的 parent 會變。

不合法就馬上還原，所以樹**隨時都是合法的**。最後 `main` 還會用 `evaluate` 從頭完整檢查一次，萬一有問題就退回第 3 課的樹，**絕對不會交出不合法的檔案**。

## Tree 新增的功能

**刪除 buffer（`removeBuffer`）：**
- 不能真的從 vector 拿掉，否則後面所有節點的編號都會錯位。
- 做法是把它標記成 `DEAD`，釋放它的座標，並把編號放進 `free_` 清單。
- 下次 `addBuffer` 會**優先重用**這個編號，vector 才不會越長越大。

**復活 buffer（`revive`）：**
- Reparent 失敗要還原時，被刪掉的 buffer 要救回來。

**`write` 不用改：** 它是從 SRC 做 BFS，`DEAD` 的節點沒接在樹上，自然不會被寫出去。

**`evaluate` 改了三個地方：** 跳過 `DEAD` 節點，不檢查它的座標，也不把它算進「應該走得到的節點數」。

## 時間設定

```cpp
double seconds = std::min(20.0, 2.0 + T.numSinks() / 500.0);
```

- 小測資跑 2 秒，大測資最多 20 秒，離 60 秒的限制很遠。
- 評分機可能比較慢，所以留了很大的餘裕。

---

## 為什麼還追不上參考解

Hill climbing 的弱點是**只接受「不變差」的修改**，很容易卡在局部最佳：周圍每一步都會變差，但遠處其實有更好的解。

例如 006 的 2208 和參考解 763 差最多，就是這個原因。參考解還多做了這些：

| 技巧 | 效果 |
|---|---|
| **模擬退火** | 偶爾接受變差的修改，跳出局部最佳 |
| **增量計算**（每個節點記子樹的 lo/hi） | 每次修改不用整棵重算，速度快上百倍，同樣時間可以試幾百萬次 |
| **Stage 2 平衡** | 有系統地找「整棵子樹都太早到」的地方插繞路 buffer |

這些在第 5 課（進階），看你剩下的時間決定要不要做。

**先 commit 存檔**，這是比第 3 課好很多的可交版本：

```bash
git add Lab2/M11502102 && git commit -m "M11502102: add hill climbing optimization"
```

接下來可以：
- **(a) 做第 5 課：** 模擬退火，分數還能再降；
- **(b) 先準備報告：** 截止是 10/14，報告最多 3 頁。用 `stage_report.py` 產生各階段的 cost / skew / Score 表格和圖，正好對應題目要求的「比較各階段」。

你想先做哪一個？

---

# 我

請你創 兩個檔案 目前第一個為 "M11502102: legal initial construction"

---

# Claude

我在 `Lab2/` 底下建了兩個資料夾。資料夾名稱不能有冒號和空白，所以改用底線：

| 資料夾 | 內容 | input001 的分數 |
|---|---|---|
| [M11502102_legal_initial_construction/](Lab2/M11502102_legal_initial_construction) | 第 3 課版本：只建合法初始樹 | Score 131 |
| [M11502102_hill_climbing/](Lab2/M11502102_hill_climbing) | 第 4 課版本：加上 hill climbing 最佳化 | Score 43 |

- **內容：** 兩個都是完整的專案，有 `src/main.cpp`、`CMakeLists.txt`、`README.md`，沒有 `build/`。
- **測試：** 兩個我都另外編譯、跑過 `input001`，沒有任何 warning，分數和之前一樣。
- **`M11502102/`：** 維持不動，裡面是目前最新的第 4 課版本，和 `M11502102_hill_climbing` 相同。

之後每完成一個階段，都可以照這樣另存一份，改壞了也能直接拿舊版回來比較。

---

# 我

且請把 selftest 刪掉 讓 main 都是 報告需要的格式

---

# Claude

我把三個資料夾的 `main.cpp` 都改好了：`M11502102`、`M11502102_legal_initial_construction`、`M11502102_hill_climbing`。

- **刪掉 selftest：** `selfTest` 函式和 `main` 裡的 `--selftest` 判斷都拿掉了。
- **加上報告格式：** 新增一個 `logStage` 函式，每個階段結束時在 stderr 印一行 `stage_report.py` 讀得懂的格式：

```
[stage1] legal=1 buffers=3 cost=36 skew=95 T_max=200 T_min=105 score=131 time=0.00s
[stage2] legal=1 buffers=3 cost=42 skew=1 T_max=141 T_min=140 score=43 time=2.01s
T_max: 141, T_min: 140, Score: 43
```

- **stdout** 只有最後一行，這是題目規定的格式，評分機看這行。
- **stderr** 是各階段的 buffer 數、cost、skew、Score 和時間，報告用。
- `legal_initial_construction` 版本只有 Stage 1，所以只印 `[stage1]`。
- 為了算 buffer 數，`Eval` 多了一個 `buffers` 欄位；為了印時間，多加了 `<chrono>` 和 `<iomanip>`。新程式碼一樣沒有註解。

三個都編譯過，沒有 error 或 warning，`M11502102` 和 `M11502102_hill_climbing` 的內容仍然相同。

我用 `stage_report.py` 實際跑了一次，產生的表格可以直接放進報告：

| case | N | w | stage1 cost / skew / Score | stage2 cost / skew / Score | 改善 |
|---|---|---|---|---|---|
| 001 | 5 | 1 | 36 / 95 / **131** | 42 / 1 / **43** | 67.2% |
| 002 | 5 | 1 | 22 / 97 / **119** | 26 / 30 / **56** | 52.9% |
| 003 | 20 | 1 | 150 / 439 / **589** | 156 / 162 / **318** | 46.0% |
| 004 | 50 | 2 | 494 / 631 / **1756** | 394 / 99 / **592** | 66.3% |
| 005 | 100 | 1 | 996 / 713 / **1709** | 906 / 319 / **1225** | 28.3% |
| 006 | 200 | 5 | 820 / 411 / **2875** | 778 / 286 / **2208** | 23.2% |
| 007 | 500 | 1 | 2532 / 1217 / **3749** | 2208 / 585 / **2793** | 25.5% |
| 008 | 1000 | 3 | 3708 / 1246 / **7446** | 3488 / 342 / **4514** | 39.4% |
| 009 | 2000 | 1 | 8974 / 4085 / **13059** | 8877 / 1577 / **10454** | 19.9% |
| 010 | 5000 | 10 | 18294 / 4250 / **60794** | 17390 / 1798 / **35370** | 41.8% |
| 011 | 10000 | 1 | 34072 / 5393 / **39465** | 32604 / 2658 / **35262** | 10.6% |
| 012 | 30 | 1 | 1248 / 640 / **1888** | 910 / 81 / **991** | 47.5% |
| 013 | 300 | 1 | 926 / 266 / **1192** | 884 / 76 / **960** | 19.5% |
| 014 | 1 | 1 | 10 / 0 / **10** | 10 / 0 / **10** | 0.0% |
| 015 | 8 | 1 | 0 / 143 / **143** | 10 / 103 / **113** | 21.0% |

從表上可以看出，Stage 2 主要是把 **skew 大幅壓低**（例如 001 從 95 降到 1），cost 只小幅變動，有時還會增加，因為多插了繞路 buffer。這正好可以寫進報告的 trade-off 分析。

自己產生表格和圖的指令（在 `Lab2/` 執行，跑完約 1.5 分鐘）。你的程式不支援 `-t` / `-d` 參數，所以要加 `--no-extra-args`：

```bash
python tools/stage_report.py M11502102/build/cbi testcases M11502102_report --no-extra-args
```

結果會存在 `M11502102_report/stages.md` 和 `stages.csv`。

---

# 我

開始第 5 課模擬退火

---

# Claude

原版設定（σ 權重 0.3、原本的起始溫度）整體最好，就用它。現在套用到你的檔案，並另存一份資料夾。

---

# Claude

結果全部 VALID，但 001 這次是 50（上一次跑是 37）。模擬退火有隨機性，小測資的結果會跳動。我加上「多次重啟取最好」來降低這種運氣成分。

---

# Claude

第 5 課（模擬退火）已經直接改進你的 [main.cpp](Lab2/M11502102/src/main.cpp)，新程式碼一樣沒有註解，也另存了一份 [M11502102_simulated_annealing/](Lab2/M11502102_simulated_annealing)。

**驗證：**
- 用 TA 的評分流程跑 15 筆測資，**全部 VALID**，最久 20 秒。
- 極端測資也全部合法，只剩那 5 筆本來就無解的。
- 編譯沒有任何 error 或 warning。

## 分數比較（越低越好）

| 測資 | N | 第 4 課 hill climbing | **第 5 課 模擬退火** | 參考解 |
|---|---:|---:|---:|---:|
| 001 | 5 | 43 | **38** | 37 |
| 002 | 5 | 56 | **31** | 30 |
| 003 | 20 | 318 | **151** | 141 |
| 004 | 50 | 592 | **558** | 400 |
| 005 | 100 | 1225 | **1152** | 870 |
| 006 | 200 | 2208 | **1145** | 763 |
| 007 | 500 | 2793 | 2885 | 2112 |
| 008 | 1000 | 4514 | **4361** | 3574 |
| 009 | 2000 | 10454 | **10150** | 7243 |
| 010 | 5000 | 35320 | **32924** | 18876 |
| 011 | 10000 | 35256 | 35362 | 33114 |
| 012 | 30 | 991 | **916** | 839 |
| 013 | 300 | 960 | 981 | 884 |
| 014 | 1 | 10 | 10 | 10 |
| 015 | 8 | 113 | **52** | 41 |

整體大約好 14%（各筆比值的幾何平均）。小測資進步最多：003 和 006 幾乎砍半，002、015 也已經很接近參考解。

---

# 第 5 課：模擬退火（Simulated Annealing）

## 問題：Hill climbing 會卡住

Hill climbing 只接受「不變差」的修改。走到一個「往哪走都會變差」的地方，就永遠出不去了，這叫**局部最佳（local optimum）**：

```
Score
  │  ╲      ╱╲
  │   ╲    ╱  ╲        ╱
  │    ╲__╱    ╲      ╱
  │   局部最佳   ╲____╱  ← 真正更好的地方
  │   （卡在這）   全域最佳
  └──────────────────────→ 樹的形狀
```

要從左邊的坑走到右邊，**必須先往上爬（先變差）**，hill climbing 永遠不會這樣做。

## 解法：偶爾接受變差

名字來自金屬「退火」：高溫時原子亂跳，慢慢降溫，最後排成最整齊的結構。

```
d = 新能量 − 舊能量
如果 d ≤ 0（沒變差）     → 一定接受
如果 d > 0（變差了 d）   → 以機率 e^(−d / 溫度) 接受
```

| 溫度 | 變差 d = 10 時的接受機率 | 行為 |
|---|---|---|
| 高（例如 3） | e^(−10/3) ≈ 3.6% | 偶爾願意變差，到處探索 |
| 低（例如 0.01） | e^(−1000) ≈ 0 | 幾乎只接受變好，等於 hill climbing |

**溫度從高慢慢降到低：** 前期大膽探索、跳出坑洞，後期收斂到附近最好的解。

## 程式裡的四個重點

### 1. 溫度怎麼設

```cpp
double T0 = f0 * c;                                          // 起始溫度
double T1 = std::min(T0, 0.01 * std::min(c, (double)P.w_skew));   // 結束溫度
temp = T0 * std::pow(T1 / T0, used / seconds);               // 隨時間指數下降
```

- **c = 最便宜 buffer 的 cost。** 溫度用它當單位，意思是「一開始偶爾願意多花大約一個 buffer 的成本去探索」。
- **f0 隨 sink 數變小（0.03～0.3）：** 樹越大，同時被打亂的地方越多，起始溫度要低一點。
- **指數降溫：** 前期降得快、後期降得慢，讓收斂階段有足夠的時間。

### 2. 能量函數多加了 σ（標準差）

```cpp
c.energy = (double)c.score + beta * (double)P.w_skew * sigma;   // beta = 0.3
```

**為什麼要加：** skew 只看最大和最小 arrival。大部分修改只動到中間的 sink，skew 完全不變，所以 SA 不知道該往哪走（能量是平的）。

σ 是**所有** sink arrival 的標準差，每個 sink 變得更整齊都會讓它下降，提供「方向感」。

**最後交出去的仍然是真正的 Score**，σ 只用來引導搜尋。

### 3. 另外記住「最好的解」

SA 會接受變差的修改，所以**結束時的樹不一定是過程中最好的**。程式另外用 `best` 記住看過的最低 Score：
- 每 1024 次迭代檢查一次，有更好的就複製一份；
- 最後交出 `best`。

### 4. 小測資多跑幾次（重啟）

```cpp
int runs = T.numSinks() <= 50 ? 4 : 1;
```

SA 有隨機性，小測資的結果會跳動。我把 001 重複跑了 6 次：

| 方式 | 001 的 6 次結果 | 平均 |
|---|---|---:|
| 單次 2 秒 | 39 37 46 49 47 41 | 43.2 |
| **4 次 × 0.5 秒，取最好** | 42 40 37 43 38 37 | **39.5** |

小測資跑 0.5 秒就收斂了，分成 4 次、每次換一個亂數種子，比一次跑 2 秒穩定。大測資則相反，分段會讓每次時間不夠，所以只跑一次。

## 實驗紀錄（可以寫進報告）

我在 15 筆測資上比較了幾種設定。數字是和最終版的分數比值，越低越好：

| 設定 | 結果 | 結論 |
|---|---:|---|
| **最終版**（σ 權重 0.3、原本的起始溫度） | 1.000 | 採用 |
| 不加 σ（beta = 0） | 1.030 | 差 3%，σ 有幫助 |
| 起始溫度 × 0.3 | 1.042 | 差 4%，太冷就跳不出坑 |
| 所有測資都重啟 | 1.004 | 中型測資變差，所以只在 N ≤ 50 時重啟 |

**注意：** SA 有隨機性，每次跑的分數會有幾 % 的差異。報告裡比較時，最好每筆多跑幾次取平均。

---

## 為什麼大測資還追不上參考解

007、011、013 幾乎沒有進步，010 也還差參考解很多。原因是**速度**：
- 你的程式每試一次修改，都用 `quickScore` 重算整棵樹，是 O(N)。1 萬個 sinks 時，每秒只能試幾千次。
- SA 需要大量嘗試才能發揮作用，**試的次數不夠，退火就只是隨機亂走**。

參考解用**增量計算**：每個節點記住子樹的最早 / 最晚到達時間（lo / hi），改一個 buffer 只要沿祖先往上更新，每秒可以試幾十萬次，快上百倍。

這可以當成**第 6 課**。它會改到 Tree 的核心，比較大，但對大測資幫助最大。

**目前的進度：**

| 資料夾 | 版本 |
|---|---|
| `M11502102_legal_initial_construction` | 第 3 課：只建合法樹 |
| `M11502102_hill_climbing` | 第 4 課：hill climbing |
| `M11502102_simulated_annealing` | 第 5 課：模擬退火（= 目前的 `M11502102`） |

截止還有一天多，建議先寫報告（最多 3 頁），有剩餘時間再做第 6 課。要我先幫你整理報告的架構和要放的圖表嗎？

---

# 我

開始第 6 課增量計算 且 小測資不要用退火

---

# Claude

先確認一下「小測資不要用退火」的效果。我用同樣的時間和重啟次數，比較小測資改用 hill climbing 和繼續用退火的差別。

---

# Claude

好，小測資保留退火。接著做第 6 課：增量計算。我先讀一次你目前的完整程式，再在副本上實作和測試。

---

# Claude

對準關鍵路徑又好了約 5%，而且全部合法，所以採用。現在套用到你的檔案、另存資料夾，再做完整驗證。

---

# Claude

第 6 課（增量計算）已經直接改進你的 [main.cpp](Lab2/M11502102/src/main.cpp)，新程式碼一樣沒有註解，也另存了一份 [M11502102_incremental/](Lab2/M11502102_incremental)。小測資照你的決定保留退火。

**驗證：**
- 用 TA 的評分流程跑 15 筆測資，**全部 VALID**；
- 極端測資也全部合法，只剩那 5 筆本來就無解的；
- 編譯沒有任何 error 或 warning。

## 分數比較（越低越好）

| 測資 | N | 第 5 課 模擬退火 | **第 6 課 增量計算** | 參考解 |
|---|---:|---:|---:|---:|
| 001 | 5 | 38 | 39 | 37 |
| 002 | 5 | 31 | **30** | 30 |
| 003 | 20 | 151 | **149** | 141 |
| 004 | 50 | 558 | **516** | 400 |
| 005 | 100 | 1152 | **1063** | 870 |
| 006 | 200 | 1145 | **1108** | 763 |
| 007 | 500 | 2885 | **2606** | 2112 |
| 008 | 1000 | 4361 | **4115** | 3574 |
| 009 | 2000 | 10150 | **9520** | 7243 |
| 010 | 5000 | 32924 | **22422** | 18876 |
| 011 | 10000 | 35362 | **33103** | 33114 |
| 012 | 30 | 916 | 949 | 839 |
| 013 | 300 | 981 | **968** | 884 |
| 014 | 1 | 10 | 10 | 10 |
| 015 | 8 | 52 | **51** | 41 |

- **大測資進步最多：** 010 從 32924 降到 22422（−32%）。
- **011 追平參考解：** 33103 對 33114。
- **小測資本來就收斂了：** 差異在隨機性的範圍內。

速度（20 秒內的嘗試次數）：

| 測資 | 第 5 課 | 第 6 課 | 倍數 |
|---|---:|---:|---:|
| 008（1000 sinks） | 305 萬 | 1074 萬 | 3.5× |
| 011（1 萬 sinks） | 166 萬 | 3384 萬 | **20×** |

樹越大，增量計算的優勢越明顯，因為原本每次都要重算整棵樹。

---

# 第 6 課：增量計算

## 問題：每次都重算整棵樹太浪費

第 5 課每試一次修改，`quickScore` 都從 SRC 走完整棵樹，是 O(N)。但移動**一個** buffer 時，真正會變的只有：
- 它自己的子樹接到上面的那段距離；
- 它的祖先們（往上一路到 SRC）。

其他幾千個節點完全沒變，卻每次都重算。

## 解法：每個節點記住「子樹的摘要」

`Node` 多了這些欄位：

| 欄位 | 意思 |
|---|---|
| `lo` / `hi` | 從**這個節點**往下走，到子樹裡 sink 的**最短 / 最長**路徑 |
| `cnt` | 子樹裡有幾個 sink |
| `s1` / `s2` | 子樹裡所有 sink 路徑長的**總和 / 平方和**（用來算 σ） |
| `sumLen` | 到所有 children 的距離總和 |
| `type` | 目前最便宜的合法型號 |

**關鍵：整棵樹的答案就在根節點上**

```
T_max = nodes[0].hi        T_min = nodes[0].lo
skew  = hi − lo            σ 由 nodes[0] 的 cnt、s1、s2 算出
cost  = total_cost_（Tree 隨時維護的總成本）
```

所以 `T.score()` 只要 **O(1)**：直接讀根節點，不用走樹。

## 怎麼由 children 算出自己（`recompute`）

對每個 child c，邊長 e = d(自己, c)：

```
lo  = min( e + c.lo )                 ← 走到 c 再走 c 的最短路
hi  = max( e + c.hi )
cnt = Σ c.cnt
s1  = Σ ( c.s1 + c.cnt × e )          ← c 底下每個 sink 都多走 e
s2  = Σ ( c.s2 + 2e × c.s1 + c.cnt × e² )   ← (a+e)² = a² + 2ae + e² 展開
```

sink 本身：`cnt = 1`，其他都是 0。

`s2` 的公式來自平方展開：每個 sink 的路徑長從 a 變成 a+e，平方和就從 Σa² 變成 Σ(a+e)² = Σa² + 2eΣa + cnt·e²。

## 往上更新，而且會提早停（`refreshUp`）

```cpp
void refreshUp(int v) {
    while (v >= 0) {
        if (!recompute(v)) break;   // 這個節點的摘要沒變 → 祖先也不會變，停
        v = nodes_[v].parent;
    }
}
```

例如，把一個 sink 移到同一個 buffer 底下的另一個位置，只要它不是最長或最短的那條路，buffer 的 `hi` / `lo` 就不會變。這時更新**在第一層就停了**，連 SRC 都不用碰。

複雜度從 O(N) 變成 **O(深度 × fanout)**，通常只有幾十個節點。

## 物件導向的好處：Tree 自己保證資料永遠正確

`link`、`unlink`、`moveTo`、`removeBuffer`、`revive` 改完之後，都會**自動呼叫 `refreshUp`**。外面的程式（建樹、最佳化）完全不用管「要更新哪些節點」，也**不可能忘記**。`recompute` 和 `refreshUp` 放在 `private`，外面根本呼叫不到。

`total_cost_` 也由 `recompute` 自動維護：型號一換，就把成本差加回總成本。

## 還原也不用另外處理

退火拒絕一個修改時，只要做**反向操作**：移回原位、接回原 parent、復活被刪掉的 buffer。這些操作同樣會自動 `refreshUp`，摘要就會回到原本的值，不需要另外存一份備份。

## 安全檢查：增量結果 vs 完整重算

增量計算寫錯的話，分數會悄悄算錯。所以 `main` 每次最佳化後都會比對：

```cpp
if (e2.legal && e2.score == T.score() && e2.score < e.score)
```

`evaluate`（完整 BFS 重算）和 `T.score()`（增量）**必須一模一樣**才採用，否則退回之前的解。測試時兩者完全一致，例如 001 是 131 = 131，011 是 39465 = 39465。

## 附加：對準關鍵路徑（`criticalNode`）

有了 `lo` / `hi`，就能從 SRC 往下，一路跟著「`e + child.lo == 自己.lo`」的 child 走，**直接找到決定 T_min 的那條路徑**，T_max 也一樣。

30% 的 move 改成從這條路徑上挑節點：
- **Pad：** 對準最早到的路徑，讓它繞路變晚；
- **Shift / Reparent：** 對準最早或最晚的路徑。

skew 只由這兩條路徑決定，隨機挑的話，大部分 move 根本碰不到它們。這一項讓整體再好約 5%（005：1220 → 977，010：25698 → 22324）。

---

## 目前的版本

| 資料夾 | 版本 | 001 / 010 |
|---|---|---|
| `M11502102_legal_initial_construction` | 第 3 課：建合法樹 | 131 / 60794 |
| `M11502102_hill_climbing` | 第 4 課：hill climbing | 43 / 35320 |
| `M11502102_simulated_annealing` | 第 5 課：模擬退火 | 38 / 32924 |
| `M11502102_incremental` | 第 6 課：增量計算 + 關鍵路徑（= 目前的 `M11502102`） | 39 / 22422 |

這四個版本正好可以當報告裡「各階段比較」的材料。

離截止（10/14 23:59）剩大約一天半，**建議現在開始寫報告**。報告最多 3 頁，題目要求說明初始建構、最佳化方法，並比較各階段的 skew / cost / Score。要我幫你整理報告的架構，並產生需要的表格和圖嗎？

---

# 我

好 並幫我 建立新的資料夾 專門產生 測資 與圖 跟表格

---

# Claude

正在寫產生測資、執行、表格和圖的腳本（共用設定已完成）。

---

# Claude

All VALID on both sets. Let me look at the tables and figures.

---

# Claude

圖已經修好。最後寫 README，說明怎麼使用這個資料夾，以及報告的架構。

---

# Claude

收到，先把 node 裝進 `eda` 環境，再用它驗證配色並修改繪圖程式的配色。

---

# Claude

配色已改好，圖也重畫了，三種顏色在分數圖上都分得清楚。最後在 README 補上配色驗證的說明。

---

# Claude

新資料夾 [M11502102_report/](Lab2/M11502102_report/README.md) 建好了：一個指令就會產生新測資、跑 4 個版本、整理表格、畫圖。兩組測資（正式 15 筆、新產生 12 筆）在 4 個版本上**全部 VALID**。node 也裝進 `eda` 了，配色已依驗證結果修改。

## 怎麼用

```bash
conda activate eda
cd M11502102_report && ./run_everything.sh
```

全部跑完約 3 分鐘，依序做這幾件事：

| 腳本 | 做什麼 | 產出 |
|---|---|---|
| `build_all.py` | 編譯 4 個版本 | `build/v1..v4/cbi` |
| `gen_tests.py` | 用 `tools/gen_input.py` 產生 12 筆新測資（5～1 萬個 sinks） | `inputs/holdout/` |
| `run_all.py` | 平行執行每個版本的每筆測資，用課程 verifier 檢查 | `results/<set>/<版本>/` |
| `make_tables.py` | 整理成表格（`.md` + `.csv`） | `tables/<set>/` |
| `make_figures.py` | 畫圖 | `figures/<set>/` |

每一步也可以單獨跑，例如產生另一組測資：`python gen_tests.py --name holdout2 --count 20`。用法都寫在 README 裡。

## 產生的表格與圖

| 檔案 | 內容 | 報告用途 |
|---|---|---|
| `stages.md` | 建構 → 最佳化的 buffers / cost / skew / Score / 秒數 | 題目要求的「各階段比較」 |
| `versions.md` | 4 個版本的 Score，加上幾何平均 | 每個方法的貢獻 |
| `bounds.md` | cost 下限、奇偶 skew 下限，以及和最終 Score 的比值 | 離最佳解還有多遠 |
| `fig_trees_001/004.png` | 同一筆測資 4 個版本的樹（2×2） | 方法說明 |
| `fig_score_by_version.png` | 各版本 Score ÷ 初始 Score | 實驗結果 |
| `fig_cost_skew_split.png` | Score 拆成 cost 和 w·skew 兩段 | 觀察：最佳化主要是降低 skew |

**目前的結果**（Score ÷ 初始建構的 Score，幾何平均，越低越好）：

| 測資組 | Hill climbing | 模擬退火 | 增量計算 |
|---|---:|---:|---:|
| 正式 15 筆 | 0.630 | 0.518 | **0.497** |
| 新產生 12 筆（開發時沒看過） | 0.452 | 0.389 | **0.317** |

在沒看過的新測資上也一路變好，可以拿來說明方法不只對原本那 15 筆有效。

## 配色修改

- **node：** 已裝進 `eda` 環境（v26.10.0），配色驗證程式現在可以跑了。
- **舊配色不合格：** 分數圖原本用橘、青綠、黃。這是點狀圖，每一對顏色都會同時出現，其中黃和橘的辨識度只有 13.7（標準是 ≥ 15）。
- **新配色：** 分數圖改用預設配色的前三色（藍、橘、青綠），全部檢查都通過。青綠和背景的對比 2.74:1 偏低，圖上用不同的點形狀（● ■ ◆）加上 `versions.md` 表格補救。
- **堆疊長條圖：** cost 用藍、skew 用橘，所有檢查都通過，包括對比。
- **在哪裡改：** 顏色定義在 `scripts/common.py`，README 寫了重新驗證的指令。

樹狀圖用的是 `tools/plot_cbi.py` 原本的配色，我沒有改。

## 報告架構（細節在 README）

| 節 | 篇幅 | 放什麼 |
|---|---|---|
| 1. Problem & Notation | 0.3 頁 | Score 定義、verifier 比 PDF 嚴格的地方 |
| 2. Initial Construction | 0.6 頁 | `fig_trees_004.png` 左上 |
| 3. Optimization | 0.9 頁 | 3 種 move、退火、增量計算、關鍵路徑 |
| 4. Experiments | 0.9 頁 | `stages.md`、`versions.md`、`fig_score_by_version.png` |
| 5. Observations | 0.3 頁 | 奇偶性下限、`bounds.md`、`fig_cost_skew_split.png` |

**兩件事要注意：**
- **不要放進 zip：** 這個資料夾只是報告工具，交作業時只交 `M11502102/`。
- **數字會變動：** 模擬退火有隨機性，每次跑會差幾 %，報告的數字請以你最後一次跑出來的為準。

---

# 我

那你再看看 brief 的所有資料 看哪裡還可以改

---

# Claude

已把 22 份 PDF 轉成文字（共約 1.7 萬行），先讀最相關的時脈樹講義（L04）和模擬退火講義（L06_1）。

---

# Claude

L04 和 L06_1 讀完了。接著快速列出其他講義每張投影片的標題，找出和這題有關的段落。

---

# Claude

6 個實驗版本 × 27 筆測資 × 2 次都跑完了，正在整理結果並用 verifier 檢查合法性。

---

# Claude

brief 的 22 份資料都看過了，從講義裡找到兩個有效的改法，實測整體分數大約可以再低 3.5%。

**和這題無關、跳過的：**
- L11～L17：高階合成、邏輯合成；
- L08、L09：floorplan 的資料結構；
- L05：channel routing；
- 兩篇余振華的訪談。

## 有效的改法

| 改法 | 出處 | 正式 15 筆 | 新 12 筆 |
|---|---|---:|---:|
| **(A) 零 skew 合併點**：放 buffer 時讓各 child 的到達時間對齊，而不是只取總線長最短的中位數。建樹時用，退火時也加一個「重新放置」move | L04 的 Tsay 零 skew 演算法、DME、GMA（tapping point 不在中點） | **−2.0%** | **−4.0%** |
| **(C) 交換 parent**：兩個鄰近節點互換 parent | L06 TimberWolf 的 interchange、L10 KL 的成對交換 | ±0% | −2.5% |
| **(A) + (C) 一起** | | **−3.4%**（11 好 / 3 差） | **−3.6%**（10 好 / 1 差） |

**無效的：** L06 TimberWolf 的 range limiter（移動範圍隨溫度縮小）會讓結果變差 0.4～0.7%，不建議採用。原因是我們的移動距離本來就是 1、2、4…隨機混用，已經兼顧大步和小步。

**另外一個和 brief 無關的調整：** 把時間上限從 20 秒放寬到 40 秒（限制是 60 秒），在 (A)+(C) 的基礎上再好約 1%。合計正式 15 筆 **−4.5%**、新 12 筆 **−4.8%**，最慢的一筆跑 40.05 秒。

**驗證方式：**
- 每個版本每筆測資跑 2 次取平均，一共 432 份輸出；
- 全部用 TA verifier 檢查，**全部 VALID**；
- 實驗程式在暫存區，你的 `M11502102` 還沒動。

## 其他看過但不適用的

| brief 的內容 | 為什麼不適用 |
|---|---|
| H-tree、MMM（L04） | 要求 sink 分布均勻，或只做由上往下對半切；我們的建樹已經是由下往上分群 |
| DME 完整版、Elmore delay（L04） | 題目的延遲是 Manhattan 距離加總，沒有電阻電容，而且每個分支點都是要付錢的 buffer |
| Clock mesh、spine（L04） | 題目規定輸出必須是一棵樹 |
| FLUTE（Steiner tree） | 之前 Ultracode_3 已經測過：Hanan grid 沒有幫助，只有「多方向切割」略有效 |
| Rip-up and re-route（L03） | 對應到「拆掉子樹重建」，Ultracode_3 測過，會變差 |
| ILP（L02） | 只對很小的題目可行，而小測資本來就已經接近最佳 |
| 退火排程 Tᵢ = 0.85·Tᵢ₋₁（L06、L08） | 我們是依時間指數降溫，效果相同 |

## 報告可以引用

- **建樹：** 對應 GMA（由下往上配對）＋ Tsay 的零 skew 合併點。
- **最佳化：** 對應 TimberWolf（displacement + interchange moves）。
- **小測資重啟：** 對應 L10「Adding randomness to KL/FM：多跑幾次取最好」。
- **range limiter：** 可以寫成「試過但無效」，作為實驗佐證。

要我把 **(A)+(C)** 改進你的 `M11502102`，並另存成新的版本資料夾嗎？時間上限要放寬到 40 秒，還是維持 20 秒（多留一些餘裕給較慢的評分機）？

---

# 我

好 用 (A)+(C) 時間改 40 秒 並另存資料夾

---

# Claude

好，我把 (A)+(C) 和 40 秒時間上限改進 `M11502102`。先在副本上改好並測試，確認沒問題才覆蓋。

---

# Claude

全部合法（正式 15 筆、新 12 筆，極端測資只剩那 5 筆無解的）。現在套用到 `M11502102`、另存資料夾，再跑一次 TA 評分流程。

---

# Claude

TA 評分流程 15 筆全部 VALID，最久 40.1 秒。最後把新版本（v5）加進報告工具，重跑一次產生最新的表格和圖。

---

# Claude

我已經把 (A)+(C) 和 40 秒時間上限改進 [M11502102/src/main.cpp](Lab2/M11502102/src/main.cpp)，另存成 [M11502102_balanced_swap/](Lab2/M11502102_balanced_swap)，報告工具也加入了這個版本並重跑過。新程式碼沒有註解，檔案裡只剩你自己在 `Problem` 寫的 11 行。

## 驗證

- **TA 評分流程**（打包 → cmake → 每筆限時 60 秒 → verifier）：15 筆**全部 VALID**，最久一筆（011，1 萬 sinks）40.1 秒。
- **新測資 12 筆：** 全部 VALID。
- **極端測資：** 全部合法，只剩原本那 5 筆無解的。
- **編譯：** 沒有任何 error 或 warning。

## 改了什麼

| 改動 | 出處 | 程式裡的位置 |
|---|---|---|
| **零 skew 合併點：** 放 buffer 時，在 children 的範圍內找讓「到達時間最晚 − 最早」最小的位置，同時考慮型號成本 | L04：Tsay 的零 skew 演算法、DME、GMA | 新函式 `balancedSpot`；`buildInitial` 建群時改用它 |
| **重新放置 move（10%）：** 把 buffer 移到 `balancedSpot` 找到的位置 | L04 | `optimize` 的 `move == 3` |
| **交換 parent move（10%）：** 兩個節點互換 parent | L06 TimberWolf interchange、L10 KL | `optimize` 的 `move == 4` |
| **Move 比例：** Pad 20%、Shift 30%、Reparent 30%、重新放置 10%、交換 10% | | 原本是 20 / 40 / 40 |
| **時間上限：** `min(40, 2 + N/250)` 秒 | | 原本是 `min(20, 2 + N/500)` |

## 分數（各版本 Score ÷ 初始建構 Score，幾何平均，越低越好）

| 測資組 | Hill climbing | 模擬退火 | 增量計算 | **brief 改進（新）** |
|---|---:|---:|---:|---:|
| 正式 15 筆 | 0.630 | 0.521 | 0.487 | **0.469** |
| 新產生 12 筆 | 0.452 | 0.380 | 0.313 | **0.301** |

和上一版相比，15 筆裡 12 筆變好，005 和 012 稍微變差，那是退火的隨機性範圍。幾個變化明顯的例子：
- **010：** 22266 → 20446（參考解 18876）；
- **011：** 33248 → 32808，已經比參考解的 33114 更好；
- **001：** 37，和參考解相同，已經碰到奇偶性下限（cost 36 + skew 1）。

## 版本資料夾一覽

| 資料夾 | 版本 |
|---|---|
| `M11502102_legal_initial_construction` | v1 建合法樹 |
| `M11502102_hill_climbing` | v2 hill climbing |
| `M11502102_simulated_annealing` | v3 模擬退火 |
| `M11502102_incremental` | v4 增量計算 |
| **`M11502102_balanced_swap`** | **v5 brief 改進 + 40 秒（= 目前的 `M11502102`）** |

**報告工具（`M11502102_report/`）：**
- 加入了 v5，表格和圖都已經更新；
- 分數圖改畫 v2 / v4 / v5（配色規則最多 3 色），樹狀圖畫 v1 / v2 / v4 / v5；
- README 補上 brief 實驗的數據，以及報告架構裡新的 move。

目前這版已經可以交，建議先 commit：

```bash
git add Lab2/M11502102 Lab2/M11502102_* && git commit -m "M11502102: zero-skew merge placement, parent swap, 40s budget"
```

接下來就剩報告了（截止 10/14 23:59）。
