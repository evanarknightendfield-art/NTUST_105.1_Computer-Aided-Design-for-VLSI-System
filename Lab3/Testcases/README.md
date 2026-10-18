# Testcases

測資資料夾不放進 git（`testcases/` 從 `bench.tgz` 解出來，`holdout/` 是程式產生的）。clone 下來後先跑：

```bash
./make_testcases.sh
```

| 資料夾 | 內容 | 產生方式 |
|---|---|---|
| `testcases/` | 課程的 3 筆：toy（31 cells）、ibm01（12,028）、ibm05（28,146） | 解壓 `bench.tgz` |
| `holdout/` | 8 筆報告 / 穩健度測試用的新測資 h01–h08（300 ~ 25,000 cells） | `Grader/tools/gen_bench.py --suite`（seed 2026） |

每筆是一個資料夾 `X/X.{aux,nodes,nets,wts,pl,scl}`，執行方式：`dp testcases/ibm01/ibm01 out/ibm01`。

## holdout 有什麼課程測資沒有的東西

| | cells | 使用率* | macros | pads | 熱點 | split rows | 座標 |
|---|---:|---:|---:|---:|---:|---:|---|
| h01 | 300 | 0.48 | 2 | 16 | 1 | 20% | site 1、row 16 |
| h02 | 1,000 | 0.67 | 3 | 40 | 1 | 30% | site 66、row 504、原點為負 |
| h03 | 2,000 | 0.80 | 0 | 0 | 2 | 0% | site 0.2、row 1.6（小數座標） |
| h04 | 4,000 | 0.83 | 4 | 80 | 2 | 20% | site 1、row 16 |
| h05 | 6,000 | 0.58 | 6 | 120 | 0 | 50% | site 66、row 504 |
| h06 | 10,000 | 0.87 | 2 | 160 | 3 | 10% | site 1、row 16 |
| h07 | 15,000 | 0.74 | 5 | 200 | 2 | 20% | site 0.2、row 1.6 |
| h08 | 25,000 | 0.81 | 3 | 300 | 2 | 10% | site 66、row 504 |

\* cell 面積 ÷ 全部 row 面積（macro 底下的 row 也算在內，所以扣掉 macro 之後實際更擠）。

- **macro**：fixed、跨好幾個 row、有的沒有對齊 row → row 要被切成好幾個 segment
- **pad**：晶片外 1×1 的 terminal，`.pl` 沒有 `/FIXED`（跟 ibm05 一樣，要看 `.nodes` 的 `terminal`）
- **split row**：一個 row 有兩個 `SubrowOrigin`，中間有缺口
- 關鍵字大小寫（`NumSites` / `Numsites`）、net 有沒有名字，每筆不一樣

global placement 是合成的（隨機 + 熱點），沒有經過真正的 placer 最佳化，所以 SA 降 HPWL 的幅度會比 ibm 大很多，報告引用時要說明。
