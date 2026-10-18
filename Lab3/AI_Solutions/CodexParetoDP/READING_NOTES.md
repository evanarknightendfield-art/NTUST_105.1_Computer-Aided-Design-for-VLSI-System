# 閱讀與設計紀錄

本次使用者已允許閱讀全部 `AI_Solutions`，包含先前排除的 Claude 解法。

## 題目與資料

- 重新核對 `Project03.pdf` 的兩階段目標、L1 位移、cell-center HPWL、固定物件與
  方向、site/subrow 合法性，以及輸出四行與 Bookshelf 檔案要求。
- 已閱讀兩份本地 Bookshelf 格式說明；原有 toy 的 NetDegree 與實際 pin 數不一致，
  因此不能把 PDF 範例數字當此 toy 的正確答案。
- 檢視三筆課程與八筆額外測資的結構；額外測資包含負原點、小數 site、split row、
  row 內 fixed macro，和只在 `.nodes` 標 terminal 的 pad。
- 原本 `M11502102/src` 與 `MyCode/my_dp_skeleton/src` 是相同的未完成骨架；本次
  沒有修改它們。教學、各 README、Report 的兩支 Python 與 shell 流程已閱讀。
- 讀取參考報告 Markdown 與 DOCX 文字、各表格、66 份 JSON 實驗紀錄、21 份 CSV
  trace、20 份 placement；它們包含成功與預期失敗（例如 Tetris 放不下）的紀錄。
  這些是理解先前實驗的資料，沒有冒充為新版本的測試結果。
- 檢視位移熱圖、退火比較、effort 圖，並閱讀 step1 範例及 step2 的故意錯誤配置。
  大量重複的生成資料採解析／統計檢視；沒有將二進位執行檔或建置暫存當成演算法來源。

## AI_Solutions 程式碼

逐一閱讀三份來源中的資料結構、讀寫／合法性判斷、合法化、退火與 main；對
`ClaudeOpus55` 與 `_v2` 另外比較差異，避免把已有改善誤稱成新功能。

| 檔案／模組 | 核對的行為 |
|---|---|
| CodexGapSA `db.cpp`、`placer.cpp` | 寬度排序、所有空隙搜尋、雙目標交換條件、SA 重啟、最佳快照與回滾 |
| Claude `placedb.*` | fixed 判斷、pin 去重、忽略 offsets／weights、macro 切 segment、輸出精度 |
| Claude `rowmap.hpp` | cellAt、freeAround、不同寬相鄰／非相鄰交換、局部 resort |
| v1 `legalizer.cpp` | Abacus trial/commit、insert/push、worst-cell refinement、eviction rollback |
| v2 `legalizer.cpp` | spreading BFS、凸 row 最佳化、Hungarian、phi 懲罰、matching work budget、plain/spread 選擇 |
| v1/v2 `annealer.cpp` | CSR adjacency、受影響 net 去重、optimal region、溫度採樣、時間截斷、undo journal |
| v2 新 SA 部分 | inactive cells、密度式 near fallback、window 上限、三顆隨機 reorder、minMoves |
| 三版本的 main/建置檔 | 預設參數、stdout 格式、例外情況與可重跑方式 |

## 新設計為何這樣做

1. 原本 v2 的 Stage 1 matching 使用懲罰成本，不能保證總位移不增加。
   新增模組將總位移當明確預算，再解最低瓶頸值，不改寫原本的 phi 來假裝雙目標保證。
2. SA 的局部終點仍可能存在立即可改善的合法空隙或交換。新增確定性下降掃描所有 cell，
   對候選算真實成本，而不是只抽到一個 proposal 就交給亂數接受。
3. HPWL matching 需要比位移 matching 多一個條件：同組 cell 不能共享 net。
   因此另建 independent groups；相鄰重排與雙 cell 交換則用 net 聯集重算。
4. 新增最佳化前後保存／重算指標，失敗直接報錯，避免悄悄用合法化結果掩蓋 bug。
5. SA trace 與後處理 trace 分開，報告中的 SA 嘗試次數不混入 Hungarian／排列搜尋。

## 文獻查核

實際查閱作者公開的兩篇原文，沒有使用第三方轉載作技術依據：

- [ABCDPlace, TCAD 2020](https://yibolin.com/publications/papers/ABCDPLACE_TCAD2020_Lin.pdf)，
  independent set matching 與 local reordering 的定義及成本可加性。
- [RippleLG, TCAD 2022](https://chengengjie.github.io/papers/J4-TCAD22-RippleLG.pdf)，
  相同 cell type 的位置指派與最大位移最佳化。

本版仍是單執行緒 C++17 實作；沒有引入上述專案的外部程式庫或 GPU 程式。
