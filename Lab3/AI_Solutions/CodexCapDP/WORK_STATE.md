# CodexCapDP 工作狀態

## 第二階段（2026-10-21 使用者已授權）
- 依診斷實作、調參、凍結、完整回歸與交付；以下第一階段內容保留為歷史。
- 第二階段實作已結束，51 檔已凍結；不可再改演算法，五個保護版本仍不得修改。
- 最新下一步（2026-10-22）：本輪交付與核對已完成，沒有待跑的實驗。README、COMPARISON 第 10 節、edge 表已更新；final/summary.md、details.md 列出所有未達標，final/delivery_audit.json 核對來源與紀錄。停在凍結交付點；後續若再改須另開一輪驗證，不得用本次 edge／seed4 調參或重跑未變來源的實驗。
- 最終驗收：11 筆位移、三種 HPWL 協定各 3 seeds、27+L1+L2、一般與 sanitizer 測試、凍結後 seed4 四版本；交付 README/比較/edge 表。
- 所有新實驗放 results/CodexCapDP/phase2/，逐步留下命令、hash 與完成標記；第一階段實驗不重跑。

## 第一階段範圍與規則
- 只診斷；完成 `Testcases/results/CodexCapDP/diagnosis.md` 後停止，不開始第二階段。
- 不 commit、不加署名；五個既有解法資料夾保持不變。
- `src/`、CMakeLists.txt、Makefile、tests/ 已由 v5 複製；src 演算法不修改。
- 所有執行輸出（包括 build、log、placement）寫到 `Testcases/results/CodexCapDP/`。
- 計時工作串行；其他分析工作用 `taskset -c 16-19`。超過 2 分鐘工作背景執行。
- 僅 testcases/holdout 分析與調參；L2 是指定的單 seed 排程診斷。edge 系列不參與調參。
- 不產生新測資；若需要產生，使用 `/home/evan/anaconda3/bin/python3`（numpy 2.4.6）。

## 已完成
1. 已讀 Lab3/README.md、v5 README、既有診斷及指定比較資料，建立獨立副本。
   - 現有工作樹有使用者未提交變更；保持原樣。
   - 已知數字只作歷史參考，新的時間預算實驗各自標明 run。

## 下一步（第一階段已完成，停止）
- `Testcases/results/CodexCapDP/diagnosis.md` 已完成，全部指定量測與數據核對已完成。
- **沒有待補跑的實驗；不要重跑。**
- 目前停在診斷交付點，尚未開始第二階段。只有使用者明確要求進入第二階段時，才依報告的建議繼續。
- 最終核對在 `validation.json`：src 與 v5 相同，五個保護資料夾已記錄的 133 檔未變，L2 三個輸出獨立 checker 全過，報告連結無遺漏。

## 重啟規則
先讀本檔與 results/CodexCapDP 下的進度紀錄；已完成且來源未變的實驗不重跑。

## 工具準備完成
- `tests/diagnostic*.cpp/.hpp` 是獨立量測版本；由 results/CodexCapDP/scripts/make_instrumented.py 產生的複本只加觀測，src 未改。
- cap 嘗試記錄 probe/full、seed、row/site 修復秒數、DP 秒數、raw/post 位移、接受/失敗理由；保存 full candidate 的共同起點與 memo。
- 局部 DP 僅在測試複本加 window mask（違規或實際移動 cell，及同 segment ±4 個鄰居）；全域 / local0 / local4 各從完全相同 raw 與 warm memo 開始。
- rowRepair 中間只有 membership，尚無實際座標；後處理用同一 cost/clump 函式重建各階段座標，報告將明確標成反事實分解。
- chain 配對使用相同 pre-chain placement/memo；holdout 小設計預設 chain 其實 3 秒，指定 1.5/4 秒屬控制實驗。
- run_ts.py 的 phases.py 未辨識 [cap]，其 t_chain 包含 cap；將在本次彙整腳本用完整時間戳重新分開，不改既有工具。
- 已確認指定 Python 的 numpy = 2.4.6；未生成測資。

- Run A 完成：`results/CodexCapDP/run_A_local_grade/record.csv`；下一步 run B。

