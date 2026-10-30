---
title: CAD Design Project 2 – Clock Buffer Insertion
subtitle: Computer Aided Design for VLSI System ｜ M11502102 薛宇宸
---

# I. 初始合法時脈樹建構

**資料結構：** 時脈樹存成 `std::vector<Node>`，索引就是節點編號（0 = SRC、1～N = sinks，其餘為 buffers）。每個節點記錄座標、型號與父子關係，已占用的座標存在雜湊表。輸出前用 BFS 重新編號。

**由下往上分群：** frontier 存放還沒有父節點的節點。若 SRC 的 fanout 與總線長限制允許，就直接連接全部 frontier。否則先依 Morton 順序排序，用貪心法把相鄰節點分成一群，在群組的 x、y 中位數（曼哈頓總線長最小的位置）放一個共同的 buffer，再往上一層合併。若某一輪無法合併，就在靠近 SRC 的位置插入 relay buffer。

**合法性：** 候選位置必須在晶片內且沒有被占用。型號選擇符合 fanout 與線長限制、成本最低的型號。只連接還沒有父節點的節點，因此不會形成環。每一輪若沒有減少 frontier 數量，就必須縮短 frontier 到 SRC 的距離總和，確保建構會結束。若找不到合法的 relay buffer 或超過 10⁵ 輪，就回報建構失敗。

# II. Score 最佳化（v2→v5）

目標：**Score = Σ buffer cost + w_skew × T_skew**。sink 的到達時間等於 SRC 到 sink 的曼哈頓路徑長，T_skew = T_max − T_min。所有修改都會先檢查合法性，每個版本的輸出也都通過課程 verifier 檢查（全部 VALID）。

| 版本 | 方法 | 動機 |
|---|---|---|
| v1 | 第 I 節的建構法 | 產生合法的初始解 |
| v2 | 爬山法：移動 buffer、更換父節點、插入延遲 buffer，只接受 Score 不增加的修改 | 插入 buffer 可以延後太早到達的 sink，更換父節點可以拿掉多餘的 buffer |
| v3 | 模擬退火，能量 = Score + 0.3·w_skew·σ，小測資多次重啟 | skew 只看最大值和最小值，大部分修改不會改變 skew，搜尋缺少方向；σ 可以引導搜尋 |
| v4 | 增量計算（只更新受影響的節點與其祖先）＋集中修改最早或最晚到達的路徑 | 在同樣時間內做更多次嘗試 |
| v5 | 建構與搜尋時都考慮子樹延遲來選合併點，加入兩個節點交換父節點的操作 | 讓初始解的 skew 更小，並增加可以探索的接法 |

# III. 實驗結果

**測資：** A 組為課程提供的 testcases（15 筆，001 是範例，最多 10,000 個 sinks）。B 組為另外產生、沒有用來調整參數的 holdout（12 筆，固定亂數種子）。以下使用相同輸入比較各版本：

| A-004（N=50, w=2） | Skew | Cost | Score | | B-004（N=50, w=1） | Skew | Cost | Score |
|---|---|---|---|---|---|---|---|---|
| v1 建構 | 631 | 494 | 1756 | | v1 建構 | 108 | 157 | 265 |
| v2 爬山法 | 100 | 394 | 594 | | v2 爬山法 | 42 | 134 | 176 |
| v3 模擬退火 | 42 | 416 | **500** | | v3 模擬退火 | 37 | 132 | 169 |
| v4 增量計算 | 24 | 456 | 504 | | v4 增量計算 | 18 | 132 | **150** |
| v5 平衡合併＋交換 | 13 | 450 | **476** | | v5 平衡合併＋交換 | 24 | 132 | 156 |
| **整體幾何平均（相對 v1）** | | | **0.469** | | | | | **0.301** |

![圖 1　A-004 各 sink 的到達時間：v1 為 91～722，v5 為 380～393，Skew 從 631 降到 13。](/tmp/claude-1000/-home-evan-Desktop-NTUST-105-1-Class-Computer-Aided-Design-for-VLSI-System/aced8320-58d1-4aa3-ac5b-ceb6dc88b371/scratchpad/doc/media/media/image1.png){width=5in}

![圖 2　A-004 在 v1／v2／v4／v5 的時脈樹（顏色代表 sink 的到達時間）。](/tmp/claude-1000/-home-evan-Desktop-NTUST-105-1-Class-Computer-Aided-Design-for-VLSI-System/aced8320-58d1-4aa3-ac5b-ceb6dc88b371/scratchpad/doc/media/media/image2.png){width=4.6in}

![](/tmp/claude-1000/-home-evan-Desktop-NTUST-105-1-Class-Computer-Aided-Design-for-VLSI-System/aced8320-58d1-4aa3-ac5b-ceb6dc88b371/scratchpad/doc/media/media/image4.png){width=3.2in} ![](/tmp/claude-1000/-home-evan-Desktop-NTUST-105-1-Class-Computer-Aided-Design-for-VLSI-System/aced8320-58d1-4aa3-ac5b-ceb6dc88b371/scratchpad/doc/media/media/image5.png){width=3.2in}

圖 3　各版本的 Score 除以 v1 的 Score（越小越好），左為 A 組，右為 B 組。

# IV. 分析與取捨

1. **用 Score 判斷是否插入 buffer：** A-004 從 v2 到 v3，cost 從 394 增加到 416，但 skew 從 100 降到 42，所以 Score 從 594 降到 500。只看 cost 或只看 skew 都會選錯。
2. **σ 引導搜尋：** 拿掉 σ 之後，A 組整體 Score 約差 3%。最後仍然用真正的 Score 選出最佳解。
3. **增量計算：** A-011 在 20 秒內的嘗試次數從約 171 萬次增加到 3,420 萬次（約 20 倍）。代價是要維護子樹的 lo/hi/σ 資訊，所以輸出前會重新完整計算一次，確認結果一致。
4. **新版本不一定在每筆測資都比較好：** v5 在 B-004（156 > 150）、A-005、A-012 都比 v4 差，A-004 的 v4 也比 v3 差。退火有隨機性，所以本報告用多筆測資的幾何平均判斷整體效果。
5. **時間公平性：** 搜尋時間由 min(20, 2+N/500) 秒改成 min(40, 2+N/250) 秒。N ≤ 500 時兩者差不到 2 秒，因此小測資的改善主要來自方法；大型測資的改善則有一部分來自增加的時間。最長執行時間為 40 秒，低於 60 秒的限制。
6. **下限：** 到達時間的奇偶性由 sink 與 SRC 的座標決定，任何樹都無法改變。因此 A-001 的 skew = 1 已經是最佳值。cost 下限是由 fanout 推得的估計，不一定能達到；大型測資的最終 Score 為下限的 1.1～1.7 倍，小型測資約為 3～4 倍，差距主要來自 cost。
