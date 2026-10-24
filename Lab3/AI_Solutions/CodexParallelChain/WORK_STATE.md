# CodexParallelChain — WORK_STATE

- 本 session：只建立版本並啟動第一階段診斷；不等待、不查進度、不調參、不 commit。
- 專案根目錄：Lab3。底版：ClaudeOpus55_v6；src、tests、CMakeLists.txt、Makefile 原樣複製。
- 只閱讀 HANDOFF.md 與必要原始碼／工具片段，未閱讀其他報告。
- 執行輸出：Testcases/results/CodexParallelChain/；scripts/ 已完整複製 v6 scripts/。
- 保護：ClaudeOpus55、CodexWindowDP、ClaudeOpus55_v4、CodexChainDP、ClaudeOpus55_v5、CodexCapDP、ClaudeOpus55_v6，均不得修改。
- SHA256：provenance/protected_before.json（逐檔）、protected_roots.sha256（各目錄 aggregate；按相對路徑排序的 sha256  雙空白  路徑 + 換行）、base.sha256（底版複製項目）、copied_scripts.json。
- v6 整個保護目錄 aggregate SHA256：2702e487b8f7be35de32daab56d71d9ac5fc74444c1b19fe20b1e24f8f15b2e3。

## 第一階段範圍

1. chain：testcases 3 + holdout 8 = 11 筆，另 e08_fragmented、e16_rowgaps 僅診斷；default 與額外指定 --chain-time 6，共 26 次。
   使用 -v --no-sa --cap-time 0；default 保留原本 1.5/3 秒 chain 預算。cap 關閉以隔離 chain 收穫；不改接受規則或額外預算。
   phase1/instrumented/ 是獨立副本，只有 main.cpp 增加 8 個 fprintf；記錄每輪 target windows、ejection chains、post-chain DP 的實際耗時與前後總位移，以及整段 chain。
2. SA：ibm01、ibm05、h06、h07、h08，seed 1、2；自動執行緒 moves=3,000,000（8 bands 約 2,400 萬實際次數），再把其實際 attempts 原值傳給單執行緒。
   sa_bench：seconds=100000、polish_seconds=0，避免時間截止；保留 100 溫度步驟，明列單執行緒整除步驟可能少做的 0–99 次，不冒稱完全相等。
   原始演算法不改；診斷 driver 只加 fprintf 印實際 threads。逐筆、平均與範圍均由完成後腳本生成，僅診斷不宣稱進步。
   指定起點：Testcases/results/ClaudeOpus55_v6/final/hpwl_fixed/legal_starts/ClaudeOpus55_v4/<bench>/output。
   例外：該目錄沒有 ibm01、ibm05；在新目錄 phase1/sa/legal_starts/ClaudeOpus55_v4/ 以未修改 v4 -v --no-sa 補建；每對 SA 共用同一份起點並保存 SHA256。
3. toy：未插樁 v5 與 v6 各一次 -v --no-sa，保留完整 cap stderr；HANDOFF 參考最大位移分別 1972.0、2004.4，實測另列。

## 執行約定

- 使用複製的 v6q.Queue，每步 .done.json；拿新版本 timing.lock，同時拿既有 v6 timing.lock（唯讀開啟、不更動檔案），計時程式一次一個。
- 編譯、分析固定 CPU 16–19；計時程式可用 CPU 0–19，無其他診斷同時執行。
- 完成輸出 phase1/chain_summary.csv、chain_rounds.csv、sa_pairs.csv、toy_summary.csv、summary.json；只有全部成功且保護資料夾 SHA256 未變才寫 phase1/ALL_DONE。
- 失敗寫 phase1/FAILED.json，保留每步 log/.failed.json；修正後可重跑佇列，已成功步驟跳過。
- 尚未啟動；先做編譯與輸入檢查，不執行計時診斷。
- 10-22 23:55 phase1/build_jobs/build_v6 完成（12 秒）
- 10-22 23:55 phase1/build_jobs/build_chain 完成（2 秒）
- 10-22 23:55 phase1/build_jobs/build_sa 完成（1 秒）
- 10-22 23:55 phase1/build_jobs/build_v5 完成（12 秒）
- 10-22 23:55 phase1/build_jobs/build_v4 完成（8 秒）
- 10-22 23:55 phase1 啟動前檢查完成：編譯成功、輸入有效、插樁僅 fprintf；尚未執行診斷。

## 第一階段執行歷史（已於 10-23 05:01 完成）

歷史等待項目：scripts/diag.py，完成標記 phase1/ALL_DONE
- 已 nohup 啟動，PID 4；日誌 phase1/diag.nohup.log。
- 啟動前編譯與輸入檢查成功；保護資料夾 SHA256 未變（provenance/protected_after_setup.json）。
- 本 session 到此結束，未等待完成、未查詢診斷進度。
- 10-23 00:09 phase1 診斷失敗：Protected directories changed; do not write ALL_DONE；未寫 ALL_DONE，詳見 phase1/FAILED.json。
- 10-23 00:09 phase1 診斷失敗：Prepared script/binaries changed; rerun --prepare；未寫 ALL_DONE，詳見 phase1/FAILED.json。
- 10-23 00:10 phase1 啟動前檢查完成：編譯成功、輸入有效、插樁僅 fprintf；尚未執行診斷。
- 10-23 00:10 phase1/jobs/chain_ibm01_default 完成（22 秒）
- 10-23 00:10 phase1/jobs/chain_ibm01_chain6 完成（26 秒）
- 10-23 00:11 phase1/jobs/chain_ibm05_default 完成（37 秒）
- 10-23 00:12 phase1/jobs/chain_ibm05_chain6 完成（41 秒）
- 10-23 00:12 phase1/jobs/chain_toy_default 完成（1 秒）
- 10-23 00:12 phase1/jobs/chain_toy_chain6 完成（1 秒）
- 10-23 00:12 phase1/jobs/chain_h01_default 完成（3 秒）
- 10-23 00:12 phase1/jobs/chain_h01_chain6 完成（3 秒）
- 10-23 00:12 phase1/jobs/chain_h02_default 完成（14 秒）
- 10-23 00:12 phase1/jobs/chain_h02_chain6 完成（14 秒）
- 10-23 00:12 phase1/jobs/chain_h03_default 完成（8 秒）
- 10-23 00:13 phase1/jobs/chain_h03_chain6 完成（9 秒）
- 10-23 00:13 phase1/jobs/chain_h04_default 完成（13 秒）
- 10-23 00:13 phase1/jobs/chain_h04_chain6 完成（16 秒）
- 10-23 00:13 phase1/jobs/chain_h05_default 完成（5 秒）
- 10-23 00:13 phase1/jobs/chain_h05_chain6 完成（9 秒）
- 10-23 00:14 phase1/jobs/chain_h06_default 完成（30 秒）
- 10-23 00:14 phase1/jobs/chain_h06_chain6 完成（34 秒）
- 10-23 00:15 phase1/jobs/chain_h07_default 完成（22 秒）
- 10-23 00:15 phase1/jobs/chain_h07_chain6 完成（26 秒）
- 10-23 00:16 phase1/jobs/chain_h08_default 完成（58 秒）
- 10-23 00:17 phase1/jobs/chain_h08_chain6 完成（63 秒）
- 10-23 00:17 phase1/jobs/chain_e08_fragmented_default 完成（6 秒）
- 10-23 00:17 phase1/jobs/chain_e08_fragmented_chain6 完成（10 秒）
- 10-23 00:18 phase1/jobs/chain_e16_rowgaps_default 完成（6 秒）
- 10-23 00:18 phase1/jobs/chain_e16_rowgaps_chain6 完成（10 秒）
- 10-23 00:18 phase1/jobs/legal_start_ibm01 完成（21 秒）
- 10-23 00:19 phase1/jobs/legal_start_ibm05 完成（35 秒）
- 10-23 00:19 phase1/jobs/sa_ibm01_s1_auto 完成（5 秒）
- 10-23 00:19 phase1/jobs/sa_ibm01_s1_single 完成（21 秒）
- 10-23 00:19 phase1/jobs/sa_ibm01_s2_auto 完成（6 秒）
- 10-23 00:20 phase1/jobs/sa_ibm01_s2_single 完成（21 秒）
- 10-23 00:20 phase1/jobs/sa_ibm05_s1_auto 完成（6 秒）
- 10-23 00:20 phase1/jobs/sa_ibm05_s1_single 完成（26 秒）
- 10-23 00:20 phase1/jobs/sa_ibm05_s2_auto 完成（6 秒）
- 10-23 00:21 phase1/jobs/sa_ibm05_s2_single 完成（26 秒）
- 10-23 00:21 phase1/jobs/sa_h06_s1_auto 完成（4 秒）
- 10-23 00:21 phase1/jobs/sa_h06_s1_single 完成（14 秒）
- 10-23 00:21 phase1/jobs/sa_h06_s2_auto 完成（4 秒）
- 10-23 00:21 phase1/jobs/sa_h06_s2_single 完成（14 秒）
- 10-23 00:21 phase1/jobs/sa_h07_s1_auto 完成（6 秒）
- 10-23 00:22 phase1/jobs/sa_h07_s1_single 完成（22 秒）
- 10-23 00:22 phase1/jobs/sa_h07_s2_auto 完成（6 秒）
- 10-23 00:22 phase1/jobs/sa_h07_s2_single 完成（22 秒）
- 10-23 00:22 phase1/jobs/sa_h08_s1_auto 完成（7 秒）
- 10-23 00:23 phase1/jobs/sa_h08_s1_single 完成（25 秒）
- 10-23 00:23 phase1/jobs/sa_h08_s2_auto 完成（6 秒）
- 10-23 00:23 phase1/jobs/sa_h08_s2_single 完成（24 秒）
- 10-23 00:23 phase1/jobs/toy_v5 完成（5 秒）
- 10-23 00:23 phase1/jobs/toy_v6 完成（1 秒）
- 10-23 00:23 phase1/jobs/summarize 完成（0 秒）
- 10-23 00:23 phase1 診斷失敗：Protected directories changed; do not write ALL_DONE；未寫 ALL_DONE，詳見 phase1/FAILED.json。
- 10-23 05:01 phase1/ALL_DONE：全部診斷完成，保護資料夾 SHA256 未變；等待下一個 session 分析。

