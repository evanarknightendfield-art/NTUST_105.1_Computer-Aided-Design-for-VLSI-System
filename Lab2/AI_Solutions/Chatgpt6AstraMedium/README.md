# M11502102_Chatgpt6AstraMedium — Clock Buffer Insertion

本版本以 `M11502102_ClaudeOpus55Ultracode` 的 C++17 程式為基礎，保留建樹、合法性交易、型號查表與輸出格式，改進位置搜尋、幾何求交與最佳解保存。原版資料夾沒有修改。

## 編譯與執行

```bash
conda activate eda  # 本機的 cmake 在此環境；評分機不需要這一行
cmake -S . -B build
cmake --build build -j 4
./build/cbi ../testcases/input001.cbi output001.cbi
./build/cbi ../testcases/input010.cbi output010.cbi -t 5 -s 1
```

執行介面與原版一致：`cbi INPUT OUTPUT [-t seconds] [-s seed] [-v] [-d PREFIX]`。
預設小於等於 12 個 sinks 使用 5 秒，其餘 `min(30, 10 + N/100)` 秒。`-t` 是啟發式時間預算，建樹、驗證與 I/O 不保證能在任意短的預算內完成。
stdout 輸出 `T_max: ..., T_min: ..., Score: ...`，階段紀錄輸出到 stderr。

## 演算法差異

1. **全樹增量位置評估（`src/optimizer.cpp`）**
   原版 Retarget 對「超過目標時間」與「低於目標時間」施加不同的局部懲罰。
   新版先沿 buffer 的祖先蒐集兄弟子樹的最早、最晚 arrival，得到不受移動影響的子樹外範圍。
   對每個候選座標，重新計算該 buffer 下所有 children 的 arrival 範圍，與子樹外範圍合併，得到全樹 skew。
   成本同時計入被移動 buffer 與其 parent 的最便宜合法型號，避免忽略 parent 升級成本。
   用子樹一階、二階統計更新全樹標準差，使搜尋目標與退火 energy 一致。
   預處理為 O(路徑上各祖先的 fanout 總和)，每個座標的統計計算為 O(buffer fanout)，型號查表另需二分搜尋。
   座標仍採粗網格加局部細搜，因此評估值精確不代表找到全域最佳座標。

2. **互補搜尋與解析幾何求交**
   保留原版 Retarget 的延遲目標搜尋；該操作中 75% 使用原版方法、25% 使用全樹增量評估。
   全程維持原版退火輪數與溫度流程，避免提早切成純貪婪搜尋造成退步。
   `src/diamond.h` 將兩個 Manhattan 菱形的邊分成線性區間，直接解距離方程與晶片邊界不等式。
   原版沿半徑逐點掃描 O(a)；新版用常數個區間求交，只在可行但被佔用的點上繼續找，
   複雜度為 O(1 + 搜尋中遇到的佔用點數)，與巨大座標半徑解耦。
   選點仍是 x 最小、同 x 時 y 最大，與原版完全相同。

3. **立即保存已接受的最佳解**
   原版最多每隔 `max(256, 節點數/50)` 次操作保存一次，期間遇到的較佳解可能被後续退火覆蓋。
   新版每次接受後若 Score 創新低就保存。代價是每次保存需 O(節點數) 複製；大型案例不保證更快。

Score = buffer 總成本 + w_skew × (T_max − T_min)，越低越好。
本方法仍屬啟發式，沒有全域最佳保證，也不保證每筆測資都勝過原版。

## 驗證及重現比較

`tests/compare.py` 交替執行順序、固定相同時間和 seed，清除 `CBI_*` 實驗環境變數，並使用課程 `grader/grader/verifier.py` 驗證每份輸出與 stdout 分數。
原始輸出、stderr、逐筆 CSV 及摘要存於 `experiments/`。時間制退火會受到機器負載影響，即使 seed 相同，結果也可能不同。

```bash
python3 tests/compare.py --seconds 3 --seeds 1,2 --out experiments/reproduced_testcases
python3 tests/make_holdout.py
python3 tests/compare.py --inputs experiments/final_holdout_inputs --seconds 3 --seeds 1 --out experiments/reproduced_holdout
```

增量公式檢查可獨立啟用；每次全域位置搜尋後，會將預測 energy 與真正交易後的全樹 energy 比對：

```bash
cmake -S . -B build-check -DASTRA_VERIFY_PLACEMENT=ON \
  -DCMAKE_CXX_FLAGS='-fsanitize=undefined -fno-sanitize-recover=all'
cmake --build build-check -j 4
```

`tests/` 依賴同一份 Lab2 中的測資產生器及課程 verifier；正式 C++ 執行檔僅依賴標準函式庫。

正式比較請看 [COMPARISON.md](COMPARISON.md)。開發期曾測試「完全替換 Retarget + 最後一輪純 Score 貪婪」，既有測資及第一組新測資平均皆略差，故未採用。`pilot`、`polish_pilot`、`testcases_3s`、`holdout_3s` 為先前版本紀錄，不能當作最終版結果。

解析幾何的對照測試：

```bash
g++ -O2 -std=c++17 -I src tests/diamond_test.cpp src/tree.cpp src/problem.cpp -o build/diamond_test
./build/diamond_test
python3 tests/check_predictions.py build-check/cbi
```

最終驗證：42 組同時間比較（84 份輸出）全部 VALID；25 萬組解析幾何對照結果一致；39 筆開啟增量公式檢查與 UBSan 的案例全部通過。3 秒預算下，既有測資 Score 幾何平均下降 0.68%，全新測資約持平。完整紀錄見 `experiments/validation.txt` 與 `COMPARISON.md`。