- 儀器化補充：C 的可行 full candidate 同時保存實際 phase1/cleanup/phase2 membership（`.members`）；局部 DP 分析時才重建中間座標。這讓 raw 增幅可對應到實際被拒絕的候選，不只依賴額外 seed 7 實驗。
- L2 的原起點與 SHA256 已保存 `results/CodexCapDP/L2_source.json`，不重跑合法化。

- Run A 結果確認：{'seconds': 629.26, 'pass': 11, 'both': 6}。逐筆與 ChainDP 參考比較在 `results/CodexCapDP/baseline_comparison.csv`。下一步等待串行 run B，既有數據不重跑。

- 量測工具已凍結，hash：`results/CodexCapDP/diagnostic_tool_hashes.json`。另加 memo sync 的純觀測統計（實際標記 vs 原移動 cell 的直接鄰居），沒有修改 sync 演算法。接下來不再變更工具，等待序列量測結果。

- Run B 進行中；已觀察固定設定的 h01 A/B 最大位移 30.07/29.59、h02 2513.931/2496.281。屬不同執行的實測差異，不能視為程式變更效應。`run_variation.csv` 將彙整全部 11 筆。
- 背景工作：baseline exec session 29434；依賴事件等待與診斷佇列 session 25179。日誌 baseline.log / run_A.log / run_B.log / diagnostic_runs.log。

- Run B 完成：`results/CodexCapDP/run_B_ts/record.csv`；下一步 cap/chain/L2 診斷。

- run_C/h01 完成（13.59 秒），資料：`results/CodexCapDP/run_C/h01/`。

- run_C/h02 完成（24.26 秒），資料：`results/CodexCapDP/run_C/h02/`。

- run_C/h03 完成（17.12 秒），資料：`results/CodexCapDP/run_C/h03/`。

- run_C/h04 完成（16.76 秒），資料：`results/CodexCapDP/run_C/h04/`。

- run_C/h05 完成（6.26 秒），資料：`results/CodexCapDP/run_C/h05/`。

- run_C/h06 完成（32.39 秒），資料：`results/CodexCapDP/run_C/h06/`。

- Run B 已完成，11 筆合計 626.44 秒；phase 分解在 `phases_corrected.csv`，與 A 的差異在 `run_variation.csv`。
- C 初步數據：h01 57 次（25 full），row 修復 8.955 秒、DP 0.901 秒；h04 11 次（4 full），修復 0.538 秒、DP 4.009 秒。兩者瓶頸不同，不能把「DP 太慢」套用所有 holdout。待 C 其餘與局部實驗完成後排序。

- run_C/h07 完成（26.24 秒），資料：`results/CodexCapDP/run_C/h07/`。

- run_C/h08 完成（63.45 秒），資料：`results/CodexCapDP/run_C/h08/`。

- Run C：8 筆 holdout 的 pre-chain/pre-cap 與 cap 嘗試紀錄均已保存；接下來只重用快取起點。

- chain/h01 完成（0.12 秒），資料：`results/CodexCapDP/chain/h01/`。

- chain/h02 完成（3.28 秒），資料：`results/CodexCapDP/chain/h02/`。

- chain/h03 完成（4.55 秒），資料：`results/CodexCapDP/chain/h03/`。

- chain/h04 完成（5.11 秒），資料：`results/CodexCapDP/chain/h04/`。

- chain/h05 完成（5.22 秒），資料：`results/CodexCapDP/chain/h05/`。

- chain/h06 完成（5.40 秒），資料：`results/CodexCapDP/chain/h06/`。

- chain/h07 完成（5.35 秒），資料：`results/CodexCapDP/chain/h07/`。

- chain/h08 完成（5.01 秒），資料：`results/CodexCapDP/chain/h08/`。

- plateau/h01/precap 完成（0.00 秒），資料：`results/CodexCapDP/plateau/h01/precap/`。

- plateau/h01/final_A 完成（0.00 秒），資料：`results/CodexCapDP/plateau/h01/final_A/`。

- local_dp/h01 完成（0.17 秒），資料：`results/CodexCapDP/local_dp/h01/`。

- forensic/h01 完成（1.38 秒），資料：`results/CodexCapDP/forensic/h01/`。

- plateau/h04/precap 完成（0.02 秒），資料：`results/CodexCapDP/plateau/h04/precap/`。

