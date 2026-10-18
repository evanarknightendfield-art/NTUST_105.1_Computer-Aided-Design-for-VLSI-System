# CodexWindowDP：兩個 row 的聯合子集合動態規劃

本版在 `ClaudeOpus55_v3` 的合法化與 SA 上，整合 `CodexParetoDP` 的瓶頸匹配及 HPWL 收尾，**新增自己實作的兩個 row 聯合動態規劃**。新增核心為 `src/window.cpp`，不是獨立重寫全部 placer，也不宣稱發明了動態規劃這個方法。其餘五個版本保留原樣。

實測比較與「是否最佳」見 [COMPARISON.md](COMPARISON.md)。閱讀範圍、評分文件與下界工具的限制見 [READING_NOTES.md](READING_NOTES.md)。

## 編譯及執行

只需要 C++17 編譯器，placer 不依賴 Python、SciPy 或外部最佳化器。

```bash
make
./dp ../../Testcases/testcases/ibm01/ibm01 out/ibm01 --seed 1 --trace out/trace.csv -v
# 或
cmake -S . -B build
cmake --build build
./build/dp ../../Testcases/testcases/toy/toy out/toy
```

輸出題目要求的 7 個檔案；stdout 只有總位移、最大位移、合法化 HPWL、Stage 2 HPWL 四行。進度與各階段改善量寫到 stderr。保留 orientation、fixed 座標、site 對齊及 subrow 邊界。也處理整個 row 被 fixed macro 擋住、沒有有效 net、全 fixed 等情況。

## Stage 1

1. v3 的 Abacus、spreading 自動選擇、row L1 最佳化、匹配與局部修整。
2. ParetoDP 的同尺寸瓶頸匹配：在群組總位移不增加下，二分最大位移上限，再用 Hungarian 最小化總位移。
3. **兩個相鄰 row 的聯合最佳化**：每個 row 最多取 4 顆相鄰 cell，共最多 8 顆；視窗外的 cell 固定。可以一起更換 row、交換順序、重新分配空白與 site，並非只能同寬交換。

令選定的 cell 集合為 `U`，視窗第 `r` 個 row 長度為 `L_r`；`w[r,i]` 是 cell 在該 row 所占 site 數。用目前全域最大位移 `B` 當上限：

```text
c[r,i,x] = |row_origin + x * spacing - GP_x[i]| + |row_y - GP_y[i]|
            若不合高度、超出視窗或 c > B，則設為無限大

F_r(S, x) = 把集合 S 全部放在 row r 的前 x 個 site 內的最小總成本
F_r(空集合, x) = 0
F_r(S, x) = min(
    F_r(S, x-1),
    min_{i in S, w[r,i] <= x} F_r(S\{i}, x-w[r,i]) + c[r,i,x-w[r,i]]
)

最佳兩 row 分配 = min_{S subset U} F_0(S,L_0) + F_1(U\S,L_1)
```

第一個分支保留空白；第二個分支把 `i` 當最後一顆，枚舉不同順序。最後枚舉互補子集合，允許不等數量、不同寬度的 cell 換 row。

**為什麼視窗內是精確的？** 任意合法單 row 排列，不是最後一個 site 空著，就是有最後一顆 cell 在該 site 結束；移除它得到更小集合的同類子問題。這涵蓋所有合法順序及間距。兩個 row 的 cell 集合必定互補，因此最後一步也完整。原本配置一直是可行候選；只接受嚴格減少總位移的解，且每顆位移不超過 `B`。這保證新增 Stage 1 不會使 v3 起點的總位移或最大位移變差。

**保證範圍：** 只對選出的兩個 row、選定 cells、截取的 site 區間及固定最大位移上限精確。沒有保證整個電路、最大位移目標、或不同視窗的聯合全域最佳。視窗之間採循序更新，兩輪掃描方向及錨點錯開。

每個視窗時間 `O(k * 2^k * (L_0+L_1))`、空間 `O(2^k * (L_0+L_1))`，預設 `k<=8`、`L_r<=256`。超寬視窗直接略過，不裁掉原本 cell，也不宣告原問題不可行。不要任意放大 k：多兩顆的成本約四倍。

## Stage 2

從實際輸出的 Stage 1 位置開始 SA，保留 v3 的移動、交換、三顆重排、低溫 linear placement / independent set matching，以及最佳解回復。預設 effort 與 v3 相同：每 cell 5000 次、至少 500 萬、最多 6000 萬，SA 時間上限 60 秒。

接著執行 ParetoDP 的三輪確定性改善，再做兩輪新視窗最佳化：

- 先把其他 pin 的位置暫時固定，計算每顆 cell 在各 site 的個別 HPWL 成本，交給同一個 DP 產生候選。
- 視窗內若有共享 net，這些個別成本**不能相加當成真正 HPWL**，也不能宣稱 DP 解出該視窗的精確 HPWL 最佳解。
- 因此對每個候選重新計算所有受影響 net 的聯集；只有真正 HPWL 嚴格下降才接受。其他候選完整回復，不影響 row 清單。

SA 之後的收尾不會增加 HPWL，但 Stage 1 改變會使 SA 經過不同路徑，所以與 v3 比較仍可能退步；請看完整測試表，而非只看成功案例。`Total HPWL (SA)` 是整個 Stage 2 最終值；`trace.csv` 是純 SA，`trace.csv.polish.csv` 是 ParetoDP 收尾，新視窗各輪的前後值在 stderr 的 `[window-hpwl]`。

