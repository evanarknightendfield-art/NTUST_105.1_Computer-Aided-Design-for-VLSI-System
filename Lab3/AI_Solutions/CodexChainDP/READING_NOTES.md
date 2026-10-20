# Lab03 資料、課程與驗證工具閱讀紀錄

本紀錄說明這次改良前實際閱讀與檢查的範圍。資料夾在本次工作期間有其他編輯與新結果產生，因此檔案數是盤點當下的快照，不把新增的執行結果當成原始教材。演算法版本的逐一分析與新實驗另見本資料夾的比較文件。

## 1. 閱讀範圍與方法

| 範圍 | 實際處理方式 |
|---|---|
| `Lab3/Project03.pdf` | 完整擷取兩頁文字，並渲染兩頁逐頁看過；核對公式、限制、繳交要求與例圖 |
| `Lab3/Grading Criteria.pdf` | 擷取全文、渲染第一頁並核對內容；後續是時脈網路測資圖片，與 Project 3 placement 規格不同 |
| 兩份 `GSRC Bookshelf Format ... .txt` | 閱讀格式全文，檢查 `.pl`、terminal、site/subrow 與 pin offset 定義 |
| `Example/` | 閱讀 README 與 toy 小型輸入；六個輸入檔與正式解壓版本逐位元比較一致；圖形對照題目 PDF |
| `Testcases/` | 閱讀 README、產生腳本、完整 `gen_bench.py`；掃描當時所有 13 筆原始 benchmark 的 nodes/pl/nets/scl 結構，核對數量、參考節點、net degree、spacing 與 rows/subrows |
| `Testcases/bench.tgz` | 清單與內容 hash 檢查：18 個資料檔，解壓後共 7,756,691 bytes，與現有三筆課程測資一致 |
| `Grader/` | 完整閱讀 8 支 Python 原始工具，包括 checker、local submission checker、bounds、generator、plotting 及 demo；閱讀 README 與 demo 結果 |
| `MyCode/my_dp_skeleton/`、`M11502102/` | 閱讀全部 C++ headers/source、Makefile、CMake、README；比較內容，程式與建置檔完全一致，僅 README 不同 |
| `AI_Workspace/TUTORIAL.md` | 閱讀完整教學、規格解讀、M1-M7、參考數據與報告建議 |
| `AI_Workspace/step1_example/` | 六個 toy 輸入與正式版本比對，閱讀 stdout、placement、README；圖片以輸出關聯與示範用途檢查 |
| `AI_Workspace/reference_report/` | 閱讀 Markdown 報告、README、表格與 DOCX XML 文字；掃描 66 份 JSON 的結構、回傳碼與結果欄位；檢查 trace / placement / 圖表的引用與產物結構 |
| `Report/` | 閱讀 README、shell runner、兩支實驗與製圖腳本；區分實驗結果記錄與獨立合法性驗證 |
| `AI_Solutions/` | 由同一工作中的版本審查與實作分工閱讀原始程式、README、比較報告、tests；本篇聚焦教材、格式與評測證據，沒有把二進位執行檔當成原始碼閱讀 |
| `brief/` | 盤點全部簡報；完整讀取 `L06_1_sa.pdf`，閱讀 placement 兩份簡報的相關頁面及頁面目錄，重點如下 |

大型 `.pl/.nets/.nodes`、大量歷史 CSV/JSON、編譯中間檔與 ELF binary 採結構、欄位、數量、hash 或 summary 檢查，**不是宣稱逐字人工閱讀每一個重複輸出或逐像素檢查所有圖片**。DOCX 只作內容核對，未重新排版。這份紀錄不是整個資料夾每個 historical run 的重新驗證。

原始盤點清單另存於工作暫存的 `/tmp/lab03_inventory.tsv`。當時 `AI_Workspace` 為 140 檔、`Example` 9 檔、`Grader` 30 檔、`M11502102` 11 檔、`MyCode` 11 檔、`Report` 9 檔、`Testcases` 81 檔。後續工作的新檔可能增加計數。

## 2. 正式題目真正要求的目標

`Project03.pdf` 的截止時間為 **2026-11-04 23:59**。程式是 Linux 上的 C/C++，介面 `dp INPUT_FILE OUTPUT_FILE`。

