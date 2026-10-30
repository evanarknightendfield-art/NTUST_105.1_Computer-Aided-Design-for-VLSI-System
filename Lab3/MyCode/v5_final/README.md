# v5 — 加速最終版（`AI_Solutions/Final_Accelerated/` 的複本）

> `src/`、`CMakeLists.txt`、`Makefile` 和 `AI_Solutions/Final_Accelerated/` **逐檔相同**。
> 約 9,600 行 C++，多執行緒，全程 50 秒期限。
> 四筆都最好：和 v4 比，HPWL ibm01 −10%、ibm05 −7%、L1 −20%、L2 −30%，L2 最大位移 −29%、L1 總位移 −39%。
> 演算法說明在 [`AI_Solutions/Final_Accelerated/README.md`](../../AI_Solutions/Final_Accelerated/README.md)。

## 編譯與執行

```bash
cmake -B build && cmake --build build
cd build && ./dp input001 output001
```

stdout 四行（legal 總位移、legal 最大位移、legal HPWL、最終 HPWL），診斷寫在 stderr。選項很多（`./dp` 不帶參數會列出），交作業用預設就好；
常用的：`--seed N`、`--trace FILE`、`--print-options`。

## 和 v4 相比多了什麼

v4 的三個步驟（匹配、位移不變交換、polish）這一版都有，而且做得更多。主要差在：

| 階段 | v4 | v5 多做的 |
|---|---|---|
| Stage 1 位移 | Abacus → 微調（insert / swap / evict）→ 16 個一組的匹配 | Abacus 加速（四種計分模式）；最大位移用 **Hungarian matching + evict** 逐步壓、多執行緒（`legalizer.cpp`、`capsearch.cpp`、`chain.cpp`）；**pareto**：總位移不增加的瓶頸匹配（`optimizer.cpp`）；**視窗 DP**：1 ~ 3 個 row、最多 12 個 cell 一起**精確**重排（`window.cpp`），依收益率停止 |
| Stage 1 HPWL | 位移不變交換（單執行緒） | 同一個想法，平行提案、依每輪收益比例停止（`nswap.cpp`）；視窗 DP 也把 HPWL 當次要目標 |
| Stage 2 | 單執行緒 SA → polish | **多執行緒 SA**：晶片切成 row bands 各跑各的，定期合併、輪替邊界（`annealer.cpp`、`sa_*.hpp`）；polish 有 slide、relocate、row 內精確重排（`optimizer.cpp`、`polish_cost.hpp`）；**L2 這種 SA 時間不夠的設計直接只做 polish** |
| 時間 | 固定比例分配 | 依進度和預測分配：先預測 Stage 2 能做多少，再決定 Stage 1 各步驟的時間 |
| 小設計 | | 6 萬 cell 以內：plain 和 spread 兩條 pipeline 平行跑，選比較好的 |
| 強健性 | | 可用 CPU 同時看 affinity mask 和 cgroup 配額 |

## 實驗結果

### v4 → v5（四筆）

| 測資 | | 總位移 | 最大位移 | HPWL legal | HPWL 最終 | 時間 |
|---|---|---:|---:|---:|---:|---:|
| ibm01 | v4 | 26,995,105.3 | 10,578.3 | 80,965,878 | 54,746,418 | 42.2 s |
| | **v5** | **23,141,044.4** | **10,352.5** | 81,766,530 | **49,230,696** | 45.7 s |
| ibm05 | v4 | 1,415,026.6 | 217.5 | 11,413,099.5 | 10,332,234 | 42.8 s |
| | **v5** | **1,335,567.4** | **199.5** | 11,442,445.5 | **9,580,967** | 47.2 s |
| L1 | v4 | 3,989,140,332.7 | 59,638.5 | 2,473,075,402.0 | 2,210,324,604.5 | 45.5 s |
| | **v5** | **2,438,619,191.0** | **50,492.5** | 4,622,472,280.4 | **1,778,928,272.7** | 49.5 s |
| L2 | v4 | 2,000,085,152.6 | 14,330.9 | 2,018,180,048 | 1,967,871,265.5 | 48.1 s |
| | **v5** | **1,978,269,471.6** | **10,185.0** | **1,393,518,257.5** | **1,369,676,655** | 49.4 s |

| v5 ÷ v4 | 總位移 | 最大位移 | HPWL 最終 |
|---|---:|---:|---:|
| ibm01 | −14.3% | −2.1% | −10.1% |
| ibm05 | −5.6% | −8.3% | −7.3% |
| L1 | **−38.9%** | −15.3% | **−19.5%** |
| L2 | −1.1% | **−28.9%** | **−30.4%** |

全部 LEGAL，stdout 和 `check_pl.py` 一致。期限下各步驟做多少工作取決於機器當下的速度，同一個輸入重跑會有約 1% 的差異。

### ibm-place2（10/28 HINT 的 16 筆）

三版都 16 筆全部合法；v5 最久 49.5 秒。最終 HPWL 的幾何平均 **v5 ÷ v4 = 0.602**（每一筆都比 v4 好 26 ~ 54%），
逐筆的數字見 [v4 README](../v4_match_polish/README.md) 的 ibm-place2 一節。

## 觀察

- **L1 的 legal HPWL 比 v4 差、最終卻比較好：** v5 在 Stage 1 把時間花在壓位移（視窗 DP 讓 L1 總位移少 39%），legal HPWL 4.62e9；
  v4 把一半時間給位移不變交換，legal HPWL 2.47e9，但 v5 的多執行緒 SA 後段追回來而且超過。兩個 stage 的目標不同，要看最終的數字。
- **L2 最大位移少 29%：** 主要來自 Hungarian matching + evict 的多執行緒最大位移搜尋；v3、v4 的單執行緒微調在 L2 被時間切斷（只做了幾百步）。
- **代價：** 程式量是 v4 的 4 倍多，多執行緒；而且幾乎每一步都受期限控制，**機器越慢或 CPU 越少品質越差**（4 顆 CPU 時 L1 HPWL 約 1.86e9）。
  v3、v4 是單執行緒，品質受機器影響比較小，但上限也比較低。
