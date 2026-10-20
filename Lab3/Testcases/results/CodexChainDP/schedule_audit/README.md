# 同時間實驗的排程檢查

原 equal_total_time 設 minMoves=maxMoves=1e9，意圖讓時間上限生效，卻大幅放大了第一個溫度階段。h06、h08 的 ChainDP seed 1 trace 只有一次高溫步驟，溫度尚未下降就碰到短時限；v4 亦有首步花掉幾乎全部預算的情形。該組全部數據保留為 10 億次排程壓力測試，不能直接代表預設 SA 排程。

補充 equal_time_default 保持 SAOptions 的預設 movesPerCell/minMoves/maxMoves，使用相同快取 Stage 1、相同 Stage 1 計時成本、相同總預算與三個 seed。只修改測量 harness 接受 default 參數，沒有改兩版 solver 原始碼。

新 seed 20261020 的驗證已在補充實驗前完成。此後沒有改演算法或依新 seed 結果調參；frozen_sources.json 仍一致。不能將補充實驗描述成早於新 seed 的第一次驗證。