- plateau/h04/final_A 完成（0.02 秒），資料：`results/CodexCapDP/plateau/h04/final_A/`。

- local_dp/h04 完成（2.74 秒），資料：`results/CodexCapDP/local_dp/h04/`。

- forensic/h04 完成（3.57 秒），資料：`results/CodexCapDP/forensic/h04/`。

- plateau/h06/precap 完成（0.06 秒），資料：`results/CodexCapDP/plateau/h06/precap/`。

- plateau/h06/final_A 完成（0.05 秒），資料：`results/CodexCapDP/plateau/h06/final_A/`。

- local_dp/h06 完成（13.50 秒），資料：`results/CodexCapDP/local_dp/h06/`。

- forensic/h06 完成（10.74 秒），資料：`results/CodexCapDP/forensic/h06/`。

- plateau/h07/precap 完成（0.09 秒），資料：`results/CodexCapDP/plateau/h07/precap/`。

- plateau/h07/final_A 完成（0.08 秒），資料：`results/CodexCapDP/plateau/h07/final_A/`。

- local_dp/h07 完成（7.93 秒），資料：`results/CodexCapDP/local_dp/h07/`。

- forensic/h07 完成（8.59 秒），資料：`results/CodexCapDP/forensic/h07/`。

- plateau/h08/precap 完成（0.16 秒），資料：`results/CodexCapDP/plateau/h08/precap/`。

- plateau/h08/final_A 完成（0.14 秒），資料：`results/CodexCapDP/plateau/h08/final_A/`。

- local_dp/h08 完成（18.80 秒），資料：`results/CodexCapDP/local_dp/h08/`。

- forensic/h08 完成（18.56 秒），資料：`results/CodexCapDP/forensic/h08/`。

- Chain、頂部密度、局部 DP、membership 分解量測完成；下一步 L2 單 seed 排程。

- L2 完成（285.71 秒），資料：`results/CodexCapDP/L2/`。

- L2 三種排程各一次、seed=1、同一既有合法起點完成；全部指定量測完成，開始彙整 diagnosis.md。

## 最終彙整進度
- 全部 37 個診斷步驟完成；run A 11/11 PASS、629.26 秒，run B 626.44 秒。
- `summary.json` / `tables.md` / `attempts.md` 已產生；來源與五個保護資料夾 133 檔 hash 核對無變更（validation.json）。
- h08 C attempt 9：max 28355.252、total 126528060.717，同時滿足 ChainDP 比較條件，但被內部 total_limit 125541000.867 拒絕。
- h06/h08 raw 增幅主要來自原未違規 cell 的同 segment 重排；實際換 segment 僅 11 / 3 顆，實際移動共 5029 / 11114 顆。
- 局部 DP 沒有普遍大幅加速：local0 h06 省 19.28% 但總位移 +0.246%；h08 反而慢 7.28%。
- chain 1.5 vs 4 秒：holdout 總位移幾何平均 +0.03664%，最大單筆 +0.09554%，最大位移無差；多用 13.47 秒。
- L2 seed 1：SA30→P60 vs SA60→P30，HPWL −3.3037%、實際 Stage2 秒數 −3.7059%；P30→SA60 反而 +6.6734%。

## 完成
- 已完成 diagnosis.md（10 節）、attempts.md（五筆共 97 次 cap 嘗試）、tables.md、summary.json 與所有彙整 CSV。
- L2 三種排程由獨立 check_pl.py 再核對，全部 LEGAL；沒有重跑演算法。
- 第二階段建議已按預期幅度與可行性排序；保留 h01/h04 等仍未達標事實與單 seed 限制。
- 不 commit、不加署名、src 未改。第一階段結束並停止。

