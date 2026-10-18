# Report — 跑實驗、表格、圖、報告（報告用）

這個資料夾**不要放進交作業的 tgz**。

| 資料夾 / 檔案 | 內容 |
|---|---|
| `draft/` | 你的報告初稿（docx / pdf） |
| `completed/` | 你的最終版報告 |
| `scripts/run_experiments.py` | 跑實驗，原始結果（每次執行一個 `.json`）放 `results/<組>/` |
| `scripts/make_assets.py` | `results/` → `tables/*.md` 和 `figures/*.png` |
| `run_everything.sh` | 上面兩個一起跑（含編譯、產生測資） |

參考解的實驗、表格、圖和範例報告在 `../AI_Workspace/reference_report/`（同樣的腳本跑出來的）。

## 一行跑完

```bash
conda activate eda
./run_everything.sh              # 你的程式（Lab3/M11502102），約 1 ~ 2 分鐘
./run_everything.sh reference    # 參考解的全部實驗（6 個平行，約 5 分鐘）
```

`run_everything.sh` 做的事：

1. 編譯（`cmake -S M11502102 -B M11502102/build`）
2. `Testcases/testcases/`、`Testcases/holdout/` 不存在的話跑 `Testcases/make_testcases.sh`
3. `scripts/run_experiments.py`：預設跑 `main`、`holdout` 兩組
4. `scripts/make_assets.py`：表格到 `tables/`、圖到 `figures/`

## 實驗

| 組 | 內容 | 你的程式要支援的選項 | 結果 |
|---|---|---|---|
| `main` | toy / ibm01 / ibm05 預設參數，一次跑一個（時間才準），含 SA trace | `--trace FILE` | `tables/main.md`、`figures/sa_trace_*.png`、`toy_legal.png`、`ibm01_legal_heat.png` |
| `holdout` | h01 ~ h08 預設參數 | — | `tables/holdout.md` |
| `stage1` | Tetris → Abacus（兩種 cost）→ + 微調 → + evict | `--legalizer` `--own-cost` `--no-refine` `--no-evict` `--no-sa` | `tables/stage1.md` |
| `sa` | SA 對照：貪心、p0、optimal region、range limiter | `--p0` `--target` `--clamp-target` `--win0` `--minwin` `--time` | `tables/sa_ablation.md`、`figures/sa_compare_*.png` |
| `effort` | 每個 cell 125 ~ 4000 次嘗試 | `--effort` | `tables/effort.md`、`figures/effort.png` |
| `seeds` | 5 個 seed 的變異 | `--seed` | `tables/seeds.md` |

只跑某幾組：`ONLY=main,stage1 ./run_everything.sh`。你的程式選項不一樣的話，改 `scripts/run_experiments.py` 最上面的 `STAGE1`、`SA` 兩個表。

手動執行（例如跑別的版本）：

```bash
cd scripts
python3 run_experiments.py --dp ../../MyCode/v2_abacus/build/dp --out ../v2 --only main
python3 make_assets.py --root ../v2
```

## 報告架構建議（≤ 3 頁）

| 節 | 篇幅 | 內容 | 放什麼 |
|---|---|---|---|
| I. Stage 1 | 0.9 頁 | Tetris → Abacus（cluster 公式）→ 微調；為什麼一定合法 | `tables/stage1.md`、`figures/ibm01_legal_heat.png` |
| II. Stage 2 | 0.8 頁 | 狀態、move、為什麼每一步都合法、增量計算、溫度、range limiter | — |
| III. 實驗 | 1 頁 | 主表（位移、HPWL legal → SA、時間）、SA 曲線、SA 對照表、holdout | `tables/main.md`、`figures/sa_compare_ibm01.png`、`tables/sa_ablation.md`、`tables/holdout.md` |
| IV. 觀察 | 0.3 頁 | 熱點決定最大位移、位移 vs HPWL、Tetris 為什麼在 ibm01 失敗 | — |

範例：`../AI_Workspace/reference_report/reference_report.md`。

**注意：** holdout 的 global placement 是合成的（沒有經過真正的 placer），SA 降 HPWL 的幅度會比 ibm 大很多，引用時要說明。
