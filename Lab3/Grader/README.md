# Grader — 檢查與評分工具

Project 3 沒有給 TA 的 grader / verifier，這裡是自己做的版本。

| 路徑 | 內容 |
|---|---|
| `tools/check_pl.py` | **獨立檢查器**：合法性（row、site、subrow、重疊、fixed 物件、方向）＋總位移、最大位移、HPWL |
| `tools/plot_pl.py` | 畫 placement（`--heat` 位移熱圖、`--arrows N` 位移最大的 N 個箭頭、`--window` 放大） |
| `tools/plot_trace.py` | 畫 SA 的 current / best-so-far 曲線（`dp --trace` 的 CSV） |
| `tools/local_grade.py` | 模擬繳交：打包 tgz → 解壓 → 編譯 → 每筆測資執行 → 檢查 |
| `tools/gen_bench.py` | 產生 Bookshelf 測資（`Testcases/holdout/` 就是它產生的） |
| `tools/exact_bound.py` | Stage 1 的**下界 / 最佳解**：site 層級的 LP（column generation）＝總位移下界；`--ilp` 小設計直接解出最佳解；`--max` 最大位移下界；`--cap -1` 限制最大位移不超過自己的（公平比較）。需要 scipy（`/home/evan/anaconda3/bin/python3`） |
| `tools/lower_bound.py` | 比較粗的 bin 版下界（很鬆，只當參考） |
| `step2_grader/` | 示範：檢查器對 1 個正確、10 個故意做錯的輸出的判斷（`record.csv`） |
| `local_grade_<SID>/` | `local_grade.py` 的輸出（不放進 git） |

## 常用指令

```bash
cd Lab3

# 檢查一個輸出
python3 Grader/tools/check_pl.py Testcases/testcases/ibm01/ibm01 out/ibm01_legal.pl

# 模擬繳交：全部 11 筆測資（3 筆課程 + 8 筆 holdout）
python3 Grader/tools/local_grade.py M11502102 M11502102 Testcases/testcases Testcases/holdout
```

`local_grade.py` 每筆測資會在 `local_grade_<SID>/results/<SID>/<bench>/` 執行 `dp <bench> output`（跟 PDF 範例一樣），
然後檢查：

1. 7 個輸出檔都有（`output.{aux,nodes,nets,wts,scl,pl}`、`output_legal.pl`）
2. stdout 有 4 行，而且數字跟 `check_pl.py` 算的一樣
3. `output_legal.pl`、`output.pl` 都合法
4. `Total HPWL (SA) <= Total HPWL (legal)`

結果在 `local_grade_<SID>/record.csv`（每筆）、`summary.csv`、`logs/<SID>.log`（編譯和執行的完整輸出）；
要交的檔案在 `local_grade_<SID>/submissions/<SID>.tgz`。

參考解的結果：11/11 PASS（ibm05 最久，約 41 秒）。

## 離最佳解還有多遠（Stage 1）

```bash
P=/home/evan/anaconda3/bin/python3          # 有 scipy 的環境
$P Grader/tools/exact_bound.py Testcases/testcases/toy/toy out/toy_legal.pl --ilp --max
$P Grader/tools/exact_bound.py Testcases/holdout/h01/h01 out/h01_legal.pl --ilp --cap -1
```

- `total displacement: LP lower bound` 一定 ≤ 任何合法解的總位移；`ILP` 是小設計的最佳解（toy、h01 幾秒內解完）。
- `--cap -1`：只允許位移 ≤ 自己的最大位移 → 「在最大位移不變的前提下，總位移最低能到多少」。
- 1000 個 cell 以上會很慢（h02 的 ILP 要 10 分鐘以上），大設計只能算 LP 或放棄。

結果整理在 `AI_Solutions/COMPARISON.md`。

## step2_grader

```bash
cd Grader/step2_grader && python3 run_demo.py
```

| case | 改了什麼 | check_pl.py |
|---|---|---|
| GOOD | 參考解在 h01 的 Stage 1 輸出 | LEGAL |
| BAD_OVERLAP | 一個 cell 搬到另一個 cell 上 | overlaps |
| BAD_OFF_SITE | x 偏半個 site | off-site |
| BAD_OFF_ROW | y 偏半個 row | not on a row |
| BAD_OUTSIDE | 超出 row 的右端 | outside every subrow |
| BAD_IN_GAP | 放在 split row 的缺口 | outside every subrow |
| BAD_ON_MACRO | 疊在 fixed macro 上 | overlaps m0 |
| BAD_FIXED_MOVED | fixed macro 被移動 | fixed moved |
| BAD_ORIENT | 方向 N → FS | orientation changed |
| BAD_MISSING_CELL | 少了一行 | missing |
| NO_OUTPUT | 沒有輸出檔 | not found |
