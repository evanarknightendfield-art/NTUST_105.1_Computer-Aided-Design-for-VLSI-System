# Lab3 — CAD Project 3：Standard Cell Detailed Placement

題目在 `Project03.pdf`：Stage 1 合法化（壓總位移和最大位移）→ `OUTPUT_legal.pl`；Stage 2 自己寫的 SA 壓 HPWL、保持合法 → `OUTPUT.pl`。
執行方式 `dp INPUT_FILE OUTPUT_FILE`，stdout 印 4 行。交 `學號.tgz`（程式 + ReadMe）和 3 頁以內的 PDF 報告，截止 2026/11/04。

**先看這份就夠了。** 下面說每個資料夾放什麼、哪些東西很大不用看。

解法：[AI_Solutions](AI_Solutions/README.md) 有參考解、未加速最終版（不限時間）、**加速最終版**（全程 50 秒期限）；
照教學一步一步做到最終版的五個版本：[MyCode/README.md](MyCode/README.md)。

> **10/28 新規定（`Project3 Requirement(Latest Update_ 10_28 16_00) .pdf`）**：TA 在 VM 裡 `cmake -B build && cmake --build build`，
> 在 `build/` 裡執行 `./dp input001 output001`；**每筆 60 秒（含讀寫檔）**、8 GB、編譯 60 秒；交 `學號.zip` + `學號.pdf`（報告不放進 zip）。
> 沒有全程期限的程式在大設計（L1、L2）都會超過 60 秒；加速最終版和 `MyCode/v3` 起的版本都在 50 秒內做完。

## 量測規則：一次只跑一個 dp（2026-10-30 起）

**寫量測腳本時，每次只能有一個 `dp` 在機器上跑**（其他 CPU 閒著；checker、編譯可以放 E-core 16–19）。
不要再用「新版和基底同時跑在 CPU 0–7 / 8–15」的配對方式，也不要兩條 lane 同時跑不同的 case。

**測資只用四筆**：老師給的 `Testcases/testcases/ibm01`、`ibm05`（`bench.tgz`；裡面的 toy 只有 31 個 cell，不列入），
加上大設計 `Testcases/large/L1`（20 萬 cell）、`L2`（100 萬 cell）。holdout、edge、M1–M3 已刪除；`Testcases/ibm-place2/`（原樣下載，沒有 global placement）只用來測跑不跑得完，不拿來比品質。

原因：兩個 dp 同時跑會搶共用的 L3 快取（25 MB）、記憶體頻寬和功耗額度，而每個步驟都受 50 秒牆鐘期限限制，
被拖慢就是做的工作變少，品質直接變差（L1 實測同時跑時 HPWL 差到 70% 以上）。不同程式被拖慢的程度不一樣，所以「兩邊負載相同」的配對也不公平。

機器鎖：`Testcases/results/timing_global.lock`（`Testcases/results/machine_lock.py` 的 `machine_lock()`；量測腳本拿到鎖才跑 dp）。

## 資料夾

| 資料夾 / 檔案 | 放什麼 |
|---|---|
| `Project03.pdf` | 題目 |
| `Project3 Requirement(Latest Update_ 10_28 16_00) .pdf` | **繳交與執行規定（10/28）**：60 秒、8 GB、`build/dp`、`input001` / `output001` 命名、`學號.zip` |
| `Grading Criteria.pdf` | **Project 2** 的評分方式（放錯 lab，但可以看出 TA 用隱藏測資、跑不完就 0 分） |
| `GSRC Bookshelf Format ….txt` | 輸入 / 輸出格式 |
| `Example/` | 題目的 toy 範例和圖 |
| **`Testcases/`** | **所有測資，和所有程式跑出來的結果**（見下一節） |
| `AI_Solutions/` | AI 寫的解法（只有原始碼和說明）：參考解 `ClaudeOpus55/`、`Final_Unaccelerated/`、`Final_Accelerated/` |
| `AI_Workspace/` | `TUTORIAL.md`（一步一步自己做的教學）、`step1_example/`、`reference_report/`（範例報告） |
| `Grader/` | 檢查與評分工具：`tools/check_pl.py`（獨立檢查器）、`tools/local_grade.py`（模擬繳交）、`tools/exact_bound.py`（最佳解 / 下界） |
| `M11502102/` | **要交的程式**（學號資料夾） |
| `MyCode/` | 練習用的骨架，和照 `TUTORIAL.md` 做出來的 v1 ~ v5（[總表](MyCode/README.md)）：v1 Abacus、v2 SA、v3 微調 + 全程 50 秒期限、v4 過渡、v5 = 加速最終版 |
| `Report/` | 報告：跑實驗的腳本、表格、圖、草稿、完稿 |