1. 讀取 GSRC Bookshelf global placement。
2. 正式簡化條件是無 macro、movable cell 高度相同。合法位置必須在 row 上、x 對齊 site、完整放在一個 valid subrow 中、沒有 cell overlap。
3. 兩階段均保持原本 orientation 與 fixed terminal 位置。
4. Stage 1 同時追求總位移與最大位移小。`d_i = |x_legal - x_gp| + |y_legal - y_gp|`，輸出 `OUTPUT_FILE_legal.pl`。
5. Stage 2 必須從 Stage 1 出發，實作自己的 simulated annealing，維持合法性並輸出找到的最佳 HPWL placement 到 `OUTPUT_FILE.pl`。
6. HPWL 使用 cell **中心**，忽略 pin offsets，所有 net 不加權直接相加；terminal 仍是 net endpoint。方向不能因最佳化而翻轉。
7. stdout 印 Stage 1 的總位移、最大位移、HPWL，以及 Stage 2 HPWL，共四項。
8. 另外繳交最多 **3 頁 PDF**，包含演算法、實驗、trade-off、同一輸入 SA 前後 HPWL、current 與 best-so-far 對 attempted SA iterations 的曲線。
9. source + text ReadMe 打包成學號名稱的 `.tgz`。範例列出輸出 `.aux/.nodes/.nets/.wts/.scl/.pl` 與 `_legal.pl`。

**題目沒有提供 total displacement 與 maximum displacement 的加權比例，也沒有提供 Stage 1 與 Stage 2 的合併分數。** 因此不能任意宣稱某一加權總分等於 TA 成績。更合理的是各別報告 total/max/final HPWL/time/合法性，並用 Pareto dominance 說明誰在某個測資上確實同時較好。

題目也沒有明文的執行秒數上限。實驗的 60 秒、120 秒等是本地設定，要在報告列明；尤其 `--time` 可能只限制 SA，而不是整支程式。

## 3. 容易誤讀的教材與輸入

### 3.1 Grading Criteria 並非 Project 3 的評分表

檔案內容是 **ISPD 2010 High Performance Clock Network Synthesis** 的八筆測資，談的是 sink 數、buffer library、`project2.pdf` 的 input1，以及 Project 2 的三組評分。不能把其中「程式 90% + 報告 10%」、「六筆以上至少 70 分」套成 Lab03 規則。它至多說明老師重視隱藏測資與複雜度。

`Grader/README.md` 自己也明確說 Project 3 沒有提供 TA grader。`local_grade.py` **沒有打分公式**，只提供本地 PASS/FAIL：

- 七個輸出檔存在。
- 四個 stdout 數字與獨立 checker 的計算一致。
- Stage 1、Stage 2 都合法。
- Stage 2 HPWL 不劣於 Stage 1。

### 3.2 PDF 的 toy 數字不能當唯一答案

PDF 的範例 legal HPWL 是 757,296；現有附檔 toy 的 GP HPWL 約 2,698.3，並且和既有示範結果的尺度不同。範例應用來確認介面與輸出格式，不應為了匹配印出的示意數字而改指標定義。

另外，`toy.nets` 有一條 `NetDegree : 22`，但在下一個 NetDegree 出現前實際只有 **3 個 pin**；實際總 pin 數與 header 的 110 相符。13 筆輸入掃描只有 toy 有此不一致。現有 parser 與 checker 在新 NetDegree 出現時切換 net，兼容這個教材問題。嚴格 parser 若直接拒收，會連課程 toy 都無法執行；新增驗證宜先記錄告警。

### 3.3 Bookshelf 的一般格式比本地測資更寬

- fixed 需辨認 `.nodes` 的 `terminal/terminal_NI`，以及 `.pl` 的 `/FIXED`；ibm05 的 1,201 個 pads 只有前者。
- `.scl` 關鍵字有 `NumSites/Numsites`、`NumRows/Numrows` 等大小寫差異；同一 row 可以多 subrow。
- `.nets` 的 net 名稱可有可無，同一 cell 可能有多 pin，cell-center HPWL 對重複 cell 去重後不變。
- `.pl` 是物件左下角座標；HPWL 則要加 `(width/2, height/2)`。
- 一般 Bookshelf 的 subrow 右界是 `origin + (NumSites - 1) * Sitespacing + Sitewidth`。現有模型與 checker 常以 `origin + NumSites * Sitespacing` 計算，等價需要 **Sitewidth == Sitespacing**；目前原始 13 筆符合，不能把測試結果無限推廣到不同 sitewidth 的輸入。
- 固定物件彼此重疊可能來自原始輸入，例如重疊 pads；不能因此任意移動它們。movable 與 fixed 的 overlap 才應阻擋。