## 目前狀態：2026-10-23 phase1 診斷完成

- 已確認 phase1/ALL_DONE：26 chain、20 SA、2 toy；不重跑既有完成工作。
- 已寫 Testcases/results/CodexParallelChain/diagnosis.md：三部分逐筆耗時／位移收益、band 平行資料結構與約 2–4 倍工作量假說、10 對 SA 比值／平均、toy 原因、第二階段排序與驗收。
- 主樣本 default：target 3.312757 s / 9258.268240；ejection 9.545742 s / 1446.724840；post-DP 1.802126 s / 12303.715360。ejection 占時間 65.11%，收益 6.29%；不可直接以時間加速倍率推論品質收益。
- SA 等權平均損失 1.339185% ≥ 0.5%；4/5 測資均值過門檻。h06 實際 5 threads，其餘 8；只有 2 seeds，僅診斷。
- toy 兩版 cap 起點相同；v6 局部修補候選多因 total_budget 被拒，7 attempts／1 accepted，v5 51／3；最大位移 2004.4 對 1972.0。只描述，未修正。
- 協調者已在沙箱外啟動 diag.py 並移除計時 worker 的 taskset；provenance/orchestrator_note.json 亦記載保護文件變動／基準更新，最終 unchanged 是相對更新後基準。
- 第二階段優先序：SA 邊界改善 → target/DP 分 band → ejection 分 band與無效搜尋診斷 → toy 後續原因追蹤（需後續任務授權）。各項尚未實作。
- 閱讀限制判斷：HANDOFF 只讀第 0 節；不擅自讀第 5／7 節。採使用者提供的 0.5% 門檻；驗收依第 0 節與已知範圍列出，第 7 節逐條對齊尚待後續允許閱讀時核對，不能宣告 B_DONE。
- 本 session 未改 src、tests、參數或保護目錄；未啟動背景計算、未 commit。無 RUN/WAIT 待辦。下一 session 從第二階段計畫與完成條件核對接續，本 session 到此結束。

## 階段 B：SA 第 1/3 輪（2026-10-23，待訓練，尚未保留）

- 本輪依 diagnosis.md 排序表先做 SA：訓練用五筆的診斷平均 band 損失 1.339185% ≥ 0.5%。chain target windows / ejection 分 band 尚未實作，後續仍須完成與各自判定；toy 不在本輪授權內。
- 已閱讀 HANDOFF 第 0、4、6、7 節及本 WORK_STATE；不讀其他 HANDOFF 段落。原始碼只改本版本 src 與 tests；本檔為使用者明確要求的狀態紀錄例外。未改保護目錄、未 commit。
- SA 單一設計：在既有同步點以完成批次 round 決定 band 分界，原本首分界 1 / 0.5 個 band 的二相循環改成 1 / 0.5 / 1.25 / 0.75 的四相循環，後續分界仍相隔一 band。使分界兩側可輪流處於同一 worker；最小列寬與尾 band 合併邏輯沿用。
- 不改溫度、moves、sync 頻率、接受規則、時間限制、額外預算與執行緒選擇；未用 e08/e16 調參。這是待驗證假說，不宣稱品質改善。
- 合併沿用不相交列／cell 寫入與 barrier；每批真實 HPWL 更新及 bestP 保留不變。main 的 guardedStep 仍於例外或不合法時退回 SA 前狀態，Pool 等全部 worker 結束才傳遞例外。
- 新功能測試：parallel_sa_test 驗證四相分界的精確位置與週期、單 band、稀疏列／薄尾處理；固定工作量兩次的 seg/site、best、accepted、attempts 相同，並檢查合法性及 HPWL cache。固定工作量測試關閉 wall-clock starvation heuristic；另保留既有 starvation 與零時間測試。
- tests/run_tests.py 新增互斥 --tsan 模式、編譯限定可用 CPU 16–19，執行測試不綁核。一般 10/10 PASS；sanitizer 結果於下方補記。
- tag：sa_boundary_r1；目前 src_sha256（與 phase2_train.py 同演算法）：`4e59ed648a9cfb4f859e2c8cde8dca84f8e38571db5c0f94508232d2f2613258`。
- 本 session 不執行長時間訓練、不啟動背景工作。交由協調者：`python3 Testcases/results/CodexParallelChain/scripts/phase2_train.py sa_boundary_r1`；WAIT：`Testcases/results/CodexParallelChain/phase2/train/sa_boundary_r1/stage1.done.json`。
- 下一 session 先讀 `phase2/train/sa_boundary_r1/compare.md` 並把數值寫回本檔。注意此腳本用 --no-sa，僅能判斷 Stage 1 回歸，不能單憑它保留 SA；且目前腳本 reference 指向既有 v6 final，並非同輪重跑，不可宣稱已滿足配對規定。
- SA 保留閘門：ibm01、ibm05、h06、h07、h08 同合法起點、自動執行緒、固定次數、3 seeds，對同輪 v6 幾何平均改善 ≥ 0.3%、無單筆退步 > 0.5%；固定次數重跑逐位一致、TSan 0 警告。另須報固定 wall-time 配對品質；不得以 Stage 1 無變化冒充 SA 品質證據。
- 每項最多 3 輪；本 SA 為第 1 輪候選，未有保留結論。三輪仍失敗即撤回到上一保留點（目前為原始 v6 src）；記入「試過沒用」。不能在本輪訓練返回前開始下一改動。
- B 最後仍須 final_b Stage 1（protocol hash 等於最終 src）、如保留 SA 則 final_b L1/L2 合法且 <240/<600 秒、全套一般/ASan/UBSan/TSan、各項保留或撤回數據及 hash；目前不得宣告 B 完成。

- 本輪測試完成：一般、ASan/UBSan、TSan 均 10/10 PASS（各 28 個 build/link/run 記錄）；TSan 0 警告，ASan/UBSan 無診斷。記錄路徑：Testcases/results/CodexParallelChain/{tests,tests_sanitized,tests_tsan}/records.json 與各測試 .log。ASan 在沙箱內設 ASAN_OPTIONS=detect_leaks=0，LeakSanitizer 尚待協調者沙箱外補跑，不宣稱已驗證洩漏。
- 10-23 05:15 phase2/train/sa_boundary_r1/stage1 完成（258 秒）
- 10-23 05:15 phase2/train/sa_boundary_r1 完成（預設）：見 phase2/train/sa_boundary_r1/compare.md

## 階段 B：SA 第 1/3 輪補足同輪配對（2026-10-23，等待中）

- 已讀上次 `phase2/train/sa_boundary_r1/stage1.done.json` 與 `compare.md`：258.37 秒、11/11 合法、雙條件 8/11、對 ChainDP 最大 GM 0.99605、總位移 GM 0.99833。歷史 v6 為 0.99604／0.99813；本輪 `--no-sa` 與歷史完整流程不等價，不能把 HPWL 2.00744 對 0.99013 當作 SA 退步，亦不能據此保留改動。既有 Stage 1 不重跑；最後仍需真正同輪 final_b。
- 判斷：先補足本輪 SA 閘門，不消耗第 2 輪、不調參、不開始 chain 改動。src 與 tests 均未改；src hash 仍為 `4e59ed648a9cfb4f859e2c8cde8dca84f8e38571db5c0f94508232d2f2613258`。保留／撤回結論尚未作成。
- 新佇列 `Testcases/results/CodexParallelChain/scripts/phase2_sa_pair.py`；結果 `phase2/sa_pair/sa_boundary_r1/`。保存雙方 src 快照、共同 driver、二進位／輸入／合法起點 SHA256，v6 與候選交錯同輪執行，且交替先後順序。五筆訓練測資 ibm01、ibm05、h06、h07、h08；每筆 seeds 1、2、3，無 edge 調參。
- 三個獨立 SA 協定：`fixed_single` 單執行緒 5M proposals；`fixed_auto` 自動執行緒每 band 3M（沿用診斷工作量）；`wall10_auto` 原預設工作量上限與自動執行緒、10 秒。全都共用合法起點、polish=0。這些是隔離 SA 的訓練協定，不冒稱最終 end-to-end fixed/equal/default。每項報三 seeds HPWL 平均／範圍與配對比值，協定不混算。
- 固定次數兩協定各跑兩次，比對輸出 placement、完整 seg/site、浮點 initial/best/HPWL 與 attempts/accepted 的精確序列；只在固定次數 driver 關閉依速度推估的 starvation heuristic，雙方一致且不更動 production src。固定 auto 各 band 的取整會依 placement 造成小量 attempts 差異，完整記錄，不宣稱兩版本實際 proposals 逐一相等。
- `summary.json` 預先固定判斷：固定 auto 五筆的三 seeds 平均配對比值之 GM ≤0.997，且所有單 seed 比值 ≤1.005（比逐筆均值不退步 >0.5% 更嚴格）；重跑一致與 TSan 仍獨立核對。無條件提前保留；失敗也保留數據供下一 session 判斷剩餘輪次。
- 計時雙鎖：本版本 `timing.lock`＋唯讀開啟 v6 `timing.lock`，worker 不使用 taskset；只有 build 使用 v6q 的 CPU 16–19。每項 `.done.json` 可續跑；worker 最多 240 秒、每次另以 check_pl 驗證合法性。保護資料夾 hash 前後核對，全部成功才寫 ALL_DONE；出錯寫 FAILED.json。
- 已完成雙版本編譯與輸入預檢：`phase2/sa_pair_prepare.log`；沒有啟動背景工作。既有一般／ASan/UBSan／TSan 測試對應同一份 src，無須本輪重跑；ASan 先前為 `ASAN_OPTIONS=detect_leaks=0`，完整 LeakSanitizer 仍待沙箱外補跑。
- 本 session 已直接跑完短驗證：ibm05 seed1 fixed_auto，雙方各兩次共 4 個 checkpoint；均合法、8 threads，各自重跑完整狀態逐位一致。v6 HPWL 10147365.5／23995921 attempts；候選 10093594.5／23995835 attempts。單 seed 僅驗證佇列與重現性，不能推論保留；詳見 `phase2/sa_pair_smoke.log` 與 `phase2/sa_pair/sa_boundary_r1/fixed_auto/ibm05/seed1/`。保護資料夾 hash 檢查通過。
- 等待中：協調者執行 `python3 Testcases/results/CodexParallelChain/scripts/phase2_sa_pair.py sa_boundary_r1`；完成標記 `Testcases/results/CodexParallelChain/phase2/sa_pair/sa_boundary_r1/ALL_DONE`。完整訓練超過 5 分鐘，故交外部執行；已完成的小樣本工作會依 marker 跳過。
- **下一步**：先讀上述 `ALL_DONE`、`compare.md` 與 `summary.json` 的 gate/reproducibility，將數據與判斷記回本檔。若 SA 閘門失敗，依原因決定第 2/3 輪或提前撤回，總計最多 3 輪；chain target/DP 與 ejection 尚未實作或判定。B 尚未完成，不凍結、不產生新 seed、不進 C、不 commit。
- 10-23 05:35 phase2/sa_pair/sa_boundary_r1 completed; see compare.md and summary.json