最後加入另一個新核心：**同 row 的精確 HPWL 排序 DP**。每個視窗最多 8 顆，保留原本依序排列的 gap 大小，枚舉全部排列而非只交換三顆。集合 `S` 的寬度總和與已填 cell 數決定下一顆的起點。每條 net 的 HPWL-x 只需在第一個視窗 pin 出現時計入 `-min(outside_min, center)`，最後一個出現時計入 `+max(outside_max, center)`；判斷第一／最後只需要子集合 mask。因此狀態無須記住前面每個 pin 的位置，仍可**精確處理共享 net**。複雜度約 `O(2^k*k*m)`，m 是受影響 net 數。

這個精確性限於同 row、原 gap 序列與固定視窗邊界；不能解釋成完整 HPWL 全域最優。實作也重算真正受影響 HPWL 才接受，日誌為 `[order-hpwl]`。150 個含共享 net、可變寬度、分數座標、外部 pin 的小視窗，與全部排列窮舉核對。

## 參數與時間

| 參數 | 預設 | 功能 |
|---|---:|---|
| `--window-passes N` | 2 | 位移視窗掃描輪數；0 關閉 |
| `--hpwl-window-passes N` | 2 | HPWL 視窗掃描輪數；0 關閉 |
| `--order-passes N` | 2 | 同 row 精確 HPWL 排序輪數；0 關閉 |
| `--window-cells N` | 4 | 每個 row 最多幾顆；允許 1–5 |
| `--window-sites N` | 256 | 每個 row 視窗最多 site 數 |
| `--no-pareto` | — | 關閉額外瓶頸匹配 |
| `--no-polish` | — | 關閉 ParetoDP HPWL 收尾；新視窗由自己的參數控制 |
| `--effort K` | 5000 | 明確指定時解除預設 6000 萬次上限 |
| `--min-moves N` | 5000000 | SA 最少嘗試次數 |
| `--time SEC` | 60 | **只限制 SA**；不包含 Stage 1、收尾及 I/O |
| `--seed N` | 1 | 可重現的隨機種子 |
| `--no-sa` | — | 跳過整個 Stage 2 |

`--no-refine` 僅控制繼承的 v3 修整；要關掉全部新增 Stage 1，還需 `--no-pareto --window-passes 0`。

`--time` 不是整個程式的硬性截止時間。時間敏感時可以降低 effort、視窗輪數或關閉部分收尾。與既有版本相同，在 SA 真的碰到時間限制時，結果會受機器速度影響。

## 測試及重現

已實際完成：六版本 66/66 次比較、20/20 次額外 seed、重新打包編譯後 toy 1/1、400 組跨 row DP 窮舉、150 組共享 net 排序窮舉、250 組指派／瓶頸及 100 組隨機 placement 測試。窮舉／隨機測試也以 ASan/UBSan 跑過；此環境不支援 LeakSanitizer，沒有宣稱做漏記憶體驗證。紀錄見 `results/verification.txt`，完整原始測試數據保留在 `results/`。

```bash
# 六個版本、全部 11 筆、共同 SA 嘗試預算、獨立檢查器
python3 tests/compare.py
# 新版與 v3，三個 seed 的補充比較
python3 tests/compare.py --labels ClaudeOpus55_v3,CodexWindowDP --seeds 1,2,3 --benches toy,h01,h02,h03,ibm01 --suite multiseed
# 特殊輸入與防止覆寫輸入
python3 tests/edge_cases.py

# 視窗 DP 與完整窮舉核對；任意成本、禁止位置、不同寬度
g++ -std=c++17 -O2 tests/window_test.cpp src/window.cpp src/placedb.cpp -o /tmp/window_test
/tmp/window_test
# 匹配、位移及 HPWL 綜合隨機測試
g++ -std=c++17 -O2 tests/optimizer_test.cpp src/optimizer.cpp src/window.cpp src/placedb.cpp -o /tmp/optimizer_test
/tmp/optimizer_test
```

`tests/full_bound.py` 僅供小設計驗證，需 SciPy。完整枚舉位移上限內的所有合法位置，以 LP / 整數規劃檢驗離最佳值多遠；不參與解題執行。

## 來源

- 課程 `L06_placement1.pdf`：SA、range limiter；`L06_1_sa.pdf`：Metropolis 接受準則與降溫。
- 課程 `L07_placement2.pdf`，12–22 頁：固定順序的 linear placement；24–28 頁：window interleaving；30、44–48 頁：子集合選擇、多 row 視窗及分區最佳化。新 DP 把固定前綴改成子集合，再聯合兩個 row；不是直接實作課本的雙序列 interleaving。
- `ClaudeOpus55_v3/src/`：`legalizer.*`、`annealer.*` 是本版基礎，保留出處與註解。
- `CodexParetoDP/src/`：資料結構、I/O 防護、`optimizer.*` 及 main 流程；其匹配與獨立集合概念參考來源見該版 README。
- 新增 `window.*`、完整列枚舉證書工具與本輪測試分析由本次實作。沒有為單一測資寫特殊分支或讀取預先算好的解。