## 4. 原始測資範圍

| bench | movable | fixed | nets | rows | subrows | site spacing |
|---|---:|---:|---:|---:|---:|---:|
| toy | 31 | 0 | 38 | 132 | 132 | 66 |
| ibm01 | 12,028 | 0 | 11,507 | 132 | 132 | 66 |
| ibm05 | 28,146 | 1,201 | 28,446 | 148 | 148 | 1 |
| h01 | 300 | 18 | 304 | 17 | 20 | 1 |
| h02 | 1,000 | 43 | 1,003 | 37 | 51 | 66 |
| h03 | 2,000 | 0 | 2,011 | 47 | 47 | 0.2 |
| h04 | 4,000 | 84 | 4,036 | 46 | 56 | 1 |
| h05 | 6,000 | 126 | 5,951 | 99 | 149 | 66 |
| h06 | 10,000 | 162 | 10,137 | 71 | 77 | 1 |
| h07 | 15,000 | 205 | 15,135 | 134 | 161 | 0.2 |
| h08 | 25,000 | 303 | 25,186 | 168 | 180 | 66 |
| L1 | 200,000 | 1,012 | 204,336 | 481 | 529 | 66 |
| L2 | 1,000,000 | 2,020 | 1,034,574 | 794 | 880 | 1 |

前三筆由課程 archive 提供；h01-h08 是同一個 `gen_bench.py` 的固定 seed 2026 系列；L1/L2 也是合成規模測試。合成設計有 macro、split rows、pads、熱點與不同座標尺度，對工程穩健度有用，但不是未知 TA 測資，也不是與開發完全隔離的統計 holdout。

合成 GP 是隨機位置加熱點，沒有經過真正的 global placer 最佳化。因此「SA 降 60%」不代表對真實設計也會降 60%。課程資料與合成資料應分組看，避免平均值被幾個容易改善的合成案例主導。

`M11502102` 與 `my_dp_skeleton` 仍有 `TODO(M1)` 到 `TODO(M6)`，讀檔會主動回傳未實作錯誤。它們應列為「未完成／不可比較」，不是加入有效演算法平均值的零分或零 HPWL。

## 5. 課程簡報對本次演算法的啟發

### L06_1_sa：10 頁

完整閱讀 state、neighborhood、cost、初始解、初始溫度、cooling 與 freezing point 的定義。第 7 頁的接受規則是改善接受、變差以 `exp(-Δ/T)` 接受。這支持在合法解空間內保留真正 SA，再以局部精確求解補強收尾；只做 greedy matching 不等於完成題目要求的 SA。

### L06_placement1：相關頁 2、16-32、58-98

第 25-32 頁 TimberWolf 展示 displacement、swap、reflection、range limiter 與溫度排程。但原始 TimberWolf 容許 overlap、也可翻方向；本題明確要求維持合法性與方向，所以只借 move/range/annealing 思想，不能照搬這兩個自由度。第 16-21 頁說明缺少 whitespace 時 cell 的相對順序很重要，支持加入 reordering 與跨 row 交換。

### L07_placement2：相關頁 2-49，後段 migration/spreading 作背景

- 第 2-3 頁區分「先合法化，盡量小擾動」與後續 wirelength 改善。
- 第 6-8 頁 network flow 能分配容量，但 cell 拆成 subcells 後可能分散；要還原 contiguous cell，flow 結果不能直接當一般不等寬 legalization 的完整最優解。
- 第 12-21 頁固定 row/order 的線性 placement，含 prefix DP、piecewise linear cost、clumping。這種子問題可以精確解，但固定 row/order 的最優不等於整張晶片最優。
- 第 24-28 頁 window interleaving 保留 A/B 各自內部順序，DP 尋找其 interleaving。狀態必須包含決定後續 cost 的足夠資訊，不能直接把任何非可分 HPWL 加成當成獨立 cell cost。
- 第 30、43 頁對照 DP row packing 與 greedy legalization；greedy 在高利用率可能浪費左側空白而失敗。
- 第 44 頁小視窗 branch-and-bound permutation；第 45-49 頁 flow + DP + LP 的混合方法。

對熱點最壞位移的改善方向是讓多顆 cell 協同搬移、跨越單次 swap/insert 的障礙。新方法仍需明確標示自身能搜尋的範圍；協同搬移、matching、窗口 DP 都不能自動證明全域最優。

## 6. 工具可信度與已重現的限制