## 階段 B：SA 保留；target windows 第 1/3 輪（2026-10-23）

- 已讀上次 `phase2/sa_pair/sa_boundary_r1/{ALL_DONE,compare.md,summary.json}`。固定單緒 GM 1.00000000；固定自動緒 GM 0.99174693（改善 0.825307%）；wall10 自動緒 GM 0.99143710（改善 0.856290%）。五筆各 3 seeds 的平均與範圍見 compare.md，三協定不混算；wall10 不是完整 end-to-end。
- **保留 SA 四相邊界**：固定自動緒改善 ≥0.3%，最差單 seed h07 +0.423939% <0.5%，固定工作量 60 組重跑逐位相同；該份 src 的 TSan 10/10 PASS、0 警告。判定與 src／測試證據已另存 `phase2/accepted/sa_boundary_r1/`；這是撤回用的保留點，不是階段 C 凍結。保留點 hash `4e59ed648a9cfb4f859e2c8cde8dca84f8e38571db5c0f94508232d2f2613258`。既有 ASan 是 detect_leaks=0，未宣稱完整洩漏驗證。
- 原碼核對修正：`optimizeWindowsDisp` 已有平行 row bands、移動邊界與 atomic memo clock，故本輪不重做 post-chain DP；先實作仍為序列的 `targetedWindowsDisp`。ejection 的 Search 平行化／無效搜尋仍未實作或判定，後續不能忽略。
- **target windows 第 1/3 輪候選，尚未保留**：依原 anchor 順序收集來源／目標完整 segment 互不相交的 windows；遇到衝突先完成前批，再建下一個 window。每批至多 min(threads,8)，worker 只讀 DB 與 window 求解，主緒按原順序套用 RowMap 更新。涵蓋遠列及同列不相交 regions，不禁止跨 band 候選；沒有減少 roots、passes、時間或改 cap。
- 完整 segment 保留包含 region 邊界 cell，避免只看 anchor 或 cell set 的漏判；Pool barrier 之後才更新，例外時既有 safe Placement rollback。固定工作量應與原序列語意等價；deadline 僅可能多完成已收集的一批，仍須以實際秒數驗收，不預先宣稱加速。
- 新增 `TargetWindowStats`（attempted/solved/batches/maxBatch）與 `target_window_test`：獨立遠列並行、共享 segment／同列 region 衝突、60 個隨機 fixture，序列/4 workers/repeat 的 seg/site、接受與求解數相同，合法、總／最大不增加，以及零時間無動作。CMake 與 run_tests.py 都列入第 11 個測試。
- 此候選 src hash：`e3ee1e62c7f1fdd8bc7ef3c48e3cab98ed0108ab06e93ea04045259c64a7371e`。一般測試 11/11 PASS；ASan/UBSan、TSan 結果後補。日志 `phase2/target_r1_tests.log` 與 tests/records.json。
- 新佇列 `scripts/phase2_chain_pair.py target_parallel_r1`：同輪交錯 candidate/v6，11 筆訓練 Stage 1（--no-sa）；另配對 h05/h06 candidate 1.5 s 與 v6 3 s（cap 關閉）。保留 phase1 的相同 8 行 chain 計時插樁，移除後須與各自 production main 逐字一致，src 快照及編譯結果皆記 SHA256。HPWL 不作本輪宣稱；雙條件 8/11 用歷史 ChainDP 定義，其餘改善／退步／時間閘門均比較同輪 v6。
- 佇列每筆獨立 .done.json、輸入與 protocol/hash 固定；check_pl 驗證合法、拒絕 TIMEOUT／guard fallback；同持本版本與 v6 timing.lock，計時 worker 不綁核，僅 v6q 編譯綁 CPU 16–19。保護目錄 hash 前後核對，成功才寫 ALL_DONE。準備階段修正 guard 日誌辨識，初版 metadata 已存 .precheck；當時尚無測資執行，不曾丟棄訓練結果。
- 第 7 節 chain gate 原樣保留：雙條件 ≥8/11、總 GM≤0.9997、最大 GM≤1.001、逐筆最大≤1.002、chain 實秒總和不增加，h05/h06 1.5s 收益須至少達歷史 0.29%/0.44% 且不低於同輪 v6 3s。最多 3 輪；失敗可依資料提前撤回到上述 SA 保留點。B 最後仍須 final_b 同輪 Stage 1、對應最終 src hash、L1/L2 與完整測試；目前不能 B_DONE。
- 10-23 05:44 phase2/chain_pair/target_parallel_r1/jobs/stage1_toy_v6 完成（1 秒）
- 10-23 05:44 phase2/chain_pair/target_parallel_r1/jobs/stage1_toy_candidate 完成（1 秒）

- 本輪驗證已完成：一般／ASan+UBSan／TSan 均 **11/11 PASS**，各 30 筆 build/link/run 記錄；TSan 0 警告，ASan/UBSan 0 診斷。ASan 沙箱內使用 `ASAN_OPTIONS=detect_leaks=0`，完整 LeakSanitizer 仍待協調者沙箱外補跑。候選 src/hash 與測試證據封存於 `phase2/chain_pair/target_parallel_r1/test_evidence/verification.json` 及子目錄，後續測試不會覆蓋此輪證據。
- 雙版本已編譯、protocol／src／script hash 相符；短測 toy 同輪配對已直接完成（2 個 .done.json）。兩版均合法、總位移 43324.4、最大 2004.4；chain 實秒 v6 0.013843／candidate 0.015304。此小樣本僅驗證流程，不宣稱加速或通過保留閘門。詳見 `phase2/target_r1_smoke.log` 與 `phase2/chain_pair/target_parallel_r1/stage1/toy/`；正式佇列會跳過此兩筆。
- 已核對保護目錄 hash 未變；本輪只在 SA 保留點之上改 window.cpp/window.hpp，SA 邊界功能未變。沒有 commit、沒有新 seed、沒有進 C；session 結束時無背景工作。
- **等待中**：協調者從 Lab3 執行 `python3 Testcases/results/CodexParallelChain/scripts/phase2_chain_pair.py target_parallel_r1`；WAIT：`Testcases/results/CodexParallelChain/phase2/chain_pair/target_parallel_r1/ALL_DONE`。其餘訓練預計超過 5 分鐘，未在沙箱啟動。
- **下一步**：先讀該 ALL_DONE、compare.md、summary.json 的 gates／budgets／實際 chain seconds，將本輪 target windows 判定寫回本檔。失敗時按主因決定第 2/3 輪或提前撤回（只撤回此輪 window 改動，保留 SA）；成功則記新保留點。接著處理尚未完成的 ejection 平行化／無效搜尋項；不可用 SA 已通過或 toy 短測宣告 B 完成。最終階段需修正既有 phase2_train.py 的歷史參考問題，讓 final_b 真正同輪配對，再做第 7 節所有檢查。
- 10-23 05:54 phase2/chain_pair/target_parallel_r1 completed; see compare.md and summary.json

## 階段 B：target windows 撤回；ejection 第 1/3 輪（2026-10-23）