## 規則：測資和跑出來的結果都放 `Testcases/`

```
Testcases/
├── bench.tgz、make_testcases.sh      ← 測資的來源和產生腳本（進 git）
├── testcases/  large/  ibm-place2/  ← 測資（不進 git，./make_testcases.sh [--large] 解壓 / 下載 / 產生）
│     ibm-place2/ = 10/28 規定說隱藏測資從這組改出來；原樣下載、沒改（所有 cell 在 (0,0)），說明在 Testcases/README.md
└── results/<誰跑的>/<實驗>/           ← 任何程式跑出來的東西（git 只追蹤小的紀錄檔，見下面）
    ├── local_grade/<學號>/            ← Grader/tools/local_grade.py 的預設輸出（不進 git）
    ├── manual/                        ← 自己手動跑的輸出放這裡（不進 git）
    ├── report/、reference_report/     ← Report/ 的腳本跑出來的原始結果（表格和圖還是在 Report/、reference_report/）
    ├── step2_grader/                  ← Grader/step2_grader/run_demo.py
    ├── MyCode/                        ← MyCode/ 五個版本的量測（run_versions.py）
    └── machine_lock.py                ← 機器鎖（量測腳本 import 它）
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
3. `MyCode/README.md`：五個版本每一步改了什麼、進步多少
4. 要改的那個版本的 `README.md` 和 `src/`
5. 教學：`AI_Workspace/TUTORIAL.md`

**不用看**（很大，而且都是跑出來的）：

| 路徑 | 大小 | 什麼時候才需要 |
|---|---:|---|
| `Testcases/results/` | git 裡只有小的紀錄檔 | 要查某個實驗的數字時，只看那個實驗的 `records.csv` / `record.csv` / `README` |
| `Testcases/testcases/`、`large/`、`ibm-place2/` | 約 370 MB | 只有程式要讀測資時（Bookshelf 文字檔） |
| `AI_Solutions/*/build/` | 編譯產物 | 不需要 |
| 任何 `.pl`、`.nets`、`.nodes` | | 用 `Grader/tools/check_pl.py` 讀，不要直接打開 |

搜尋程式碼時把它們排除：`grep -r PATTERN Lab3 --exclude-dir=Testcases --exclude-dir=build`，或 `rg PATTERN -g '!Testcases' -g '!build'`。

## 解法版本

| 資料夾 | 一句話 |
|---|---|
| `AI_Solutions/ClaudeOpus55/` | **參考解**：Abacus + 位移微調 + SA。教學和報告用它（`TUTORIAL.md`、`Report/run_everything.sh reference`、checker 示範）；L1、L2 跑不完 |
| `AI_Solutions/Final_Unaccelerated/` | **未加速最終版**：視窗 DP、最大位移搜尋、多執行緒 SA、polish；不限時間時品質最好，但 L2 要約 194 秒 |
| **`AI_Solutions/Final_Accelerated/`** | **加速最終版**：同樣的演算法加上全程 50 秒期限；四筆 ≤ 50 秒。`MyCode/v5_final` 是它的複本 |
| `MyCode/v1_abacus` ~ `v5_final` | 照 `TUTORIAL.md` 一步一步做到最終版的五個版本（[總表](MyCode/README.md)） |

## 常用指令

```bash
cd Lab3
./Testcases/make_testcases.sh                 # 產生測資（加 --large 再產生 20 萬 / 100 萬 cell）

cmake -S M11502102 -B M11502102/build && cmake --build M11502102/build
mkdir -p Testcases/results/manual
./M11502102/build/dp Testcases/testcases/toy/toy Testcases/results/manual/toy
python3 Grader/tools/check_pl.py Testcases/testcases/toy/toy Testcases/results/manual/toy_legal.pl

# 模擬繳交（打包 → 編譯 → 課程 3 筆 → 檢查），結果在 Testcases/results/local_grade/M11502102/
python3 Grader/tools/local_grade.py M11502102 M11502102 Testcases/testcases
# 再跑 ibm-place2 的 16 筆（原樣下載，cell 都在 (0,0)：只看跑不跑得完、合不合法、多快）
python3 Grader/tools/local_grade.py M11502102 M11502102 Testcases/ibm-place2 --work Testcases/results/local_grade/M11502102_ibm

# 報告用的實驗（原始結果 → Testcases/results/report/，表格和圖 → Report/tables、Report/figures）
cd Report && ./run_everything.sh
```