### 6.1 獨立 checker

`check_pl.py` 重新讀原始幾何、fixed、orientation、nets，檢查 row/site/subrow、overlap、缺漏 cell，並重新計算 displacement/HPWL，這比相信 solver 自報數字更可靠。但它仍是本地工具，不是 TA verifier；一般格式的 Sitewidth/Spacing 差異等超出既有模型時要加測，不應把一句 LEGAL 視為所有格式與所有邊界條件的數學保證。

`Report/scripts/run_experiments.py` 的 JSON 主要記 solver stdout/returncode/時間，**不是每筆均自動執行 checker**。參考報告 66 份 JSON 中，65 份 returncode=0；唯一失敗是 `stage1/ibm01_tetris`，符合報告所述 Tetris 放不下。JSON 的 returncode=0 本身不等同於獨立合法性通過。

過去 effort/seed/ablation 會使用 6 個平行 worker，與單獨執行的 main runtime 不能直接混比。後續 runner 加入 `Testcases/results/` 路徑慣例，舊報告樹與新結果位置可能不同，重現指令須以當前腳本為準。

### 6.2 舊 coarse lower_bound 還有負切片問題

`exact_bound.py` 已把固定物件投影的左右 index 都夾到合法範圍。但 `lower_bound.py` 在本次閱讀時仍是：

```python
a = max(0, floor(...))
b = min(nsite, ceil(...))
free[a:b] = False
```

若 pad 完全在晶片左方，`b` 可能為負數；NumPy 的負 slice end 會把幾乎整個 row 封住。已在 h01、bin size 12 重現：原始工具的 bin capacity 總和是 **10,050**；僅在記憶體中把兩端都夾到 `[0,nsite]` 後為 **13,136**，共 **92 個 bins** 不同。這不是演算法比較的有效下界；不能用此舊工具的結果證明「只離最優幾 %」。沒有修改原工具。

### 6.3 exact_bound 的正確證明範圍

原工具以每個 `(cell, row, site)` 建一個變數：每個 cell 選一個位置、每個 site 最多被覆蓋一次。總位移作 cost。

- LP relaxation 的值是對應模型的 lower bound，**不是**一個合法整數 placement。
- Column generation 後的 restricted ILP 只保留部分位置，即使 solver 回傳零 gap，也只能證明受限 column 集合內最優；全域上是一個可行上界。
- `--full --cap -1 --ilp` 完整列舉位移不超過目前最大值的所有位置。在 solver 證明零 gap 時，只能說「在此最大位移上限下，總位移最優」。它沒有證明這個最大位移本身已最小。
- `--max` 是完整位置模型的 LP feasibility 下界；要最小最大位移的整數證明需 `--max --max-ilp`，且不能把 timeout 的步驟誤當 infeasible。
- 小 toy/h01 的證書不能推廣到 ibm01、ibm05 或百萬 cells，更不能推出 Stage 2 HPWL 最優。

原模型假設 uniform spacing、row height、各 subrow 使用同一 global site grid、movable 寬度是正整數個 site、高度等於 row height；原工具用 `round(width/spacing)`。這些前提必須先驗證。尤其不同 row 的 site spacing 不同時，雖然 C++ solver 可能支援，這個 exact model 不可直接認證。

本次新增 `tests/certify.py`，先檢查上述前提與獨立合法性，再呼叫原始工具的 `--cap -1 --full --ilp --time-limit 120`，寫 solver log、placement、JSON、輸入與原工具 SHA256。只有完整模型的 `OPTIMAL (complete model, zero gap)`、solver 正常完成、輸出獨立合法且符合 cap，才標為證明；timeout / 非零 gap 保留為上界。

證書使用 HiGHS 浮點數值容差，並非 rational arithmetic 的形式驗證。log 的 LP/ILP 數字只印小數一位，不以四捨五入後相等當成精確相等；placement 的實際 displacement 另行重算。

## 7. 合理的比較與結論標準

