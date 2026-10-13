# Project 2 — Clock Buffer Insertion 實作教學

> 目標：你自己從零做出 `cbi`，交出 `學號.zip` + `學號.pdf`（報告 ≤ 3 頁）。
> 截止：**2026/10/14 23:59**

---

## 0. 資料夾導覽（這次已經準備好的東西）

| 路徑 | 內容 |
|---|---|
| `step1_example/` | 範例 input + 3 個範例答案 + 畫好的圖（`output1.png`…） |
| `step2_grader/` | 用 TA verifier 評分的示範：3 個範例答案 + 5 個故意做錯的答案 |
| `step2_grader/local_run/` | 用本機版評分流程跑參考解的完整紀錄 |
| `testcases/` | 15 筆測資（001 = 範例，002–015 由 `tools/gen_input.py` 產生，從 1 到 10000 個 sinks） |
| `claude_reference/` | **我的參考解**（先別看！自己做完 Milestone 3 再看） |
| `reference_report/` | 參考解各階段的表格 `stages.md` 與圖（報告格式參考） |
| `my_cbi/` | **你的專案骨架**，從這裡開始 |
| `tools/` | 輔助工具（見下表） |

| 工具 | 用途 |
|---|---|
| `tools/cbi_explain.py IN OUT` | 逐步列出每條邊長、每個 parent 的 fanout/length 用量、每個 sink 的 arrival，**debug 最好用** |
| `tools/plot_cbi.py IN [OUT] -o x.png` | 畫圖（取代範例附的 gnuplot） |
| `tools/gen_input.py` | 產生測資（`--suite DIR` 產生整組） |
| `tools/local_grade.py PROJECT SID INPUTS` | 本機模擬 TA 評分：打包 zip → 解壓 → cmake → 每筆限時 60 秒 → verifier |
| `tools/stage_report.py` | 整理各階段 cost / skew / Score 表格與圖（報告用） |

每次開新的 terminal 先啟用環境：

```bash
conda activate eda
```

---

## 1. 規則總整理（PDF + Requirement + verifier.py）

**輸入**：`.limit`（SRC 的 fanout / length、`.dimx` / `.dimy`）、`.buflib`（型號 F / L / C）、`.objective`（`w_skew`）、`.pin`（第一個是 SRC，之後依序是 S1, S2, …）。

**名詞**
- `d(u,v) = |xu−xv| + |yu−yv|`（Manhattan）
- 每個 parent `u`：`|children| ≤ F_u` 且 `Σ d(u, child) ≤ L_u`（是**總和**，不是每條邊）
- sink 的 arrival time `T_i` = SRC 到它路徑上所有邊長的總和（buffer 本身沒有 delay）
- `skew = T_max − T_min`，**`Score = Σ buffer cost + w_skew × skew`，越小越好**

**verifier 會判 INVALID 的情況**（比 PDF 嚴格的地方用粗體標出）

1. 座標重疊（SRC、sinks、buffers 全部都不能重疊）
2. **buffer 必須嚴格在晶片內部：`0 < x < dimx`、`0 < y < dimy`（不能壓在邊界上）**
3. buffer 編號必須是 **`B1..Bm` 連續且依序列出**
4. 型號名稱必須存在於 buflib
5. parent 只能是 SRC 或 buffer；child 只能是 buffer 或 sink
6. 每個 buffer、sink 剛好當一次 child；**每個 buffer 至少要有一個 child**
7. 全部都要從 SRC 走得到
8. **`.level` 的第 k 行列出的 child，BFS 深度必須剛好是 k；`.level N` 的 N 必須等於最大深度**
9. fanout / length 不能超過（SRC 用 `.limit`，buffer 用它宣告的型號）

**繳交與執行**
- 執行檔必須是 `build/cbi`，評分機只跑 `cmake -S . -B build && cmake --build build`（**不會指定 Release**，要在 CMakeLists.txt 自己設）
- `cbi INPUT OUTPUT`，參數不夠要印錯誤訊息；要支援相對路徑
- 檔名三位數：`input001.cbi` → `output001.cbi`
- 每筆 60 秒、build 60 秒、8 GB RAM、無網路、Ubuntu 24.04 + GCC
- 交 `學號.zip`（裡面是 `學號/` 資料夾，只放 `src/`、`CMakeLists.txt`、`README.md`）＋ `學號.pdf`（**報告不要放進 zip**）

---

## 2. Milestones

每個 Milestone 都有「檢查點」，過了再往下。骨架在 `my_cbi/src/main.cpp`，TODO 都標好了。

建議先把資料夾改成你的學號（例如 `M11312345`）：

```bash
cp -r my_cbi M11312345
```

（以下用 `MYPROJ` 代表你的資料夾）

編譯方式：

```bash
cmake -S MYPROJ -B MYPROJ/build && cmake --build MYPROJ/build
```

### Milestone 1：讀檔 `readProblem()`