- 已讀上次 `phase2/chain_pair/target_parallel_r1/{ALL_DONE,compare.md,summary.json}`。雙條件 8/11；總位移 GM 1.00041344（退步 0.041344%）、最大 GM 0.99862747；h04 最大比 1.00260668，超過逐筆 +0.2% 門檻。chain 實秒合計 candidate 14.239835989 / v6 14.558779537（僅省 2.19%）。h05 在 1.5s 收益 0.261639% < 配對 v6 3s 0.288901%／歷史門檻 0.29%；h06 0.364283% < 0.434934%／0.44%。三個品質閘門失敗。
- **試過沒用／撤回 target windows（第 1 輪提前停損）**：節省時間太小，且總位移、h04 最大位移與半預算收益均未達標，沒有證據支持只調 batch 大小可補足差距，不消耗第 2/3 輪。window.cpp/window.hpp 已逐檔還原到 SA 保留點；target_window_test.cpp 封存於該輪 test_evidence 並退出目前測試清單。決策 `phase2/chain_pair/target_parallel_r1/decision.json`；該輪數據及程式快照保留。SA 四相邊界仍保留，post-chain DP 本來即已平行，沒有新增 DP 改動。
- **ejection 第 1/3 輪候選，尚未保留**：先處理診斷所列無效搜尋工作，將每個 DFS node 的完整候選排序改成 occupied／empty 各自 partial_sort，只對最後保留的 beam 合併排序。保留原 empty 與 occupied 配額、總排序鍵、候選生成、roots/depth/expansions、時間預算及接受規則。保留候選的 seg/site 唯一，排序鍵為全序，因此固定工作量應逐位相同；限時吞吐與品質必須實測。
- 本輪**沒有**把可變 Search／RowMap 交給多執行緒；ejection 分 band 仍未實作。依 diagnosis 第 3 優先項先評估精確等價的少排序方案，避免一次混入跨 band 搜尋覆蓋率變動。不可將此輪通過視為分 band 已驗證，也不可因 SA 已保留而跳過 ejection 的明確決策。
- 新功能測試 chain_beam_test：2,400 組以 v6 全排序加過濾為獨立 oracle，涵蓋空集合、全空位／全 occupied、重疊壓縮、大量同分、奇數 beam 與候選不足；全部保留候選及次序相同。另用保留點 chain.cpp 獨立編譯 reference，套用現有 chain_test 全部固定工作量 fixture＋300 個隨機 placement，seg/site、roots/nodes/candidates/accepted/longest/multiBlocker/refreshed 全相同（6,569 accepted chains、2,673 multi-blocker steps）。證據 `phase2/ejection_beam_r1_equivalence/{driver.cpp,build.log,run.log,done.json}`；極短 wall deadline 不作逐位配對，仍由原測試檢查回滾。
- 一般 11/11 PASS（新增 chain_beam 替換已撤回 target_window）；ASan/UBSan、TSan 正在本 session 執行，結果另補。沙箱 ASan 明確設 `ASAN_OPTIONS=detect_leaks=0`；完整 LeakSanitizer 仍待協調者沙箱外補跑，不宣稱洩漏已驗證。
- 新佇列 `scripts/phase2_ejection_pair.py ejection_beam_r1`，沿用先前成功的同輪交錯雙版本 Stage 1 11 筆，以及 h05/h06 candidate 1.5s / v6 3s 配對。未修改原 target 輪腳本及證據；新增每次 ejection 的 roots/nodes/candidates/accepted 計數，存個別與彙整 ejection_work.json。仍採第 7 節完整 chain 閘門（包括總 GM≤0.9997、雙條件≥8/11、最大 GM≤1.001、逐筆最大≤1.002、實秒不增加與兩筆半預算收益）；本候選不以較寬鬆門檻事後挑選。
- 此輪候選 src_sha256：`2fc4f106a3f84835e9f92afbf40d0befa0eb9f5ac7ab1160b58373fc96cf921a`。未 commit、未改保護目錄、未產生新 seed、不進 C。
- 本輪最終短驗證：一般／ASan+UBSan／TSan 均 **11/11 PASS**，各 30 筆 build/link/run，TSan 0 警告、ASan/UBSan 0 診斷；ASan 使用 detect_leaks=0。用時約 44／151／79 秒，各工作均小於 5 分鐘。對應候選 hash 的不可覆寫證據已封存於 `phase2/chain_pair/ejection_beam_r1/test_evidence/verification.json` 與各子目錄。
- 雙版本編譯、protocol/src hash、保護目錄 hash 核對通過。toy 同輪配對短測已完成兩筆 jobs/*.done.json，均合法；各 root/node/candidate/accepted counter 皆記錄成功。這僅驗證訓練流程，不作收益宣稱；正式佇列會跳過已完成的 toy。詳見 `phase2/ejection_r1_{prepare,smoke}.log` 與 `phase2/chain_pair/ejection_beam_r1/stage1/toy/`。
- **等待中**：協調者從 Lab3 執行 `python3 Testcases/results/CodexParallelChain/scripts/phase2_ejection_pair.py ejection_beam_r1`；完成標記 `Testcases/results/CodexParallelChain/phase2/chain_pair/ejection_beam_r1/ALL_DONE`。完整訓練預計超過 5 分鐘，未在沙箱啟動；沒有殘留背景工作。
- **下一步**：先讀上述 ALL_DONE、compare.md、summary.json 的 gates/budgets/chain_seconds，必要時只讀 ejection_work.json 的 h05/h06 計數，判定此輪保留或撤回。ejection 最多 3 輪，若幾乎沒有吞吐或品質收益可依停損提前撤回至 SA 保留點；如考慮第 2 輪分 band，必須先解決完整 Search touched-set 隔離、跨界補做與 barrier cap 重驗，不能因目前排序等價就假設分 band 安全。所有 ejection 方案須有明確保留／撤回結論才能進最後驗收。B 尚未完成；最終仍須修正 phase2_train.py 歷史參考問題後做真正同輪 final_b Stage 1、protocol hash 對應最終 src、final_b L1/L2 合法及 <240/<600 秒、全套測試與最終 hash。未凍結、不產生新 seed、不進 C、不 commit。
- 10-23 06:11 phase2/chain_pair/ejection_beam_r1 completed; see compare.md and summary.json

## 階段 B：ejection 撤回；最終驗收準備（2026-10-23）

- 已讀上次 `phase2/chain_pair/ejection_beam_r1/{ALL_DONE,compare.md,summary.json,ejection_work.json}`。雙條件 8/11；總位移 GM 0.999931693871（僅改善 0.006831% < 0.03%）、最大 GM 1.00000000；chain 實秒合計 candidate 14.547725445 / v6 14.547820982，沒有實質加速。h05 半預算收益 0.261639% < v6 3s 0.288901%／歷史 0.29%；h06 0.367677% < 0.434905%／0.44%。同預算 h05 nodes 304354 / 305877、h06 138682 / 140362，吞吐分別減少約 0.50%／1.20%。
- **試過沒用／ejection 第 1 輪提前撤回**：沿用已存在的 `phase2/chain_pair/ejection_beam_r1/decision.json`（本 session 開始時已存在），沒有收益證據支持第 2/3 輪。檢查發現 src 已與 `phase2/accepted/sa_boundary_r1/src` 逐檔相同；故不重做還原，確認 hash `4e59ed648a9cfb4f859e2c8cde8dca84f8e38571db5c0f94508232d2f2613258`。chain_beam 測試亦已退出目前清單，候選證據仍保留。上次 WORK_STATE 未記錄這些落盤結果，本節補齊。
- **ejection 分 band 本次 B 不採用**：沒有完整 Search touched-set 隔離、跨界補做及 barrier cap 重驗的已驗證實作；先前精確等價少排序亦沒有吞吐收益，因此不擴展第 2 輪風險。這是提前停損／不保留的決策，並非宣稱已測得分 band 無效。target windows 已撤回；post-chain DP 本來就平行，沒有新增改動。所有項目已明確判定，最後僅保留 SA 四相邊界。
- 已修正 `scripts/phase2_train.py` 與 `phase2_large.py`，共用新 `phase2_acceptance.py`。舊腳本存 `phase2/final_b_setup/*.before`。train 使用最後 production 原碼（無計時插樁）對 11 筆訓練逐筆交錯重跑 candidate/v6；歷史 ChainDP 僅用雙條件 8/11 定義，所有 v6 比率都來自本輪。protocol 記錄 src、腳本、helper、輸入、checker、binary hashes，快照或協定變更拒絕沿用 marker；雙 timing.lock，只有編譯綁核。
- 最後僅保留 SA，故 final Stage 1 檢查總 GM≤1、最大 GM≤1.001、逐筆最大≤1.002、雙條件≥8/11、額外步驟 used≤budget（0 超時），不再要求已撤回 chain 功能的 0.03%／半預算改善。兩種輸出 placement 都 check_pl；TIMEOUT、guard fallback、非法 placement 直接失敗。large 沿用既有 L2 seed 1/2、L1 seed 1，完整預設執行，L1<240s、L2<600s；不新生 seed，不用大例調參或宣稱 HPWL 改善。
- `scripts/phase2_final_b_queue.py` 供協調者依序完成 train final_b、large final_b，以及沙箱外完整 ASan/UBSan（detect_leaks=1）；最後 marker 是驗收資料完成，非自動 B_DONE。沙箱內正做最後原碼一般／ASan+UBSan（detect_leaks=0）／TSan 驗證，結果後補。一般已 10/10 PASS，28 筆 build/link/run；證據保存在 `phase2/final_b_validation/test_evidence/tests/`。

- 本 session 最後短驗證完成：一般、ASan/UBSan、TSan 均 **10/10 PASS**，各 28 筆 build/link/run；用時 41.2／139.1／68.5 秒，單項都少於 5 分鐘。TSan 0 警告、ASan/UBSan 0 診斷。沙箱 ASan 使用 `ASAN_OPTIONS=detect_leaks=0`、UBSan `halt_on_error=1`，不能據此宣稱洩漏驗證通過。完整 test/source hashes 與不可覆寫日志已存 `phase2/final_b_validation/test_evidence/verification.json` 及各子目錄。
- train final_b 的雙 production binaries 與 large final_b candidate 已編譯，protocol／src／binary hashes 核對通過。toy 同輪短測 2 個 jobs/*.done.json 已完成，兩版均合法、總位移 43324.4、最大 2004.4；extra used candidate 0.668260／v6 0.654251，budget 12 秒。這只驗證流程，不是最終訓練結論。正式佇列跳過這兩筆；記錄 `phase2/final_b_setup/{train_smoke,large_prepare}.log`。保護目錄 hash 未變；本 session 未修改 production src，未 commit、未新生 seed、未凍結、未進 C，無殘留背景工作。
- **等待中**：協調者從 Lab3 執行 `python3 Testcases/results/CodexParallelChain/scripts/phase2_final_b_queue.py`；WAIT：`Testcases/results/CodexParallelChain/phase2/final_b_validation/ALL_DONE`。依序 train final_b 剩餘同輪配對、large final_b L2 seed1/2 + L1 seed1、沙箱外完整 ASan/UBSan（detect_leaks=1）。計時程式不綁核；只有 v6q／測試 runner 的編譯綁 16–19。佇列過程若程序失敗會寫 FAILED.json，不寫成功 marker；若只是數值門檻失敗，ALL_DONE 中 gates=false，仍須人工判定，不能視為通過。
- **下一步**：只讀 `phase2/final_b_validation/ALL_DONE`、`phase2/{train,large}/final_b/{ALL_DONE,compare.md,summary.json,protocol.json}` 與 `phase2/final_b_validation/{test_evidence/verification.json,tests_full_asan/records.json}`，核對 final src hash、每項門檻、0 額外步驟超時、全合法及 L1<240／L2<600。完整 ASan 日志須無診斷；一般／TSan 證據不可被 full ASan 覆蓋。若全通過，回顧 SA 保留與 target/ejection 撤回的數據路徑、寫最終 hash 及「B 完成，下一步 C」，最後一行 B_DONE；未全通過則按失敗原因處理，不重跑已完成成功步驟、不放寬門檻、不先進 C。目前 **B 尚未完成**。
- 10-23 13:43 phase2/train/final_b completed; all_gates=False
- 10-23 13:54 phase2/large/final_b completed; all_gates=False

## 階段 B：最終驗收失敗，保守撤回 SA（2026-10-23）

- 已核對上次完成標記、train/large summary 與 protocol：來源 hash 均為 `4e59ed648a9cfb4f859e2c8cde8dca84f8e38571db5c0f94508232d2f2613258`；marker 只是資料完成，train=false、large=false，不能 B_DONE。Stage 1 總 GM 0.99956280、最大 GM 1.00160930、雙條件 8/11、額外步驟 0 超時；h06 最大比 1.017845448592（+1.784545%，超過 +0.2%），最大 GM 亦超過 +0.1%。L1 187.50s、L2 seed1/2 211.42/212.77s，全部合法且在限制內；L2 seed2 額外步驟 6.336646s > 6s，不能略去該失敗。
- 原碼差異只有 annealer.cpp 與 sa_bands.hpp；Stage 1 使用 --no-sa，兩項失敗均在 SA 之前。h06 日志顯示微小 chain/DP 起點差異後，v6 cap 首候選 accepted（total 891019.06），candidate 首候選 total_budget（892648.88），後者最後最大仍 435.19、v6 427.56。L2 seed2 第二次 cap repair 用 1.793603s，結束後 extra 已超時。這些是日誌證據，**不能歸因 SA**，亦不把它們當成調整大例參數的理由。
- **最終保守撤回 SA**：單項固定自動緒改善 0.825307% 與 wall10 0.856290% 的證據仍有效（`phase2/sa_pair/sa_boundary_r1/`），但候選未取得完整 final_b 通過證據。不對未改原碼反覆抽樣直到通過，不放寬門檻；停止保留新版，還原 v6。這是最終驗收失敗後的風險決策，不宣稱已證明 SA 無效，也不消耗 SA 第 2/3 輪調參。target/ejection 的撤回理由維持原決策路徑。
- 所有舊 final_b 資料、production src/tests 與 helper 已封存於 `phase2/rejected/final_b_sa_boundary_r1/`：原 `train/final_b` → `train/`、`large/final_b` → `large/`、`final_b_validation` → `validation/`；原日志中的絕對路徑依此映射，不修改歷史數值。決策見該目錄 `decision.json`。先前一般／ASan+UBSan／TSan 各 28 記錄及逐檔 SHA 已核對；沙箱外完整 ASan+UBSan 28 記錄全成功、日志 0 診斷，原證據未遺失。
- 最終 production src 現已與保護 v6 **逐檔逐位一致**，hash `03e62094172ff94a039d552fdc38e556dc1c16f0786b7dd5d8a7e579a85c47b2`；還原 parallel_sa_test.cpp，保留 tests runner 的編譯綁核／TSan 選項。沒有 production 改動保留；若最後驗收通過，下一步 C 必須是「不凍結新版、直接寫報告說明沒有通過完整驗收的改善」，並誠實保留 SA 單項改善結果。
- 原碼已改為 v6，因此舊 hash 的成功步驟不能充作新 final 原碼驗收。安排新 `phase2_train.py final_b`／`phase2_large.py final_b`，**所有既有數值門檻原樣保留**，不共用舊 marker；連同 L1/L2 與額外超時條件都保留。只修 protocol 說明為全部撤回，沒有改 acceptance 算法或 gate。若基線也不通過，要如實記為基線／量測限制並處理，不可當作自動豁免。
- 新 `phase2_short_validation.py` 每次只執行一套 <5 分鐘測試，雙 timing lock、只有編譯綁核，僅封存本套 records 實際引用的日志，避免殘留已撤回測試混入證據。final queue 現會驗證最後 src/tests 與三套證據 hash，再接受沙箱外完整 ASan 的 records／日志；Python 語法檢查成功。

- 還原後最終短驗證：一般／ASan+UBSan／TSan 均 **10/10 PASS**，各 28 筆記錄；耗時 40.9／138.1／68.3 秒。ASan 使用 `ASAN_OPTIONS=detect_leaks=0`，TSan 0 警告；對應原碼／測試／日志 SHA 已封存並重新核對於 `phase2/final_b_validation/test_evidence/verification.json`。這份原碼的完整 LeakSanitizer 仍由協調者補跑，舊 SA 原碼通過證據不能冒用。
- 新 final_b 的 train 雙 production binaries 與 large candidate 已編譯，protocol/src/binary hashes 核對成功；`src_sha256` 與 `reference_sha256` 都是 `03e62094172ff94a039d552fdc38e556dc1c16f0786b7dd5d8a7e579a85c47b2`。日誌 `phase2/final_b_rollback_setup/{train,large}_prepare.log`；保護目錄 hash 未變，未 commit、未新生 seed、未凍結、未進 C，沒有殘留背景工作。
- **等待中**：協調者從 Lab3 執行 `python3 Testcases/results/CodexParallelChain/scripts/phase2_final_b_queue.py`；WAIT：`Testcases/results/CodexParallelChain/phase2/final_b_validation/ALL_DONE`。此為還原後 hash 的新驗收，不沿用舊 marker；正式計時不綁核，完整佇列超過 5 分鐘，未在本 session 啟動。
- **下一步**：讀新的 `phase2/final_b_validation/ALL_DONE`、`phase2/{train,large}/final_b/{compare.md,summary.json,protocol.json}` 與短／完整 sanitizer 證據，只判斷還原後來源。保持全部門檻；若全過，最終結論為「全部 production 改動撤回，B 完成，下一步 C：不凍結新版、直接寫報告」，須保留 SA 單項改善與最終驗收失敗的區別。若基線也失敗，先核對失敗原因與協定，不以 source 相同為由跳過 gate、不對同份原碼反覆抽樣直到通過；仍不得 B_DONE。

## 協調者備註（Claude，10-23 14:10；給下一個 B session）
- SA 的 band 分界改動只在 Stage 2（annealer.cpp），不會影響 Stage 1：h06 的最大位移、L2 的額外步驟時間都是 Stage 1 的結果。
- 正在跑的還原版（等同 v6）final_b 驗收，就是判斷這兩項是不是「v6 本身的執行間變動／既有問題」的對照：
  - 若還原版也出現同樣的 h06 最大位移變動或 L2 額外時間超出 → 不是 SA 造成的；SA 應依 HANDOFF 第 7 節的同次數協定（已量到 GM 改善 0.825%）判定保留，
    並把 h06／L2 記為 v6 既有的變動（v6 的 0/27 超時只量了 27 筆，沒有量 L1/L2 的額外步驟）。
  - 若還原版沒有這兩個問題 → 才表示和 SA 有關，維持撤回。
- 10-23 14:13 phase2/train/final_b completed; all_gates=False
- 10-23 14:24 phase2/large/final_b completed; all_gates=False

## 階段 B：v6 對照仍失敗；依協調者備註恢復 SA 單項保留（2026-10-23）

- 已消化上次等待結果：`final_b_validation/ALL_DONE` 的 train=false、large=false、full_asan_ubsan=true；沒有把完成標記當通過。還原版原碼、train/large protocol、snapshot、binary、輸入及 helper hashes 已由 `validate()` 核對，來源確為 v6 hash `03e62094172ff94a039d552fdc38e556dc1c16f0786b7dd5d8a7e579a85c47b2`。
- Stage 1：總位移 GM **1.0000003706805136**（退步 0.0000370681%，雖小仍未通過 ≤1），最大 GM 1、逐筆最大比全為 1、雙條件 8/11、額外步驟 0 超時。h06 最大兩版皆 427.55；上次 h06 最大退步沒有重現。相同來源的總位移仍有執行間變動，但不因此放寬 gate 或重抽樣。
- L1 seed1 **187.59 s**、L2 seed1/2 **199.26/199.24 s**，全部 placement 合法且符合 <240/<600 s；L2 額外步驟 **6.127434/6.135080 s > 6 s**，兩筆失敗。L1 extra=5.721812 s。L2 的超時在 byte-identical v6 亦出現，支持其為既有問題，不能歸因 SA；v6 歷史 0/27 並未涵蓋 L1/L2，不能用來否定這次觀測。
- 唯讀定位：`src/capsearch.cpp` 的 `capSearch()` 將 repair 時間限制設為 min(.4,.15*left)，`repairAt()` 雖傳入 phase1/phase2 deadline，L2 日誌第二個 unresolved repair 仍耗 1.589675/1.593705 s。這指出子程序期限涵蓋範圍有待檢查，尚未證明哪一段初始化／收尾造成超時；沒有依大例調參、縮減搜尋或擅自更改 acceptance。
- **更正上節保守撤回決策：恢復 SA 單項保留。** 協調者 14:10 備註的「還原版 L2 亦超時」條件已成立；恢復 `annealer.cpp`、`sa_bands.hpp`、`tests/parallel_sa_test.cpp` 至原保留點。固定自動緒三 seeds GM 改善 0.825307%、最差單 seed +0.423939%、60 組逐位相同、TSan 0 警告的證據仍在 `phase2/sa_pair/sa_boundary_r1/` 與 `phase2/accepted/sa_boundary_r1/`。h06 未重現也不能反過來證明是 SA 所致：Stage 1 未執行 SA，這一因果判定仍不成立。**單項保留不等於 final_b 通過。**
- target windows、ejection 繼續撤回，數據分別在 `phase2/chain_pair/target_parallel_r1/`、`phase2/chain_pair/ejection_beam_r1/`；本 session 未開新的實作／調參輪。SA 最終失敗資料仍在 `phase2/rejected/final_b_sa_boundary_r1/`。
- v6 對照整包已封存至 `phase2/rejected/final_b_v6_control/{train,large,validation,src,tests,phase2_acceptance.py,decision.json}`；原 final_b 路徑已移走，以免把舊來源 marker 誤認為目前 SA 原碼的驗收。檔內歷史絕對路徑按此搬移映射，未修改原數據。decision.json 記錄此輪 gates、完整 sanitizer 日誌 hash 與判定。
- 對照版一般／沙箱 ASan+UBSan／TSan 各 28 records、10/10 PASS，逐檔 SHA 已重驗；沙箱 ASan 使用 `ASAN_OPTIONS=detect_leaks=0`。協調者沙箱外完整 ASan+UBSan（detect_leaks=1）亦 28 records 全成功；四套日志均無 sanitizer 診斷。恢復後的 src/tests hashes 與先前 SA 最終驗收測試證據完全一致，不重跑已成功測試；沒有用對照版測試冒充 SA 證據。
- **目前 source hash：`4e59ed648a9cfb4f859e2c8cde8dca84f8e38571db5c0f94508232d2f2613258`。B 尚未完成。** 恢復的 SA 原碼仍只有先前失敗的 final_b，未放寬或豁免任何 gate，也不能直接重跑這份原碼取較好數字。`phase2_acceptance.py` 的 note 仍是歷史「全部撤回」，故未準備新 protocol／final queue；禁止直接執行既有 final queue。
- **下一步／阻塞原因**：先處理 v6 既有的 cap deadline 正確性與相同來源 Stage 1 量測變動。若實作期限修正，必須先用 testcases/holdout 的新功能測試及新一輪同輪訓練判定（小改動最多 3 輪），不得以 L2 調參，也不得讓 v6 reference 的失敗被忽略；只有來源或有明確依據的協定修正後才建立新 final_b。若維持目前原碼與全部現有 gate，兩份來源均已有失敗證據，沒有可合法直接宣告 B_DONE 的路徑。無等待中工作、無 RUN/WAIT；本 session 停在 B 驗收阻塞，不進 C、不凍結、不新增 seed、不 commit。保護資料夾 hashes 核對未變。

## 協調者備註 2（Claude，10-23 14:35）
- HANDOFF 第 7 節「不比 v6 差」的 Stage 1 項目，以第 7 節寫的執行間變動為容許範圍（總位移約 0.11%、最大約 0.13%）；
  總位移 GM 1.00000037（差 0.00004%）在範圍內，算通過，不是退步。SA 改動不碰 Stage 1，這項不需要再花一輪。
- L2 額外時間超出是 v6 既有問題（還原版也出現），修正它是正確的下一步；修好之後的最後檢查只需要讓 L1/L2 與 27 筆的額外步驟都在預算內。
- B 的 session 上限還剩 3 次（第 10 次會強制收尾）；請以「cap deadline 修正 → 一次 final_b → B_DONE」為目標。

## 階段 B：cap deadline 第 1/3 輪；修正後 final_b 等待中（2026-10-23）

- 已實讀上次封存的 `phase2/rejected/final_b_v6_control/validation/ALL_DONE` 與 train/large summaries：train=false、large=false、full ASan=true；總 GM 1.0000003706805136、最大 GM 1、雙條件 8/11、train 額外步驟 0 超時，large 僅 extra gate 失敗。沒有未消化的背景工作。依協調者備註 2，採取期限修正後一次 final_b；不對原封不動的來源重抽樣。
- **期限修正第 1 輪，尚未判定保留**：`src/capsearch.cpp` 的 `Repair` 建構子會在進入原有 deadline-aware 搜尋迴圈之前，建立全體細胞／逐列 lateness；逐列評分與最後 placement 亦缺乏期限檢查。新增只由 `rowRepair` 捕捉的內部取消例外；建構、逐列 lateness、候選列、排序 key／window 的每 256 次操作、clumping 及最後提交都檢查既有 phase2Until。超時直接回傳 false，caller placement 逐位不變；不增加預算、不改 L2 參數、不修改保護 v6。這證明並修正可重現的期限缺口，但完整 L1/L2 是否消除超時仍待驗收，不提前宣稱已解決所有期限路徑。
- 新功能測試 `tests/cap_deadline_test.cpp`：60,000-cell 合成案例中，過期期限拒絕、2ms 初始化中斷後 placement 不變（實測含清理 2.659ms；單元測試 0.5s 上限只容許 sanitizer/allocator 開銷，**正式 extra gate 不加任何寬限**）。相同測試連結 v6 capsearch 時於 expired-deadline assertion 失敗（SIGABRT，關閉 core）；證據 `phase2/cap_deadline_r1/fixtures/{ALL_DONE,v6_expected_failure.log}`。另用 testcases/toy 與 holdout/h06 的既有合法 placement 驗證，期限／合法性／失敗不改 placement 均通過；未以 edge 或 L2 調參。
- 最終來源一般／ASan+UBSan／TSan 均 **11/11 PASS**，各 30 records；42.66／144.00／76.23 秒。TSan 0 警告、sanitizer 0 診斷；沙箱 ASan 使用 `ASAN_OPTIONS=detect_leaks=0`，完整 LeakSanitizer 由外部佇列補跑。精確 source/tests/log hashes：`phase2/final_b_validation/test_evidence/verification.json`。新增測試使舊 10-test 證據不可冒用；三套新證據皆已重新核對。
- 協定修正有明確依據：依備註 2 的 0.11% 執行間變動，Stage 1 總 GM gate 改為 ≤1.0011；最大 GM ≤1.001、逐筆最大 ≤1.002、雙條件 ≥8/11 維持原限制。protocol 記載此授權來源及 SA 保留／deadline 第 1 輪待判定；不再沿用「全部撤回」歷史 note。train 仍同輪交錯重跑 candidate/v6，兩版 extra 都必須 0 超時，不忽略 reference 失敗；歷史控制資料不改寫。
- 新 `scripts/phase2_deadline_regression.py` 為最後 27 筆 deadline/合法性回歸：沿用本輪已完成的 11 筆 candidate train，另跑原始 edge 16 筆，避免重跑成功步驟；edge 只看合法性／額外步驟時限，不判定品質收益。已預檢 16 筆實際完整名稱、輸入 hashes。final queue 驗證新功能證據、全部短測 hashes，依序 train、large、27 筆回歸、完整 ASan；ALL_DONE 的 gates 增加 `deadline_regression_27`，任何數值失敗仍不能 B_DONE。
- train/large production snapshots 與 binaries 已編譯並通過 `validate()`；兩份 protocol src_sha256 都等於目前 **`00e81c9d621384e51cecfce2a1943c478e9e4df287451c2b55a378fa47a96746`**。toy 同輪 smoke 的兩筆 jobs marker 已完成：兩版 total=43324.4、max=2004.4、合法，extra candidate 0.700882／v6 0.680769 <12s。這只驗證流程，不能充作整體訓練結論。保護資料夾 hashes 未變；不 commit、不凍結、不新增 seed、不進 C。
- **現行判定**：SA 四相邊界依原三-seed 同次數改善 0.825307% 與 TSan／重現證據繼續單項保留，數據 `phase2/sa_pair/sa_boundary_r1/`、`phase2/accepted/sa_boundary_r1/`；target windows 與 ejection 繼續撤回，數據 `phase2/chain_pair/{target_parallel_r1,ejection_beam_r1}/`。deadline 修正待這一次新來源同輪資料判定；未新增其他調參輪。
- **等待中**：協調者從 Lab3 執行 `python3 Testcases/results/CodexParallelChain/scripts/phase2_final_b_queue.py`；WAIT：`Testcases/results/CodexParallelChain/phase2/final_b_validation/ALL_DONE`。整個佇列超過 5 分鐘，未在沙箱啟動；計時程式不綁核，僅編譯由既有 runner 綁核。
- **下一步**：讀上述 marker、`phase2/{train,large}/final_b/{summary.json,compare.md,protocol.json}`、`phase2/deadline_regression/final_b/{ALL_DONE,summary.json,protocol.json}` 與 sanitizer 證據，逐項核對新 hash、同輪最大／總位移 gates、27 筆全合法及 0 額外超時、L1<240/L2<600、完整 ASan 0 診斷。全部通過才將 deadline 第 1 輪判定保留，寫最終 hash、「B 完成，下一步 C」，回覆末行 B_DONE。若未通過，按失敗路徑針對性處理或撤回，維持最多 3 輪停損及協調者 session 上限，不對同份來源重抽樣、不以既有 v6 問題直接豁免時間 gate。**B 尚未完成。**
- 10-23 14:50 phase2/train/final_b completed; all_gates=False
- 10-23 15:01 phase2/large/final_b completed; all_gates=True

## 協調者備註 3（Claude，10-23 15:15；B 只剩 2 個 session：B9 與強制收尾 B10）
- 15:05 的 final_b：large 全過（L2 額外時間已在預算內）、27 筆 deadline 回歸 0 超時；**train 失敗**：h01 最大 +2.31%、h03 +1.23%、h06 +1.79%（h06 停在 cap 前的 435.19，一步都沒接受）。
- 原因判讀：v6 的 repair 預算是 `min(.4, .15*left)`，原本 rowRepair 常常超出這個小預算卻成功（v6 的 e07 診斷：0.56~1.83 秒）；
  現在照期限中止，中型設計（h01/h03/h06）的修復就做不完。問題不是「要不要守期限」，而是「每次修復的預算太小」。
- 建議 B9 一次做完：**保留整體期限（cap 搜尋的 until 與 [extra] 預算）為硬上限**，但把每次 repairAt 的預算放大到
  例如 `min(left - 保留量, max(.4, .5*left))`（或依 cell 數），讓修復在整體預算內做得完；然後**直接跑 phase2_final_b_queue.py**（含 train + large + 27 筆 deadline），不要另跑小訓練。
- B10（強制收尾）讀結果：若 train/large/deadline 全過 → B_DONE；若 train 仍失敗 → 撤回 deadline 修正與預算改動、只保留 SA，
  並把「L2 額外時間超出 0.13 秒是 v6 既有問題」寫進 WORK_STATE 的已知限制，然後 B_DONE（SA 在同次數協定已達標）。


## 階段 B9：cap deadline 第 2/3 輪；放大單次修復預算（2026-10-23）

- 已讀並核對上次等待結果：`phase2/final_b_validation/ALL_DONE` 的 train=false、large=true、deadline_regression_27=true、full_asan_ubsan=true；來源 hash `00e81c9d621384e51cecfce2a1943c478e9e4df287451c2b55a378fa47a96746` 與 train/large/regression protocol 一致，兩份 production snapshot/binary 通過 validate；短測文件 hashes、完整 sanitizer 無診斷、保護資料夾 hashes 均核對成功。
- 第 1 輪不保留：總位移 GM 0.9996810689553765、最大 GM 1.0048161292150548、雙條件 7/11；h01/h03/h06 最大比分別 1.0231371214698877／1.0123407899491619／1.0178692550578878，未達門檻。L2 seed1/2 212.26／212.20 s、extra 5.910547／5.844463 s；L1 187.53 s、extra 5.720506 s；全部合法，27 筆合法且 0 額外超時。一般／ASan+UBSan／TSan 各 11/11、30 records；完整 ASan+UBSan 30 records 無診斷。不能因時間已過就視為品質過關。
- 原輪 train、large、deadline_regression、validation 已完整移至 `phase2/rejected/final_b_cap_deadline_r1/`，同處另存原 src/tests/scripts 與 decision.json（含完整 ASan logs hashes）。原 cap_deadline_r1/fixtures 保留。封存檔中的歷史絕對路徑按搬移映射解讀，不改寫舊數據。新 final_b 不沿用舊 marker。
- 依協調者備註 3，僅更改 capsearch.cpp 的 repairAt 時間分配：`repairUntil=min(until-0.08, now+max(0.4, 0.5*(until-now)))`，即 `min(left-0.08, max(0.4,0.5*left))`；給初始化及修復較完整的時間並留下 0.08s 收尾。保留第 1 輪 deadline cancellation、cap 搜尋整體 until、[extra] 預算及所有驗收門檻；不改 window repair 時間、不調 L2、不以 edge 品質選參數。這是第 2 輪，尚未保留；本 session 不另跑小訓練。
- 新來源 hash：`d0b99109f3d04bdc33bdc7e90f55f1ef61ae8d828dc8a89b02747d85104ffc20`。toy/h06 deadline fixtures 新來源通過，`phase2/cap_deadline_r2/fixtures/ALL_DONE` 已記錄；v6 negative control 的 test/helper/非 capsearch 來源未變，逐檔核對後複用第 1 輪成功證據，未重跑已完成 control。
- SA 四相邊界仍單項保留（3-seed 同次數 GM 改善 0.825307%，證據 `phase2/sa_pair/sa_boundary_r1/`、`phase2/accepted/sa_boundary_r1/`）；target windows／ejection 仍撤回（`phase2/chain_pair/{target_parallel_r1,ejection_beam_r1}/`）。不得把這些單項結論當本輪 final_b 通過。
- 本輪一般／ASan+UBSan／TSan **各 11/11 PASS、30 records**，42.96／143.83／70.86 s；TSan 0 警告、sanitizer 0 診斷。沙箱 ASan 使用 `ASAN_OPTIONS=detect_leaks=0`；完整 LeakSanitizer 由外部佇列執行 detect_leaks=1。精確來源、tests 及逐檔日誌 hashes 已寫入並重驗 `phase2/final_b_validation/test_evidence/verification.json`。本次改動是既有 deadline 功能的預算調整，沿用 cap_deadline_test（合成取消／rollback）、toy/h06 fixtures 與 capsearch_test；收益由下列同輪完整驗收判定。
- 新 train/large final_b 的 snapshot/binary 已 prepare 並通過 validate()；兩份 protocol.src_sha256 均為 `d0b99109f3d04bdc33bdc7e90f55f1ef61ae8d828dc8a89b02747d85104ffc20`，protocol 明列第 2 輪與備註 3 依據。所有 gates 維持第 1 輪值；兩份 ALL_DONE 及總驗收舊 marker 均未沿用。未啟動訓練、未重抽舊原碼、未改保護目錄、未 commit、未凍結、不產生新 seed、不進 C。
- **等待中**：協調者從 Lab3 執行 `python3 Testcases/results/CodexParallelChain/scripts/phase2_final_b_queue.py`；WAIT：`Testcases/results/CodexParallelChain/phase2/final_b_validation/ALL_DONE`。整個佇列超過 5 分鐘，未在本 session 啟動；依序同輪 train、large、27 筆 deadline/合法性、沙箱外完整 ASan。計時不綁核，編譯沿用 v6q／測試 runner 的 16–19。程序失敗寫 FAILED.json；數值門檻失敗仍可能寫 ALL_DONE，必須讀 gates 判斷。
- **下一步（B10 強制收尾）**：先讀本輪 `final_b_validation/ALL_DONE`、`{train,large}/final_b/{summary.json,compare.md,protocol.json}`、`deadline_regression/final_b/{ALL_DONE,summary.json,protocol.json}`、短測 verification 及完整 sanitizer logs/records。核對此輪 hash、train 全部門檻、27 筆合法與 0 額外超時、L1<240/L2<600 及完整 ASan 0 診斷。全過才保留 deadline 第 2 輪並寫最終 hash、「B 完成，下一步 C」。若 train 仍失敗，依協調者備註 3 撤回 deadline 與本輪預算改動至 SA 單項保留點 `4e59ed648a9cfb4f859e2c8cde8dca84f8e38571db5c0f94508232d2f2613258`，明列「L2 額外超出約 0.13 秒為 v6 既有問題」及協調者指定收尾例外，核對恢復來源與對應測試證據，再寫 B 完成；不可聲稱未過的 gate 已過。若是程序／證據缺失，先處理該失敗，不能把缺少結果當 train 品質失敗。遵守 session 上限，不再開第三輪調參。**目前 B 尚未完成，沒有 B_DONE。**
- 10-23 17:12 phase2/train/final_b completed; all_gates=False
- 10-23 17:23 phase2/large/final_b completed; all_gates=False


## 階段 B10 強制收尾：撤回未達標改動，回到最後 SA 保留點（2026-10-23）

- 已核對 B9 的 `phase2/final_b_validation/ALL_DONE`，不是只看檔案存在：train=false、large=false、deadline_regression_27=true、full_asan_ubsan=true。來源 `d0b99109f3d04bdc33bdc7e90f55f1ef61ae8d828dc8a89b02747d85104ffc20` 與 train/large protocol、production snapshots/binaries、regression marker 一致；validate()、短測逐檔 hashes、完整 ASan 30 records 與無診斷均通過核對。
- **撤回 deadline 取消機制與 repairAt 預算放大，結束調參，不開第 3 輪。** 第 2 輪 train：雙條件 8/11、總位移 GM 1.0005056030750525、最大 GM 0.9998015013793347；但 ibm05 最大 +0.326784%、h03 +1.234079%，均超過逐筆 +0.2% 上限。L1 187.68s、L2 seed1/2 211.46/212.88s，placement 全合法且總時間通過；L2 seed2 extra=6.421174s >6s，時間修正也未完整成立。27 筆合法且 extra 0 超時不能抵銷前述失敗。
- 第 2 輪完整資料已封存 `Testcases/results/CodexParallelChain/phase2/rejected/final_b_cap_deadline_r2/{train,large,deadline_regression,validation,src,tests,scripts,decision.json}`。封存 regression 的 candidate symlink 改指向同包 train/candidate；其他歷史絕對路徑按搬移映射解讀。第 1 輪仍在 `phase2/rejected/final_b_cap_deadline_r1/`：train 7/11、最大 GM 1.0048161292，h01/h03/h06 最大不合格。兩輪均歸入「試過沒用」，不保留其功能或測試於最終版本。
- **保留 SA 四相邊界**：`phase2/sa_pair/sa_boundary_r1/` 與 `phase2/accepted/sa_boundary_r1/decision.json`：固定次數、自動執行緒、五案例各三 seeds 的 HPWL GM 比 v6 為 0.9917469293203764（改善 0.825307%），最差單 seed +0.423939% <0.5%，60 組固定次數重跑逐位相同，TSan 0 警告。此單項達標結論與 final_b 是否全部通過分開記錄。
- **維持撤回 target windows 與 ejection**：`phase2/chain_pair/target_parallel_r1/summary.json` 總 GM=1.0004134401、h05/h06 收穫 0.261639%/0.364283%；`phase2/chain_pair/ejection_beam_r1/summary.json` 總 GM=0.9999316939（只改善 0.006831%）、h05/h06 收穫 0.261639%/0.367677%。均不足總 GM 改善至少 0.03% 及 h05/h06 0.29%/0.44% 條件。
- 最後 `src/` 已逐檔還原至 `phase2/accepted/sa_boundary_r1/src/`，hash **`4e59ed648a9cfb4f859e2c8cde8dca84f8e38571db5c0f94508232d2f2613258`**（現有 src_hash：按檔名排序串接內容後 SHA256）。與 v6 原碼差異僅 `annealer.cpp`、新增 `sa_bands.hpp`；capsearch.cpp 已回復 v6。tests 回復該 SA 原碼的對應測試與 runner，deadline 測試保留於撤回證據包。
- **已知限制**：byte-identical v6 對照的 L2 extra=6.127434/6.135080s，超出約 0.13 秒，證據 `phase2/rejected/final_b_v6_control/large/summary.json`。協調者備註 3 曾允許記錄限制後收尾；本次使用者明確要求「最後檢查 RUN 已跑完且都成立」才 B_DONE，因此不以舊備註自動豁免 gate、不把已知超時說成通過。
- 依本次使用者指示，對還原後原碼建立新的 `phase2_train.py final_b`、`phase2_large.py final_b` 與 27 筆 deadline/合法性回歸；這是撤回後最後確認，不是新實作／調參或對同一失敗輸出反覆抽樣。原有數值 gates 保留（總 GM 的 0.11% 容許沿用協調者備註 2）；沒有放寬超時限制。舊 final_b 全部封存，不沿用任何 marker。queue 已改為檢查 SA 保留點與其測試，不再要求已撤回 deadline 的 fixtures。
- 本 session 直接重跑最後來源一般／ASan+UBSan／TSan，結果與新 production prepare 狀態記在下方；ASan 使用 `ASAN_OPTIONS=detect_leaks=0`，完整 LeakSanitizer 交外部 queue。全部長計算由協調者執行，未在沙箱啟動。

- 最後來源 C++ 已直接完成：一般、ASan+UBSan、TSan **各 10/10 PASS、28 records**；wrapper wall（含可能的鎖等待）41.25／153.63／69.83s，所有 returncode=0、sanitizer 0 診斷、TSan 0 警告。ASan 確實使用 `ASAN_OPTIONS=detect_leaks=0`，不能宣稱 LeakSanitizer 已檢查。逐檔 logs、src/tests hashes 位於 `phase2/final_b_validation/test_evidence/verification.json`；SA 的新功能測試 parallel_sa_test 保留並通過。
- 短測由雙 timing lock 串行。發現短測 wrapper 在等鎖前讀取 manifest，可能覆蓋前一套的 metadata；已修正為完成時重讀合併。本輪保留了 sanitizer 完成時 manifest，三套均已依原始 records／逐檔 log SHA、src/tests SHA 合併重驗，沒有重跑測試或補造成功資料。
- **等待中（最後檢查，不再調參）**：協調者從 Lab3 執行 `python3 Testcases/results/CodexParallelChain/scripts/phase2_final_b_queue.py`；WAIT：`Testcases/results/CodexParallelChain/phase2/final_b_validation/ALL_DONE`。queue 依序同輪 train、L1/L2 完整執行、27 筆合法性／deadline 回歸、沙箱外 `ASAN_OPTIONS=detect_leaks=1` 完整 ASan+UBSan；計時不綁核，僅編譯沿用 v6q／runner 的 CPU 16–19。
- **目前不能寫 B 完成／B_DONE**：最後來源還沒有這次 RUN 的結果。下一次只讀結果與核對 hashes，不開新一輪實作／調參；ALL_DONE 僅表示跑完，必須逐項讀 train/large/regression gates 與完整 ASan 證據。全部成立才寫「B 完成，下一步 C」及 B_DONE；若有門檻未過，照實列出（包含已知 v6 L2 限制），不可將備註 3 的舊收尾例外冒稱本次使用者要求的全部通過。未 commit、未凍結、未產生新 seed、未進 C。
- 最終預檢完成：新 train 的 candidate/v6 與 large candidate production binaries 均已編譯，validate() 通過；兩份 protocol.src_sha256 都等於最後 SA 保留點 `4e59ed648a9cfb4f859e2c8cde8dca84f8e38571db5c0f94508232d2f2613258`。三套測試逐檔 hashes 再核對成功，保護目錄 hashes 未變，沒有舊 ALL_DONE 或本 session 啟動的背景工作。證據 `phase2/final_b_validation/setup_verified.json`，編譯日誌 `{train,large}_prepare.log`。最終狀態仍為等待上述 RUN／WAIT，不宣告 B_DONE。
- 10-23 17:45 phase2/train/final_b completed; all_gates=True
- 10-23 17:56 phase2/large/final_b completed; all_gates=True

## B 完成（協調者判定，10-23 18:02）
- B10 留下的最後驗收已由協調者執行：`phase2/final_b_validation/ALL_DONE`（src_sha256 4e59ed648a9cfb4f859e2c8cde8dca84f8e38571db5c0f94508232d2f2613258，等於目前 src）。
- gates：train **true**（雙條件 8/11、總位移 GM 在變動內、最大 GM 與逐筆最大都不比 v6 差、0 超時）、large **true**（L1 187.5 秒、L2 210.9 / 210.9 秒，全部合法，L2 額外步驟 5.06 / 5.06 秒、L1 5.72 秒，0 超時）、27 筆 deadline 回歸 **true**、沙箱外完整 ASan/UBSan（detect_leaks=1）**true**。
- 保留：平行 SA band 分界四相循環（同次數、自動執行緒、3 seeds HPWL GM −0.825%，固定次數逐位相同，TSan 0 警告）。
- 撤回：target windows 平行化、ejection 縮小候選、rowRepair 期限修正與修復預算調整（數據見前面各節）。
- 已知限制：先前一次 L2 額外步驟 6.13 秒（> 6 秒）在 v6 原碼也出現，屬執行間變動／v6 既有問題；本次驗收 L2 為 5.06 秒。
- **B 完成，下一步 C**（凍結 + final_validate.py）。

## 階段 C：凍結與最終驗證交接（2026-10-23）

- 已確認「B 完成，下一步 C」及第 7 節最後檢查完整紀錄。保留 SA 四相邊界，其餘改動撤回；不是全撤回，因此進行凍結。
- 最後 train/large final_b protocol、驗收 ALL_DONE、27 筆回歸與短測 verification 的 src_sha256 均等於目前 src：`4e59ed648a9cfb4f859e2c8cde8dca84f8e38571db5c0f94508232d2f2613258`；目前 tests 逐檔 hash 與 verification 相符。
- 最後 Stage 1：雙條件 8/11、總位移 GM 1.0001009443594198、最大 GM 0.9999978736976397；全部 gates=true、額外步驟 0 超時。large 全合法且 L1<240s、L2<600s、0 額外超時；27 筆回歸全部合法且 0 額外超時。證據：`phase2/{train,large}/final_b/summary.json`、`phase2/deadline_regression/final_b/ALL_DONE`。
- 一般／沙箱 ASan+UBSan／TSan 各 10/10、28 records，新功能 parallel_sa_test 已有紀錄，TSan 0 警告；沙箱 ASan 使用 detect_leaks=0。協調者沙箱外完整 ASan/UBSan 補跑：`phase2/asan_outside.log` 最後幾行為 100% tests passed、0/10 failed，測試 31.71 秒；configure、build、ctest 均 rc=0。log SHA256：`9da7a8f19860ea9db6b5418781ec80d955328231f6aec3cc5eb4ad44f0b0a127`；完整 detect_leaks=1 通過亦記於既有 B 完成紀錄及驗收 ALL_DONE。
- 已寫 `Testcases/results/CodexParallelChain/frozen_sources.sha256` 與 `freeze.json`；manifest 包含 src、tests 全部檔案及 CMakeLists.txt、Makefile，共 53 檔，路徑相對於 AI_Solutions/CodexParallelChain，格式為 sha256 + 兩空白 + 相對路徑。manifest SHA256：`2960fb38797b9262133b83c9ba3ebe1c97e00c6790dc13abf5ae4b3e1cbfcf35`。
- 已確認 `Testcases/edge_seed6` 不存在（亦非 symlink），本 session 未產生 seed、未啟動長計算、未 commit。
- RUN: python3 Testcases/results/CodexParallelChain/scripts/final_validate.py --seed-dir edge_seed6 --seed 20261024
- WAIT: Testcases/results/CodexParallelChain/final/ALL_DONE
- 等待中：final_validate.py，完成標記 final/ALL_DONE；之後跑 final_report.py
- 10-23 18:15 final/lg_CodexParallelChain 完成（631 秒）
- 10-23 18:24 final/edge_CodexParallelChain 完成（584 秒）
- 10-23 18:35 final/lg_ClaudeOpus55_v6 完成（630 秒）
- 10-23 18:45 final/edge_ClaudeOpus55_v6 完成（587 秒）
- 10-23 18:55 final/lg_CodexChainDP 完成（648 秒）
- 10-23 19:05 final/edge_CodexChainDP 完成（569 秒）
- 10-23 19:08 final/lg_CodexParallelChain_L1 完成（203 秒）
- 10-23 19:12 final/lg_ClaudeOpus55_v6_L1 完成（204 秒）
- 10-23 19:16 final/lg_CodexParallelChain_L2 完成（242 秒）
- 10-23 19:20 final/lg_ClaudeOpus55_v6_L2 完成（242 秒）
- 10-23 19:20 final/edge_seed6 完成（15 秒）
- 10-23 19:28 final/seed_ClaudeOpus55_v4 完成（473 秒）
- 10-23 19:38 final/seed_CodexChainDP 完成（575 秒）
- 10-23 19:47 final/seed_ClaudeOpus55_v6 完成（589 秒）
- 10-23 19:57 final/seed_CodexParallelChain 完成（589 秒）
- 10-23 20:01 final/ts_ClaudeOpus55_v4 完成（227 秒）
- 10-23 20:07 final/ts_ClaudeOpus55_v6 完成（319 秒）
- 10-23 20:12 final/ts_CodexParallelChain 完成（320 秒）
- 10-23 20:26 final/hpwl_fixed 完成（812 秒）
- 10-23 20:59 final/hpwl_equal 完成（1999 秒）
- 10-23 22:35 final/hpwl_default 完成（5782 秒）
- 10-23 22:36 final/extra_testcases_CodexParallelChain 完成（66 秒）
- 10-23 22:38 final/extra_testcases_ClaudeOpus55_v6 完成（66 秒）
- 10-23 22:38 final_validate.py 全部完成（final/ALL_DONE）；下一步：python3 scripts/final_report.py

## 階段 C 收尾：最終報告與文件完成（2026-10-23）

- final_report.py 已完成並產生 final/summary.md；未重跑計時、測試或開背景工作。HANDOFF 僅讀第 0、6 節。
- README 完成診斷、保留改動、撤回方案、驗收限制及既有／新 seed 全部未達標表；量測數字全部取自 summary。COMPARISON 只追加第 12 節；HISTORY 表 1、2、5（兩張子表）、6 只加列；make_table.py 增加直接指向本輪 edge record.csv 的版本並重建 tables.md。
- 判斷：summary 無主要 11 筆 ÷v1 與秒數，因此 HISTORY 表 1 該列填 —，不拿 27 筆結果混用。summary 無逐筆最大對 v6 的 Stage 1 變動界線，該驗收標為未證實；L1/L2 額外步驟與測試僅引用既有 WORK_STATE 的 B 通過紀錄，不冒充本次重測。
- 結果：27/27、新 seed 16/16、L1/L2 合法；雙條件 20/27 ≥19、新 seed 11/16；27 筆額外步驟 0 超時；HPWL equal/default 的 holdout/edge 四組平均全部不差於同輪 v6，fixed 兩組相同。逐筆失敗差距完整存 README 與 COMPARISON。
- 保護目錄 7 個、242 檔與目前 protected_before.json 完全一致；此為協調者已更新的基準，並非宣稱與最初舊基準相同。orchestrator_note.json 所載 HANDOFF/NEXT_CODEX_PROMPTS/WORK_STATE 文件變動已納入脈絡，沒有新增例外或覆寫基準。凍結 src/tests/建置檔共 53 檔 SHA256 全部相符。
- COMPARISON 原檔 SHA256：2187435dae2a0a563a053e7c3e1a1e24fa36a41f2660c693272696de4503b911；追加後原長度前段逐 byte 相符。HISTORY 移除新增列後逐字等於原檔。證據與原檔備份：provenance/report_closeout/。
- B 已由協調者驗收完成，C 報告收尾完成；仍保留上述未達標／未證實條目，不宣稱所有品質門檻通過。不 commit、不新增署名；沒有 RUN/WAIT 待辦。
