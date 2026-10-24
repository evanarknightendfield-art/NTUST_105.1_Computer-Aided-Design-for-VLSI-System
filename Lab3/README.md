# Lab3 — CAD Project 3：Standard Cell Detailed Placement

題目在 `Project03.pdf`：Stage 1 合法化（壓總位移和最大位移）→ `OUTPUT_legal.pl`；Stage 2 自己寫的 SA 壓 HPWL、保持合法 → `OUTPUT.pl`。
執行方式 `dp INPUT_FILE OUTPUT_FILE`，stdout 印 4 行。交 `學號.tgz`（程式 + ReadMe）和 3 頁以內的 PDF 報告，截止 2026/11/04。

**先看這份就夠了。** 下面說每個資料夾放什麼、哪些東西很大不用看。

## 資料夾

| 資料夾 / 檔案 | 放什麼 |
|---|---|
| `Project03.pdf` | 題目 |
| `Grading Criteria.pdf` | **Project 2** 的評分方式（放錯 lab，但可以看出 TA 用隱藏測資、跑不完就 0 分） |
| `GSRC Bookshelf Format ….txt` | 輸入 / 輸出格式 |
| `Example/` | 題目的 toy 範例和圖 |
| **`Testcases/`** | **所有測資，和所有程式跑出來的結果**（見下一節） |
| `AI_Solutions/` | AI 寫的各版本解法（只有原始碼和說明），`COMPARISON.md` = 所有版本的比較和目前的結論 |
| `AI_Workspace/` | `TUTORIAL.md`（一步一步自己做的教學）、`step1_example/`、`reference_report/`（範例報告） |
| `Grader/` | 檢查與評分工具：`tools/check_pl.py`（獨立檢查器）、`tools/local_grade.py`（模擬繳交）、`tools/exact_bound.py`（最佳解 / 下界） |
| `M11502102/` | **要交的程式**（學號資料夾） |
| `MyCode/` | 練習用的骨架 |
| `Report/` | 報告：跑實驗的腳本、表格、圖、草稿、完稿 |

## 規則：測資和跑出來的結果都放 `Testcases/`

```
Testcases/
├── bench.tgz、make_testcases.sh      ← 測資的來源和產生腳本（進 git）
├── testcases/  holdout/  edge/  large/  ← 產生的測資（不進 git，./make_testcases.sh [--large] 重建）
│     edge/ = 16 筆現有測資沒測到的情況（過擬合 / 穩健度檢查，結果比較在 results/edge/README.md）
│     edge_seed2/ = 同一個產生器換 seed 的另一組（CodexChainDP 最後驗證用；gen_edge.py --seed 20261020）
│     edge_seed3/ = 再換一個 seed（ClaudeOpus55_v5 最後驗證用；gen_edge.py --seed 20261021）
└── results/<誰跑的>/<實驗>/           ← 任何程式跑出來的東西（git 只追蹤小的紀錄檔，見下面）
    ├── local_grade/<學號>/            ← Grader/tools/local_grade.py 的預設輸出（不進 git）
    ├── manual/                        ← 自己手動跑的輸出放這裡（不進 git）
    ├── report/、reference_report/     ← Report/ 的腳本跑出來的原始結果（表格和圖還是在 Report/、reference_report/）
    ├── step2_grader/                  ← Grader/step2_grader/run_demo.py
    └── CodexWindowDP/、CodexChainDP/、… ← 各解法自己的實驗腳本
```

- 原始碼資料夾（`AI_Solutions/*`、`M11502102/`、`MyCode/`）**裡面不放結果**。腳本都已經改成寫到 `Testcases/results/`。
- 每個資料夾的內容、是哪支腳本產生的：`Testcases/README.md`。
- **`results/` 裡 git 只追蹤小的紀錄檔**：`.csv`（每次的 SA trace 除外）、`.md`、`.json`、`.log`、`.txt`、`.png`、腳本，全部約 4 MB。
  placement 輸出（`.pl`）、SA trace、執行檔、解壓的套件、輸入檔的副本都只留在本機（`.gitignore`），clone 下來要看就重跑。
- 任何地方的 `out/` 也不進 git，但請盡量用 `Testcases/results/manual/`。

## 給 AI（和人）：看什麼、不用看什麼

要了解這個 lab，依序看：

1. 這份 README
2. `Project03.pdf`
3. `AI_Solutions/COMPARISON.md`：各版本比較、離最佳解多遠、還能改良的地方
   （v1 → v6 每一版做了什麼、進步多少的總表：`AI_Solutions/HISTORY.md`）
