# MyCode — 從骨架一步一步做到最終版

跟 Lab2 一樣：`my_dp_skeleton/` 是起點，每完成一個階段存一個版本。每個版本都可以單獨編譯（`cmake -S . -B build && cmake --build build`），
執行方式都是 `dp INPUT_FILE OUTPUT_FILE`。每版的 README 寫「和上一版相比改了什麼、為什麼、實驗結果、觀察」。

| 版本 | 資料夾 | 內容 | ibm01 | ibm05 | L1 | L2 |
|---|---|---|---|---|---|---|
| 骨架 | [`my_dp_skeleton`](my_dp_skeleton/README.md) | 流程、資料結構、`TODO(M1)` ~ `TODO(M6)` | | | | |
| v1 | [`v1_abacus`](v1_abacus/README.md) | M1 ~ M4：讀檔、評分、Tetris → **Abacus** | 36.8M / 17,783 / — | 1.77M / 330 / — | 4.11e9 / 117,644 / — | > 300 s |
| v2 | [`v2_sa`](v2_sa/README.md) | + M6 **SA**（基本版 → optimal region、range limiter、時間控制、undo journal） | 同 v1 / 60.3M | 同 v1 / 10.32M | 同 v1 / 2.79e9 | > 300 s |
| v3 | [`v3_refine`](v3_refine/README.md) | + M5 **位移微調**、**全程 50 秒期限** | 27.2M / 10,578 / 60.0M | 1.43M / 218 / 10.31M | 4.11e9 / 59,194 / 2.64e9 | 2.09e9 / 14,298 / 2.57e9 |
| v4 | [`v4_match_polish`](v4_match_polish/README.md) | 過渡：+ 同大小匹配、**位移不變的 HPWL 交換**、polish、SA 用滿時間 | 27.0M / 10,578 / 54.7M | 1.42M / 218 / 10.33M | 3.99e9 / 59,639 / 2.21e9 | 2.00e9 / 14,331 / 1.97e9 |
| **v5** | [`v5_final`](v5_final/README.md) | **加速最終版**（`AI_Solutions/Final_Accelerated` 的複本） | **23.1M / 10,353 / 49.2M** | **1.34M / 200 / 9.58M** | **2.44e9 / 50,493 / 1.78e9** | **1.98e9 / 10,185 / 1.37e9** |

格子裡是「總位移 / 最大位移 / 最終 HPWL」（越小越好；v1 沒有 SA）。v3 起四筆都 ≤ 50 秒（10/28 規定 60 秒）；v1、v2 的 L2 卡在 Abacus，300 秒跑不完。
v3、v4、v5 的 ibm-place2 16 筆也都通過。

量測：一次只跑一個 dp、10/28 的方式 `./dp input001 output001`、`Grader/tools/check_pl.py` 檢查兩個輸出。
腳本和原始紀錄在 [`Testcases/results/MyCode/`](../Testcases/results/MyCode/README.md)（重跑：`python3 run_versions.py`）。

## 每一步解決了什麼

```
v1 Abacus        Tetris 只往右放 → ibm01 / L1 / L2 放不下；Abacus 的 cluster 整串移動 → 都放得下（L2 太慢）
 └ v2 SA         HPWL 開始下降；時間到就停的基本版在 L1 停在高溫 → 改成溫度一定降到底，L1 −41%
    └ v3         位移微調 → 最大位移 −34 ~ 50%；全程期限 + Abacus / 微調 / SA 加速 → L2 48 秒跑完
       └ v4      位移不變交換 → L1、L2 的 legal HPWL −53% / −29%；SA 用滿時間 → ibm01 −9%
          └ v5   加速最終版：視窗 DP、多執行緒最大位移搜尋與 SA、依預測分配時間 → HPWL 再 −7 ~ 30%
```

v1 ~ v4 是照 `AI_Workspace/TUTORIAL.md` 的里程碑寫的，單執行緒、約 1,700 ~ 2,200 行；v5 約 9,600 行、多執行緒。