**做法**
- `std::getline` 一行一行讀 → 先把 `#` 之後刪掉 → `istringstream` 切 token
- 用一個字串記「目前在哪個區塊」：遇到 `.limit` / `.buflib` / `.objective` / `.pin` 就設定，遇到 `.e` 就清空
- 注意 `.dimx` / `.dimy` 寫在 `.limit` 區塊裡面
- `.pin` 的第一個座標是 SRC

**檢查點**：在 `main` 讀完後暫時加一行印出 `P.lib.size()`、`P.sinkX.size()`、`P.wSkew`，範例應該是 `3, 5, 1`。另外讀 `testcases/input011.cbi` 應該有 10000 個 sinks。

### Milestone 2：型號選擇、評分、輸出

**`cheapestType(P, k, len)`**：在所有 `F ≥ k` 且 `L ≥ len` 的型號裡挑 cost 最小的。
重要觀念：**型號不用自己決定**，buffer 的 children 與位置決定後，最便宜的合法型號就確定了。

**`evaluate()`**：照第 1 節的規則從頭檢查一遍。
- 從 SRC 做 BFS，邊走邊算 `arrival[child] = arrival[parent] + d(parent, child)`
- BFS 走到的節點數要等於總節點數（走不到的就是不合法）
- 座標重疊可以把 `(x<<32)|y` 丟進 vector 排序後找相鄰重複

**`writeOutput()`**：
- buffer 依 BFS 順序重新編號成 B1, B2, …
- `.level` 第 k 行：所有「深度 k−1 且有 children 的節點」寫成 `名字:{child1 child2}`

格式：

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

**檢查點**：骨架有寫好的 selftest，會手動建出範例 output1 的樹：

```bash
MYPROJ/build/cbi step1_example/input.cbi /tmp/selftest.cbi --selftest
```

要印出 `legal=1 T_max=141 T_min=36 cost=26 Score=131`。再用工具確認你的輸出檔格式正確：

```bash
python tools/cbi_explain.py step1_example/input.cbi /tmp/selftest.cbi
```

### Milestone 3：合法初始解 `buildInitial()`

**先求一定合法，再求好。** 推薦的最簡單做法（bottom-up 分群）：

```
frontier = 所有 sinks
重複：
  如果 |frontier| ≤ SRC fanout 且 Σ d(SRC, v) ≤ SRC length → 全部接到 SRC，結束
  把 frontier 依空間排序（Morton / Z-order：x、y 的 bit 交錯，讓相鄰的點排在一起）
  從頭貪心地切群：一群一直加下一個點，直到
      群大小 > 最大 fanout，或
      在群的中位數 (median x, median y) 放 buffer 時，沒有型號撐得住總長
  每群（≥ 2 個）放一個 buffer 當 parent，新的 buffer 進入下一輪的 frontier
  只有 1 個的群直接留到下一輪
  如果這一輪完全沒合併成功（點彼此太遠）→ 每個點往 SRC 方向插一個 relay buffer
```

要注意的坑：
- buffer 位置要找**空位**（不能跟任何 pin / buffer 重疊）且 **1 ≤ x ≤ dimx−1**
- 中位數是讓 Σ 距離最小的點（Manhattan 的性質：x、y 各自取中位數）
- 一定要保證迴圈會結束：每輪不是 frontier 變少，就是離 SRC 變近

**檢查點**：全部測資都要 VALID

```bash
python tools/local_grade.py MYPROJ M11312345 testcases
```

照上面做法，「基本版」大約會拿到下表中間那欄（Milestone 4 只做最簡單的 hill climbing 之後）。

### Milestone 4：在合法的前提下降低 Score `optimize()`

最簡單的版本（就能把範例做到 37）：

```
重複直到時間到：
  隨機挑一個 buffer，隨機移動一小段（半徑 1, 2, 4, …, 32 隨機選）
  evaluate() 整棵重算
  如果合法而且 Score 沒變差 → 保留；否則 → 還原位置
```

接著可以自己想更多「move」：
- 把一個 sink 換到附近另一個 buffer 底下（原 buffer 若沒有 child 了就刪掉 → 省 cost）
- 刪掉一個 buffer，把它的 children 分給附近的 buffer
- 在太早到的 sink 上方插一個「繞路」buffer（讓它變晚，skew 變小）

**檢查點**：`local_grade.py` 全部 VALID，且分數比 Milestone 3 好。`main()` 已經寫好保險：最佳化後若不合法或變差，會自動退回初始解。

### Milestone 5（進階，自由發揮）

這些是參考解用到的關鍵想法，建議**先自己想過**再去看 `claude_reference/src/` 對應的檔案：

1. **增量計算**（`tree.cpp`）：每個節點記 `lo / hi` = 從自己往下到所有 sink 的最短 / 最長路徑。
   整棵樹的 `T_min = lo(SRC)`、`T_max = hi(SRC)`。移動一個 buffer 只要沿著祖先往上重算，
   不用每次 O(N) 整棵重算，速度差上百倍 → 同樣時間可以試幾百萬次 move。
