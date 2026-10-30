# Final_Unaccelerated — 未加速最終版

不限時間的版本：每個階段都做到收斂或固定的時間預算（Stage 1 約 90 秒、SA 60 秒、polish 30 秒），所以品質最好，
但大設計超過 10/28 的 60 秒規定（L1 約 186 秒、L2 約 194 秒）。要交的版本請用 [Final_Accelerated](../Final_Accelerated/README.md)。

## 編譯與執行

```bash
cmake -B build && cmake --build build
./build/dp INPUT_FILE OUTPUT_FILE [options]      # 不帶參數會列出所有選項
```

或 `make`（產生 `./dp`）。常用選項：`--time SEC`（SA 時間）、`--seed N`、`--trace FILE`、`-v`。

## 演算法

| 階段 | 步驟 | 檔案 |
|---|---|---|
| Stage 1 | 流量式 spreading（擁擠的地方先往外分散）→ Abacus 合法化 | `legalizer.cpp` |
| | 位移微調：insert / swap / evict、同寬 cell 匹配、每個 row 精確重排 | `legalizer.cpp`、`optimizer.cpp` |
| | 最大位移搜尋：重新分配 row 再精確重排（不行就改用 site 層級修復）、ejection chain | `capsearch.cpp`、`chain.cpp`、`global.cpp` |
| | 視窗 DP：1 ~ 3 個 row、最多 12 個 cell 一起精確重排，掃到收斂 | `window.cpp` |
| | pareto：總位移不增加的瓶頸匹配 | `optimizer.cpp` |
| Stage 2 | 多執行緒 SA：晶片切成 row bands 各跑各的，定期合併、輪替邊界 | `annealer.cpp`、`sa_*.hpp` |
| | polish：slide、relocate、row 內精確重排 | `optimizer.cpp`、`polish_cost.hpp` |
| 小設計 | 6 萬 cell 以內：plain 和 spread 兩條 pipeline 同時跑，選比較好的 | `main.cpp` |

## 結果（一次只跑一個 dp）

| 測資 | 總位移 | 最大位移 | 最終 HPWL | 時間 |
|---|---:|---:|---:|---:|
| ibm01 | 23,101,703 | 10,427.0 | 48,280,236 | 75 s |
| L1 | 2,421,550,396 | 50,349.9 | 1,697,387,334 | 186 s |
| L2 | 1,950,992,661 | 12,206.0 | 1,530,095,661 | 194 s |