1. 每個版本保留獨立來源，使用相同 compiler/optimization 與相同輸入，不修改舊 solver。
2. 分別列出「各版本預設」與「相同時間/核心/seed」實驗。v4 多執行緒預設對舊單執行緒版本，不是同資源比較。
3. `--time`、Stage 1 budget、postprocess budget、總牆鐘時間要分清楚；資料讀取、輸出複製、驗證時間也不要混入其中一個版本而漏掉其他版本。
4. Stage 1 比 total 與 max 兩個量；Stage 2 比最終絕對 HPWL，不能只比相對自己 Stage 1 的下降比例。
5. 保留 failed/timeout/illegal 資料，不把失敗行當 0；骨架不算有效 solver。
6. SA 使用多 seed 報告 mean、best、variation；同 seed 也不代表多執行緒 wall-time 版本必然 bitwise reproducible。
7. 宣稱較好時指出 benchmark、資源與指標；若 total 降但 max 升，說明 trade-off；若只在小例證明 optimum，保留該條件。

因此，除非某一特定子問題有完整模型證書，這份工作的合適結論應是「在指定測試與預算下改善」，不是「一般 detailed placement 已經最佳」。仍值得改良的方向包括熱點的長距離連鎖搬移、更大的多 row exact/flow 子問題、更公平的多 seed 時間配置，以及新幾何與 parser edge cases。


## 8. 2026-10-19：本輪增補

本輪先讀 `Lab3/README.md`，再讀共同 `COMPARISON.md`、`Testcases/results/edge/README.md`、v4 README 與相關 src、以及本紀錄。沒有把測資、placement、net 檔或 build 產物整份輸入模型；數據以各實驗 `record.csv` 為準，測資只由 solver、generator、獨立 checker 和 exact_bound 程式讀取。

- 原 row optimizer 在 cap 區間之外仍保留 incumbent 的 site，因此不能直接當作「較小 D 可行」的判斷器。新的 row packing 使用真正的 hard interval，最後仍驗證實際 placement。
- 整數 site 上的 L1 cluster minimization 不能直接四捨五入 median。例如三顆寬度 1 的 cell，global x = 0.9、1.49、2.45：全放 0、1、2 的總位移 1.84，全放 1、2、3 只有 1.16。新測試固定這個反例，實作比較 median 兩側整數的完整成本。
- 全域 cap repair 成功是可行上界；搜尋失敗、超時或記憶體／位置數限制都是 UNKNOWN。二分試探失敗點不是真正下界，不能稱為精確 minimax。
- `Testcases/results/CodexChainDP/exact_max/record.csv` 記錄重跑原工具 `--max --max-ilp` 的結果：toy = 1938.4、h01 = 26.5，兩者均標為 exact。這是獨立工具的證明；不歸功於新的啟發式搜尋。
- 固定 500 萬 SA proposal 使用單執行緒、相同合法起點、三個 seed。每個新 proposal 會多評估候選，因此另用三 seed 的相同總預算實驗，計入各版本預設 Stage 1 成本；實測時間、預算、平均與範圍分開保留。
- 所有舊 ChainDP 結果已遷到 `Testcases/results/CodexChainDP/`，test runner 與 certificate 的預設輸出也改到該處。

最終驗收與沒有達成的目標見本資料夾 `README.md`；本節不把開發中的單次測試當成最終比較。

## 9. 2026-10-20：正式驗收與量測修正

凍結後的 27 筆預設比較，最大位移幾何平均比 v4 低 4.20%，總位移低 0.06%，逐筆最大位移沒有增加。h01 降到 29.39，但仍高於精確最小值 26.5 約 10.91%；toy 的 2007.9 沒有下降，仍高於精確最小值 3.59%。全域搜尋改善了可行上界，沒有消除最優差距。

原同時間上限實驗強制 10 億次 effort，導致部分短時限的大設計停在第一個高溫階段，新加的低溫 move 沒有機會執行。保留這批紀錄為排程壓測，另外保留預設 SA effort 重跑三 seed；只有量測 harness 改變，solver 雜湊不變。修正後 holdout／edge 的 HPWL 幾何平均比 v4 高 1.53%／1.44%，因此 HPWL 目標未達標。固定 500 萬次的下降 0.64%／0.69% 不能推論為時間效率提升；共同時間上限也不等於實際耗時完全相同。

預設 27 筆、L1 與新 seed 的 16 筆共 44/44 PASS；L1 為 196.36 秒。新 seed 20261020 有 14 筆輸入改變、2 筆固定案例；最大位移幾何平均下降 5.60%，總位移下降 0.31%，逐筆最大位移沒有增加。新 seed candidate 驗證後才補跑上述預設排程實驗與新 seed 的 v4 baseline，期間未調整演算法。最後的離散 L1 修正也重新通過 ASan/UBSan。詳細證據見 [README](README.md) 與 [驗收記錄](../../Testcases/results/CodexChainDP/acceptance.json)。