2. **Transaction / rollback**（`tree.cpp`）：每次修改前記下舊值，不接受就整批還原。
   有了它，再複雜的 move 也能「先做做看」。
3. **buffer 放哪裡**（`placement.cpp`）：不只是中位數，而是讓 children 的 `d(b, c) + latency(c)` 越接近越好
   （在 children 的 bounding box 裡粗搜再細搜）。
4. **繞路 buffer 拉高 T_min**（`balance.cpp`）：對節點 v，`slack = T_max − (arrival(v) + hi(v))` 是整棵子樹還能延後多少，
   從上往下找「一個 buffer 就能整棵子樹解決」的地方插入。繞路長度一定是偶數（下一點）。
5. **奇偶性下限**：邊長的奇偶 = 兩端 (x+y) 的奇偶差，所以 **sink 的 arrival 奇偶 = (x_s+y_s) − (x_SRC+y_SRC) 的奇偶，永遠不變**。
   只要 sinks 的 (x+y) 奇偶不一致，skew ≥ 1。範例的 37 = cost 36 + skew 1 已經碰到這個下限。這很適合寫進報告。
6. **模擬退火**（`optimizer.cpp`）：偶爾接受變差的 move 跳出局部最佳；溫度以「最便宜 buffer 的 cost」為尺度。
   skew 只看最大最小值，大部分 move 不會改到它，所以能量函數多加一項 σ（arrival 的標準差）當方向感。

---

## 3. 分數對照（15 筆測資）

| case | sinks | 基本版（M3 + 簡單 M4，5 秒） | 參考解（預設時間） |
|---|---|---|---|
| 001（範例） | 5 | 37 | 37 |
| 002 | 5 | 119 | 30 |
| 003 | 20 | 344 | 141 |
| 004 | 50 | 614 | 400 |
| 005 | 100 | 1181 | 870 |
| 006 | 200 | 1960 | 763 |
| 007 | 500 | 2649 | 2112 |
| 008 | 1000 | 5000 | 3574 |
| 009 | 2000 | 9872 | 7243 |
| 010 | 5000 | 48150 | 18876 |
| 011 | 10000 | 39621 | 33114 |
| 012 | 30 | 1152 | 839 |
| 013 | 300 | 1001 | 884 |
| 014 | 1 | 10 | 10 |
| 015 | 8 | 143 | 41 |

範例三個官方答案是 131 / 100 / 127，都遠高於 37，代表題目留了很大的最佳化空間。

---

## 4. 報告（≤ 3 頁 PDF，英文或中文依課程要求）

題目要求：說明**初始合法解怎麼建**、**之後怎麼最佳化且維持合法**，用實驗支持，並**在同一組測資上比較各階段的 skew、buffer cost、Score**。

建議架構：
1. **Problem & Notation**（0.3 頁）：Score 定義、限制
2. **Initial Legal Construction**（0.7 頁）：演算法 + 為什麼一定合法、一定會結束
3. **Optimization**（1 頁）：每種 move、如何保證合法（先試做→檢查→還原）、trade-off（多一個 buffer vs. 少多少 skew）
4. **Experiments**（0.8 頁）：各階段表格 + 1~2 張圖（同一筆測資各階段的樹）、執行時間
5. **Observations**（0.2 頁）：奇偶性下限、w_skew 大小對結構的影響等

產生表格與圖：只要你的程式在 stderr 印出 `[stage1] ... cost=.. skew=.. score=..` 這種格式（骨架的 main 已經會印 `[stage1]`、`[stage2]`），就能直接用。
你的程式不支援 `-d` / `-t` 參數，所以加 `--no-extra-args`：

```bash
python tools/stage_report.py MYPROJ/build/cbi testcases MYPROJ_report --no-extra-args --plot 004
```

參考解（支援 `-d`，會另外輸出中間階段的解來畫圖）：

```bash
python tools/stage_report.py claude_reference/build/cbi testcases reference_report --plot 001,004,006
```

`reference_report/stages.md` 是參考解的範例表格，`fig006_stages.png` 是三個階段並排的圖。

---

## 5. 繳交前 checklist

- [ ] 資料夾名稱 = 學號，裡面只有 `src/`、`CMakeLists.txt`、`README.md`（README 寫怎麼編譯、怎麼執行）
- [ ] CMakeLists.txt 有預設 `Release`、執行檔名稱 `cbi`
- [ ] `python tools/local_grade.py 學號資料夾 學號 testcases` 全部 VALID，每筆都 < 60 秒
- [ ] 不帶參數執行會印錯誤訊息
- [ ] 用 `tools/gen_input.py` 多產生幾組奇怪的測資（1 個 sink、晶片很小、L 很短、SRC length 很小）也都 VALID
- [ ] `學號.zip`（`local_grade.py` 會在 `local_grade_學號/submissions/` 產生一份可以直接交的）＋ `學號.pdf` 分開上傳