## 第二階段步驟 1：直接受限視窗原型
- 已加入 repairCapWindows、修正 WindowMemo::sync 的傳播式標記，尚未接入主搜尋。
- 共同起點測試：phase2/direct_v1/*.csv；每筆 0.5/1/2/4% cap，各從 run_C pre-cap 重開。
- h01 0.5/1% 成功（0.012/0.013 秒），其餘四筆全部未能修完；0.5% 時 h04/h06/h07/h08 剩 4/17/31/49 顆。失敗不改輸入。
- 下一步：改善狹窄局部視窗的可達性，並測保留位置的修復，然後才接入 cap 搜尋。

## 第二階段步驟 2：修復候選比較、接入搜尋
- 新資料：phase2/direct_v2、hybrid_v1、chain_cap_v1/v2、site_cap_v1。均是原 run_C 的快取起點；四個 cap 降幅各自重開，沒有重跑合法化。
- 直接視窗加保留 row/order 後：0.5% cap 時 h04/h06/h07/h08 剩 1/6/4/13 顆。單獨仍不能完成，故嘗試補充修復。
- hybrid（局部視窗→row 修復→1 秒 DP）全部得到合法 cap；但較強 cap 的總位移仍高，例如 h08 降 1%：127.619M、2.749 秒。
- chain cap 原型與偏好保留座標的 site 原型沒有較好的中大設計總位移；尚未納入主流程，後續會移除未採用的 src 分支並保留量測工具。
- search_v1：取消只有可行性的 probes，直接評估候選，h04 max 238.0；h01/h06 仍沒有候選通過內部總位移門檻。其餘逐筆 CSV/log 已保存。
- 新發現：memo 除移動鄰居外，還需記錄視窗尺寸與 cap，否則不同尺寸／放寬 cap 的候選可能錯用先前的無改善記錄。已加入條件失效。
- 下一步：search_v2 正在串行跑 8 筆共同起點；保留有潛力候選繼續 DP、小設計使用完整 5/4 視窗，避免重開後來不及回收總位移。結果出來後再做完整 11 筆調參。

## 第二階段步驟 3：search_v2 與總位移門檻
- search_v2 完成 8 筆，時間預算小型 10 秒／其他 4.5 秒：h08 max=28313.593,total=125391519.891，兩條驗收通過；h06 候選 max=423.92,total=892515.50 符合外部驗收，但被內部 ratio=1.004 拒絕。這是單次共同起點實驗。
- search_v2_r1007 已完成，不要重跑：h01 max=29.28,total=2360.27 達標；h03 total=7086.41953 > ChainDP×1.005=7075.39849，所以不能全面採用 1.007。其餘數據見各 CSV/log，h06 仍受牆鐘切點影響未收回足夠總位移。
- search_v2 是第一個加入 memo 尺寸／cap 條件失效的版本。先前原型 memo 缺少這項失效，數據只作探索參考。
- 未採用的 cap-chain 與 preserve-site 原型已移到 tests/cap_chain_prototype.*、global_preserve_prototype.*；src/chain.* 恢復 v5，沒有改平行 SA。
- site_original 正在串行量測：原 site 修復單獨 1 秒 + DP 2 秒，cap 降 1/2/4%，五筆同一 run_C 起點。用來區分方法本身效果與先前原型自動退回 row 修復造成的混淆。

## 第二階段步驟 4：局部 DP、完整調參驗證
- site_original 已完成：修復降低 1% cap 的 h04/h06/h07/h08 只花 0.008/0.082/0.069/0.344 秒，raw 較 row 方法低，但 2%/4% 常未找到解；不能當成通用替代。
- src 現在保留：直接受限局部視窗 + 剩餘 row/site 修復；DP footprint 隨移動擴大；memo 修正；逐次保留候選回收；剩餘額外預算給 chain+DP，總額仍 12/6 秒。
- L2 預設排程先按診斷實作：≥500000 movable 時 SA30/polish60，任何明確 --time 或 --polish-time 都保留使用者設定。尚待最終驗證，不能宣稱多 seed 改善。
- 1.007 全面放寬已否決（h03 超標）；目前完整調參仍用 1.004。
- phase2/full_v3 使用新腳本 scripts/tune_full.py：每筆 done.json、binary hash、參數紀錄，完成才追加 WORK_STATE；可安全續跑未完成筆，不重跑已完成筆。先跑 9 個 C++ 測試，再 11 筆 Stage 1。

- 完成 full_v3：{'passed': 11, 'both': 6, 'cases': 11, 'seconds': 263.9085181740229}；資料 phase2/full_v3/record.csv。相同 binary/protocol 不重跑。

## 第二階段步驟 5：完整 v3、v4 與未採用的更強位置保留
- full_v3 完成：11/11 合法，263.91 秒，兩條位移條件 6/11。h01/h04/h06/h07 未達最大位移，h08 的結果以 record.csv 為準；不要用快取小實驗取代完整執行。
- C++ normal v3 9/9、v4 10/10 PASS。新增 guard 測試驗證 invariant failure、標準／非標準例外與 worker 例外後恢復前一步，再可繼續執行；平行 SA 檔案未改。
- full_v4 在背景串行執行（exec session 50433）：binary phase2/build_v4/dp，加入 0.1+n×1e-6 秒 deadline 保留量、明確 [extra] 計時、1.005 ratio、只重排違規 row。
- 已觀察只重排違規 row 會失去中性的座標調整與後續 DP 收益：h01 v3 29.67/2357.03 → v4 31.07/2352.35，故不能當作必然較好的保留位置策略。ibm01 v4 total 23146452.2603 超過 ChainDP×1.005 約 3558.6867。
- 下一步先等 full_v4 完成；準備回復 v3 的保留 row/order 行為、以更保守的 1.0048 上限做最後完整調參（新 build 目錄，不能覆寫正在跑的 binary）。尚未凍結，final、新 seed4、HPWL 三協定、ASan 均未開始。

- 完成 full_v4：{'passed': 11, 'both': 6, 'cases': 11, 'seconds': 261.1990397509653}；資料 phase2/full_v4/record.csv。相同 binary/protocol 不重跑。

## 第二階段步驟 6：最後調參候選 v5 與最終驗證腳本
- full_v4 完成 11/11 合法、261.20 秒、兩條件 6/11；ibm01 總位移超標，h01/h04/h06/h07 最大位移落後。
- 候選 v5 回復「保留 row/order、對全 row 做受限 L1」；放棄只重排違規 row。總位移 ratio=1.0048；初始 cap 降幅 min(2.5%,1.75/sqrt(movable))，避免大量違規的大設計第一步太大。所有參數僅看 testcases/holdout。
- 新 binary=phase2/build_v5/dp；此輪不改演算法直到讀完 full_v5 與 sanitizer 結果。背景命令 scripts/train_v5.sh，外層 log=phase2/train_v5.log。
- scripts/phase2_final.py 已備妥但**尚未執行**，要求 frozen_sources.sha256 存在且逐檔相符才會跑：local_grade 11、run_edge.sh、L1/L2、edge 的 timestamp Stage1、三種 HPWL、最後生成 seed4 並跑四版本。每步有 done.json，重啟跳過完成步驟。
- final HPWL fixed/default 重用已完成且來源／協定未變的 v4、v5 參考（有 reuse.json），新版本全部 3 seeds；equal 重新跑 v4/新版本，對照 v5 的歷史同協定門檻。holdout Stage1 重用凍結 binary 的 full_v5，edge 只做回歸，不調參。
- 最終腳本會用 toolbin/cmake 把編譯釘到 16–19，dp 計時不釘 E-core；各計時程式串行。

- 完成 full_v5：{'passed': 11, 'both': 9, 'cases': 11, 'seconds': 261.74481895397184}；資料 phase2/full_v5/record.csv。相同 binary/protocol 不重跑。

- 最後調參 full_v5 完成：11/11 合法、9/11 兩條件，261.7448 秒；h04 max237.85 vs231.76、h07 max47.6762 vs47.4162 尚未達標。全部 11 筆總位移通過。ASan/UBSan 正在編譯／執行，完成後凍結；不再用 edge 調參。

## 第二階段凍結
- ASan/UBSan 原沙箱內的 10 個測試均因 LeakSanitizer/ptrace 限制在退出時失敗，原 log 保留；經核准於沙箱外重跑相同 10 個測試，全過（37.67 秒），沒有關閉 sanitizer。
- Normal 10/10、ASan/UBSan 10/10 PASS；測試包含新 cap-window／memo／rollback，以及 inherited 固定 seed/執行緒/次數的平行 SA determinism。未改 annealer、pool、rowmap 的平行 SA 實作，因此沒有新 TSan 測試需求。
- 最後只修正 main.cpp usage 字串 1.004→1.0048 並重編；演算法未變，不重跑 full_v5。原／新 binary 與 source hash、唯一 diff 在 phase2/help_update.json，final 腳本明確驗證相容性。
- 原始碼 sha256 已寫 frozen_sources.sha256，時間與清單 hash 見 freeze.json；五個保護資料夾 133 檔核對不變。之後不改演算法。
- 新 seed4 尚未產生，將由 final 腳本在凍結驗證後產生。README/COMPARISON/edge 表與最終數據彙整仍待完成。

- 最終驗證：開始 lg_new；log = final/lg_new.log

- final 背景串行套件已啟動；主 log=Testcases/results/CodexCapDP/final/pipeline.log。後續每步會自動追加本檔與 final/*.done.json。若中斷，使用同一 scripts/phase2_final.py 續跑，不能重新啟動已在跑的套件。

- 最終驗證：lg_new 完成；不要重跑。

- 最終驗證：開始 edge_new；log = final/edge_new.log

- 最終驗證：edge_new 完成；不要重跑。

- 最終驗證：開始 lg_new_L1；log = final/lg_new_L1.log

- 最終驗證：lg_new_L1 完成；不要重跑。

- 最終驗證：開始 lg_new_L2；log = final/lg_new_L2.log

- 最終驗證：lg_new_L2 完成；不要重跑。

- 最終驗證：開始 ts_e01_nearlegal；log = final/ts_e01_nearlegal.log

- 最終驗證：ts_e01_nearlegal 完成；不要重跑。

- 最終驗證：開始 check_ts_e01_nearlegal；log = final/check_ts_e01_nearlegal.log

- 最終驗證：check_ts_e01_nearlegal 完成；不要重跑。

- 最終驗證：開始 ts_e02_legalgp；log = final/ts_e02_legalgp.log

- 最終驗證：ts_e02_legalgp 完成；不要重跑。

- 最終驗證：開始 check_ts_e02_legalgp；log = final/check_ts_e02_legalgp.log

- 最終驗證：check_ts_e02_legalgp 完成；不要重跑。

- 最終驗證：開始 ts_e03_dense97；log = final/ts_e03_dense97.log

- 最終驗證：ts_e03_dense97 完成；不要重跑。

- 最終驗證：開始 check_ts_e03_dense97；log = final/check_ts_e03_dense97.log

- 最終驗證：check_ts_e03_dense97 完成；不要重跑。

- 最終驗證：開始 ts_e04_sparse10；log = final/ts_e04_sparse10.log

- 最終驗證：ts_e04_sparse10 完成；不要重跑。

- 最終驗證：開始 check_ts_e04_sparse10；log = final/check_ts_e04_sparse10.log

- 最終驗證：check_ts_e04_sparse10 完成；不要重跑。

- 最終驗證：開始 ts_e05_bignets；log = final/ts_e05_bignets.log

- 最終驗證：ts_e05_bignets 完成；不要重跑。

- 最終驗證：開始 check_ts_e05_bignets；log = final/check_ts_e05_bignets.log

- 最終驗證：check_ts_e05_bignets 完成；不要重跑。

- 最終驗證：開始 ts_e06_tallnarrow；log = final/ts_e06_tallnarrow.log

- 最終驗證：ts_e06_tallnarrow 完成；不要重跑。

- 最終驗證：開始 check_ts_e06_tallnarrow；log = final/check_ts_e06_tallnarrow.log

- 最終驗證：check_ts_e06_tallnarrow 完成；不要重跑。

- 最終驗證：開始 ts_e07_tworows；log = final/ts_e07_tworows.log

- 最終驗證：ts_e07_tworows 完成；不要重跑。

- 最終驗證：開始 check_ts_e07_tworows；log = final/check_ts_e07_tworows.log

- 最終驗證：check_ts_e07_tworows 完成；不要重跑。

- 最終驗證：開始 ts_e08_fragmented；log = final/ts_e08_fragmented.log

- 最終驗證：ts_e08_fragmented 完成；不要重跑。

- 最終驗證：開始 check_ts_e08_fragmented；log = final/check_ts_e08_fragmented.log

- 最終驗證：check_ts_e08_fragmented 完成；不要重跑。

- 最終驗證：開始 ts_e09_offgrid；log = final/ts_e09_offgrid.log

- 最終驗證：ts_e09_offgrid 完成；不要重跑。

- 最終驗證：開始 check_ts_e09_offgrid；log = final/check_ts_e09_offgrid.log

- 最終驗證：check_ts_e09_offgrid 完成；不要重跑。

- 最終驗證：開始 ts_e10_mixedsites；log = final/ts_e10_mixedsites.log

- 最終驗證：ts_e10_mixedsites 完成；不要重跑。

- 最終驗證：開始 check_ts_e10_mixedsites；log = final/check_ts_e10_mixedsites.log

- 最終驗證：check_ts_e10_mixedsites 完成；不要重跑。

- 最終驗證：開始 ts_e11_format；log = final/ts_e11_format.log

- 最終驗證：ts_e11_format 完成；不要重跑。

- 最終驗證：開始 check_ts_e11_format；log = final/check_ts_e11_format.log

- 最終驗證：check_ts_e11_format 完成；不要重跑。

- 最終驗證：開始 ts_e12_collapse；log = final/ts_e12_collapse.log

- 最終驗證：ts_e12_collapse 完成；不要重跑。

- 最終驗證：開始 check_ts_e12_collapse；log = final/check_ts_e12_collapse.log

- 最終驗證：check_ts_e12_collapse 完成；不要重跑。

- 最終驗證：開始 ts_e13_onecell；log = final/ts_e13_onecell.log

- 最終驗證：ts_e13_onecell 完成；不要重跑。

- 最終驗證：開始 check_ts_e13_onecell；log = final/check_ts_e13_onecell.log

- 最終驗證：check_ts_e13_onecell 完成；不要重跑。

- 最終驗證：開始 ts_e14_fixedonly；log = final/ts_e14_fixedonly.log

- 最終驗證：ts_e14_fixedonly 完成；不要重跑。

- 最終驗證：開始 check_ts_e14_fixedonly；log = final/check_ts_e14_fixedonly.log

- 最終驗證：check_ts_e14_fixedonly 完成；不要重跑。

- 最終驗證：開始 ts_e15_realistic；log = final/ts_e15_realistic.log

- 最終驗證：ts_e15_realistic 完成；不要重跑。

- 最終驗證：開始 check_ts_e15_realistic；log = final/check_ts_e15_realistic.log

- 最終驗證：check_ts_e15_realistic 完成；不要重跑。

- 最終驗證：開始 ts_e16_rowgaps；log = final/ts_e16_rowgaps.log

- 最終驗證：ts_e16_rowgaps 完成；不要重跑。

- 最終驗證：開始 check_ts_e16_rowgaps；log = final/check_ts_e16_rowgaps.log

- 最終驗證：check_ts_e16_rowgaps 完成；不要重跑。

- 最終驗證：開始 hpwl_fixed；log = final/hpwl_fixed.log

- 最終驗證：hpwl_fixed 完成；不要重跑。

- 最終驗證：開始 hpwl_equal；log = final/hpwl_equal.log

- 最終驗證：hpwl_equal 完成；不要重跑。

- 最終驗證：開始 hpwl_default；log = final/hpwl_default.log

- 最終驗證：hpwl_default 完成；不要重跑。

- 最終驗證：開始 generate_seed4；log = final/generate_seed4.log

- 最終驗證：generate_seed4 完成；不要重跑。

- 最終驗證：開始 seed4_ClaudeOpus55_v4；log = final/seed4_ClaudeOpus55_v4.log

- 最終驗證：seed4_ClaudeOpus55_v4 完成；不要重跑。

- 最終驗證：開始 seed4_CodexChainDP；log = final/seed4_CodexChainDP.log

- 最終驗證：seed4_CodexChainDP 完成；不要重跑。

- 最終驗證：開始 seed4_ClaudeOpus55_v5；log = final/seed4_ClaudeOpus55_v5.log

- 最終驗證：seed4_ClaudeOpus55_v5 完成；不要重跑。

- 最終驗證：開始 seed4_CodexCapDP；log = final/seed4_CodexCapDP.log

- 最終驗證：seed4_CodexCapDP 完成；不要重跑。

- 最終驗證：所有計算步驟完成；下一步彙整數據、README、COMPARISON 與 edge 表，不改演算法。

## 最終彙整（2026-10-22）
- 背景套件已全部完成（final/ALL_DONE.json）；本次沒有重跑任何實驗，也沒有修改凍結演算法。
- 已執行 scripts/final_summary.py，產出 final/summary.md、summary.json、extra_times.csv。27+L1+L2 全 PASS；L1 187.88 秒、L2 199.42 秒。11 筆調參集合位移雙條件 9/11，未達 h04 最大 +2.6277%、h07 +0.5483%；包含 edge 共 21/27 雙條件。
- HPWL 同總時間 holdout/edge = 0.985721/0.986691，皆通過；預設 = 0.992399/0.993073，holdout 略差於 0.9919 目標。e08 總位移仍比 ChainDP 多 1.0537%，未拿回原目標。新 seed4 四版本皆 16/16 PASS，新版位移雙條件 11/16。
- 下一步：核對 cap 計時與來源 hash、補四版本 seed4 比較，再完成 README、COMPARISON 第 10 節和 edge 表；保持凍結，不以回歸資料調參。

- 補充彙整完成：scripts/final_details.py → final/details.md、details.json、cap_attempts.csv、cap_timing.csv、hpwl_per_bench.csv。修正的是彙整腳本的歷史 log 路徑／cap summary 解析，沒有重跑計算。
- 計時結論：h04 可行完整候選平均 1.0065→0.7085 秒；h01/h06/h07/h08 單次均未普遍變便宜（不同 D／候選不能當配對加速比）。h01 cap 總時間 10.49→6.40 秒，其餘約 4.4–4.5 秒。
- 額外時間 26/27 在預算內；e07_tworows 6.245429 秒，超 0.245429 秒。最後 unresolved repair 單次 1.822982 秒；未有子步驟計時，不臆測是哪個修復子步驟。
- fixed 三 seed：新版/v5 共 72 筆 HPWL 與 attempts 完全相同。equal 為相同總上限，非實際秒數相等；新版最大計費超時 holdout 0.019 秒／edge 0.029 秒，保留在 details.md。
- L2 最終 HPWL 1,724,913,056，比歷史 v5 單次 +3.3157%；起點不同，不能把第一階段共同起點排程 −3.30% 當成全流程改善。
- 來源核對：凍結 51 檔及保護 133 檔未變；平行 SA 相關檔案與 v5 相同；三種 HPWL 新版皆 72/72 PASS。下一步完成文件與 edge 表，最後再核對交付。

- 交付文件完成：README 已改為第二階段完整說明，含所有未達標筆與差距、三協定平均／範圍、失敗方向、L2 與 deadline 限制；COMPARISON 只追加第 10 節，既有內容保留。
- 指定 run_edge.sh CodexCapDP 已於背景套件完成（final/edge_new.done.json）；本次只把 edge/make_table.py 加入 CapDP 並重建 tables.md，未重跑 edge。
- 來源數據入口：final/summary.md、details.md；編輯前的共享文件 hash/長度在 final/delivery_baseline.json。下一步做文件連結、來源凍結、數據完整性與既有比較內容保留的最後核對。

## 第二階段交付完成（2026-10-22）
- 最後核對：final/delivery_audit.json。凍結 manifest 51 檔無變更、五個保護版本 133 檔無變更；比較文件原有內容逐 byte 保留，其他既有 README 未變，新增文件連結無缺漏。
- 已完成 local_grade 27+L1+L2、新 seed4 四版本 64 筆、三協定合計新版 216 筆 HPWL；全部合法 PASS。一般／ASan+UBSan 10/10 證據保留，未重跑。seed4 確認在凍結後才生成。
- 品質未達仍保留：主要 h04/h07 最大；edge e03/e12 最大、e08/e16 總；seed4 e12/e16 最大、e05/e06/e08 總；預設 holdout HPWL 0.992399 > 0.9919；e07 extra 6.245429 > 6 秒。每次 cap 成本未普遍大降，e08 chain 代價未回收，L2 全流程 HPWL 比歷史 v5 +3.3157%。
- 本次接續只讀既有結果、彙整與寫文件／edge 表，沒有重跑完成實驗、沒有改凍結演算法、沒有 commit 或署名。沒有背景計算待收尾。
- 下一步：向使用者交付摘要；本輪工作完成，維持凍結。
