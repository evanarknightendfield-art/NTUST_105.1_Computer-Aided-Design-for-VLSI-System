# 本輪完成狀態（2026-10-20）

實作、正式量測、文件與驗收檢查已完成；沒有仍在執行的實驗。沒有 commit。

- 預設 27 筆最大位移幾何平均比 v4 低 4.20%，總位移低 0.06%，逐筆最大位移沒有退步。
- HPWL 未達標：三 seed、共同時間上限、預設 SA 排程下，holdout 高 1.53%，edge 高 1.44%。固定次數結果與時間效率分開報告；嚴格同實際耗時比較尚未完成。
- 27 筆 + L1 + 新 seed 16 筆共 44/44 PASS；L1 196.36 秒。
- 新 seed 最大位移幾何平均低 5.60%，總位移低 0.31%；14 筆輸入改變，另 2 筆固定案例。
- 7 套 C++ 與 sanitizer 測試通過，最後的離散 L1 修正也重新通過 ASan/UBSan。
- 來源仍符合凍結雜湊，原 edge 可逐檔重生；舊結果已遷移至指定位置。

詳見 [README](README.md)、[最終驗收](../../Testcases/results/CodexChainDP/acceptance.json)、[所有彙總](../../Testcases/results/CodexChainDP/summary.json)。後續工作應承接未達標的 HPWL 與剩餘最優差距，不重跑已完成且來源未改變的實驗。任何新調參應回到 testcases / holdout；本輪 edge 與 edge_seed2 都已看過，不再是未知驗證集。