4. 要改的那個版本的 `README.md` 和 `src/`
5. 教學：`AI_Workspace/TUTORIAL.md`

**不用看**（很大，而且都是跑出來的）：

| 路徑 | 大小 | 什麼時候才需要 |
|---|---:|---|
| `Testcases/results/` | 本機約 500 MB（git 裡約 4 MB） | 要查某個實驗的數字時，只看那個實驗的 `records.csv` / `record.csv` / `README` |
| `Testcases/testcases/`、`holdout/`、`edge/`、`edge_seed2/`、`large/` | 約 210 MB | 只有程式要讀測資時（Bookshelf 文字檔） |
| `AI_Solutions/*/build/` | 編譯產物 | 不需要 |
| 任何 `.pl`、`.nets`、`.nodes` | | 用 `Grader/tools/check_pl.py` 讀，不要直接打開 |

搜尋程式碼時把它們排除：`grep -r PATTERN Lab3 --exclude-dir=Testcases --exclude-dir=build`，或 `rg PATTERN -g '!Testcases' -g '!build'`。

## 解法版本

| 資料夾 | 一句話 |
|---|---|
| `ClaudeOpus55/` | v1：Abacus + 位移微調 + SA。**教學和報告的參考解**（`TUTORIAL.md`、`Report/run_everything.sh reference`、checker 示範都用它） |
| `CodexWindowDP/` | Codex：v3 + 兩個 row 的子集合 DP（v4 的前一代） |
| `ClaudeOpus55_v4/` | 兩 / 三個 row 的視窗 DP 跑到收斂、多執行緒 SA、每個階段都有時間預算（20 萬 cell 3 分鐘）。**HPWL 最好** |
| `CodexChainDP/` | Codex：v4 + 最大位移的全域上限修復、ejection chain、多候選 swap。**位移最好**（最大位移比 v4 少 4.2%，總位移一樣）；同樣時間下 HPWL 比 v4 差約 1.5% |
| `ClaudeOpus55_v5/` | ChainDP + SA 用滿時間、平行 merge 變便宜、row 重新分配的最大位移搜尋。同樣時間下 HPWL 比 v4 好約 1%；最大位移比 v4 少 4.3%、跟 ChainDP 平均差不多（逐筆互有勝負）。診斷在 `Testcases/results/ClaudeOpus55_v5/diagnosis.md` |

已移除的版本：v2、v3、CodexGapSA、CodexParetoDP 的原始碼在 git 歷史裡，commit `e1fd81f`（lab3-01-CodexWindowDP）：
`git show e1fd81f:Lab3/AI_Solutions/ClaudeOpus55_v2/README.md` 看，`git checkout e1fd81f -- Lab3/AI_Solutions/ClaudeOpus55_v2` 拿回來；
CodexAugmentDP（以 v4 為底的實驗，沒寫完）從來沒有進 git。它們的數字仍在 `AI_Solutions/COMPARISON.md`
和 `Testcases/results/edge/`。WindowDP、ChainDP 的比較腳本會重新編譯這些舊版本，要重跑那些比較就先把它們拿回來。

數字和排名看 `AI_Solutions/COMPARISON.md`、`AI_Solutions/CodexChainDP/README.md`，不要用這張表。

## 常用指令

```bash
cd Lab3
./Testcases/make_testcases.sh                 # 產生測資（加 --large 再產生 20 萬 / 100 萬 cell）

cmake -S M11502102 -B M11502102/build && cmake --build M11502102/build
mkdir -p Testcases/results/manual
./M11502102/build/dp Testcases/testcases/toy/toy Testcases/results/manual/toy
python3 Grader/tools/check_pl.py Testcases/testcases/toy/toy Testcases/results/manual/toy_legal.pl

# 模擬繳交（打包 → 編譯 → 11 筆測資 → 檢查），結果在 Testcases/results/local_grade/M11502102/
python3 Grader/tools/local_grade.py M11502102 M11502102 Testcases/testcases Testcases/holdout
# 再跑 16 筆沒見過的情況（各版本的比較在 Testcases/results/edge/README.md）
python3 Grader/tools/local_grade.py M11502102 M11502102 Testcases/edge --work Testcases/results/local_grade/M11502102_edge

# 報告用的實驗（原始結果 → Testcases/results/report/，表格和圖 → Report/tables、Report/figures）
cd Report && ./run_everything.sh
```
