# Final_Accelerated — 加速最終版

[Final_Unaccelerated](../Final_Unaccelerated/README.md) 的演算法，加上**全程 50 秒期限**（含讀寫檔），守住 10/28 每筆 60 秒規定。
多執行緒，可用 CPU 同時看 affinity mask 和 cgroup 配額。`MyCode/v5_final` 是它的複本。

## 編譯與執行（TA 的方式）

```bash
cmake -B build && cmake --build build
cd build && ./dp input001 output001
```

stdout 四行：legal 總位移、legal 最大位移、legal HPWL、最終 HPWL。診斷寫在 stderr。選項很多（`./dp` 不帶參數會列出），交作業用預設就好；
常用的：`--seed N`、`--trace FILE`、`--deadline SEC`、`--print-options`。

## 演算法流程

| 階段 | 步驟 |
|---|---|
| Stage 1（`output001_legal.pl`） | Abacus 合法化（四種計分模式，大設計用近似下界加速）→ 最大位移搜尋：逐步壓低上限，搭配 Hungarian matching 與 evict（多執行緒）→ pareto 瓶頸匹配 → 視窗 DP（依收益率停止）→ chain / cap 修復（先試探，收益不夠就跳過）→ 位移不變的 HPWL 交換 |
| Stage 2（`output001.pl`） | 多執行緒 SA 加 polish；預估 SA 時間不夠的設計（例如 100 萬 cell 的 L2）只做 polish |
| 時間分配 | 先預測 Stage 2 能做多少，再決定 Stage 1 各步驟的時間；每一步都可以在期限前中斷 |
| 小設計 | 6 萬 cell 以內：plain 和 spread 兩條 pipeline 平行比較 |

## 結果（一次只跑一個 dp，`Testcases/results/MyCode/records.csv` 的 `v5_final`）

| 測資 | 總位移 | 最大位移 | 最終 HPWL | 時間 |
|---|---:|---:|---:|---:|
| ibm01 | 23,141,044.4 | 10,352.5 | 49,230,696 | 45.7 s |
| ibm05 | 1,335,567.4 | 199.5 | 9,580,967 | 47.2 s |
| L1 | 2,438,619,191.0 | 50,492.5 | 1,778,928,272.7 | 49.5 s |
| L2 | 1,978,269,471.6 | 10,185.0 | 1,369,676,655 | 49.4 s |

ibm-place2 的 16 筆（10/28 HINT）也全部合法、≤ 49.5 秒。

## 已知限制

- **機器變慢或 CPU 變少時品質會下降**：各階段都受時間限制，做的工作量跟著機器速度走（例如 4 顆 CPU 時 L1 的 HPWL 約 1.86e9）。
- 少數步驟的第一輪沒有時間上限，CPU 很少時可能壓縮到後面 polish 的時間；50 秒期限離 60 秒規定留了 10 秒。
