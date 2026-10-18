# 閱讀與評分文件核對

## 規格優先順序

本次以 `Lab3/Project03.pdf` 為題目：Stage 1 最小化總／最大 Manhattan 位移；Stage 2 必須從 Stage 1 用 SA 最小化 cell-center、unweighted HPWL。兩階段維持合法、固定點與方向，輸出 7 個 Bookshelf 檔與 4 行數值。

**特別核對 `Lab3/Grading Criteria.pdf`：它雖放在 Lab3，內文卻明確是 Project 2 的 clock-network 規則。** 第一頁提及 ISPD 2010 Clock Network Synthesis、sinks、`project2.pdf`、buffer library 及 skew。其「通過 6/8 才進 A 組」、「保底 40」、「程式分數乘 0.9」不能直接推成 Project 3 的位移／HPWL 評分式。後續頁是 clock-tree 測資圖，也沒有 Lab3 權重。本文不臆測 Lab3 分數；比較合法性、總位移、最大位移、HPWL、時間各自的數值。

`Grader/README.md` 及 `tools/local_grade.py` 自述是自製檢查器，並非 TA 官方 grader。`Testcases/holdout/h01..h08` 是 `gen_bench.py` 產生的測資，不能當成 Grading Criteria 的 8 個官方 clock 測資。通過本地測試不等於官方得分保證。

## 各版程式的實際差異

| 版本 | 閱讀重點 | 新版如何採用／處理 |
|---|---|---|
| ClaudeOpus55 | Abacus row 選擇、insert/swap/evict、SA 增量 HPWL 與 undo journal | 沿用其後續 v3 架構，沒有冒稱全部獨立撰寫 |
| CodexGapSA | 寬 cell 優先、free gap、成對修整、SA restart | 保留作獨立基準；未加入額外初始解以免增加時間並混淆實驗 |
| ClaudeOpus55_v2 | spreading、精確 row 重排、懲罰函數匹配、near fallback、三顆重排 | v3 已包含；位移卡點是 row membership／順序受局部鄰域限制 |
| CodexParetoDP | 總位移預算下的瓶頸指派、independent matching、滑動／跨 row 交換／三顆排列 | 整合既有核心，保留數值檢查、輸入覆寫防護、邊界處理 |
| ClaudeOpus55_v3 | 仔細核對 v2→v3 diff，包含 row-opt/ISM、net-box cache、finish rounds、swap-with-push | 保留預設關閉的 swap-push；新 DP 直接處理多顆不同寬度 cell 聯合換 row |

本次也閱讀 `AI_Solutions/COMPARISON.md`、每版 README、測試工具、Bookshelf 結構，以及既有參考報告與先前實驗。前兩輪已完成的基礎閱讀沿用；本輪重點重讀新 v3、其差異、課程相關頁與新加入的 Grading Criteria。大型 `.pl/.nets` 等資料由 parser 與 checker 全量讀取驗證，沒有把每個重複的產出檔都當成新的演算法文件。

## 課程簡報對設計的影響

- `L06_1_sa`：以機率接受 uphill move、逐步降溫，避免只做貪婪下降；本版仍保留真正 SA。
- `L06_placement1` 25–32 頁：移動、交換、range limiter。課堂介紹的翻轉與暫時重疊不能直接套用，因 Project03 要求保持方向與合法性。
- `L07_placement2` 12–22 頁：固定 row 順序可以精確處理空白；這正是 v3 的強項，但無法改變 row 指派。
- 24–28 頁：window interleaving 擴大可考慮的排列，卻仍有兩組相對順序限制。本次改用小集合的 bitmask DP，容許所有排列，再枚舉兩個 row 的集合分割。
- 30、44–48 頁：row 子集合選擇、多 row 小視窗與分區最佳化，是本次聯合視窗的直接啟發。避免宣稱只靠 flow 就能精確處理不同寬度 cell 的連續占用。

## 圖片中的「最佳解／下界」需要補足什麼

圖片與既有 COMPARISON 的數值是待驗證的既有實驗結果，不是新版本必須照做的指令。

讀取 `Grader/tools/exact_bound.py` 發現：

1. `--max` 在二分 cap 後，若**受限候選列**的 LP 不可行，`column_generation` 直接回傳 `None`。沒有進行 Phase-I feasibility pricing，亦未完整產生該 cap 的所有位置。因此「當前欄位不可行」不等於「原問題不可行」，這種結果不能直接當全域最大位移下界。
2. `--ilp` 僅在 LP column generation 留下的位置上解整數規劃。LP 不需要的正 reduced-cost 欄位，整數解可能仍需要。只證明該受限 ILP optimal，不代表完整位置集合的 ILP optimal。
3. HiGHS 的預設相對 MIP gap 也不是 0；`status=0` 不必然表示 incumbent 與 dual bound 相同。
4. fixed 物件在晶片左邊時，原模型的右界 `b` 可以為負數，NumPy 的 `free[r,0:b]` 就會誤把整段 row 封住。h01 的左側 pad `x=-2,w=1` 實際在晶片外，卻會把某些 row 幾乎全封鎖。加入 incumbent 位置作候選只能讓受限模型勉強可行，不能修正全域下界的涵義。新工具把兩端都截在 `[0,ns]`，重新建立障礙物遮罩，並檢查所有 incumbent cell 仍是合法候選。

新工具 `tests/full_bound.py` 因此完整枚舉「不超過 incumbent 最大位移」的所有 row/site 位置，要求零相對 MIP gap，另存 LP bound、MIP incumbent、dual bound、求解狀態及範圍。時間截止就只報界與已知解。較小 cap 的 LP 測試也使用這個完整位置集合，才可將不可行當成有效下界。

該工具沿用既有 parser／site 模型，額外檢查同網格、整數 site 寬度、單高度等適用假設。它沒有證明大設計的全域最佳，也沒有建立 HPWL 下界。原工具及其他版本的文件均未改動；新比較會明列證據範圍。
