# ClaudeOpus55_v6 工作狀態

以 CodexCapDP 為底（src/、CMakeLists.txt、Makefile、tests/ 複製時逐檔 sha256 相同：`Testcases/results/ClaudeOpus55_v6/base_CodexCapDP.sha256`）。
六個保護資料夾的 hash：`Testcases/results/ClaudeOpus55_v6/protected_sources.sha256`（186 檔）。

## 規則（摘要）
- 不 commit、不加署名；結果全部在 `Testcases/results/ClaudeOpus55_v6/`。
- 調參只用 testcases + holdout；edge / edge_seed2~4 只當回歸；凍結後才產生 edge_seed5（seed 20261023）。
- 計時程式一次一個（`timing.lock`）；分析工作 `taskset -c 16-19`；> 2 分鐘的放背景。
- 實驗腳本：`results/ClaudeOpus55_v6/scripts/`（`v6q.py` = 共用的串行佇列，每步 `.done.json`，重跑會跳過已完成的步驟）。

## 第一階段：診斷 CodexCapDP（完成 2026-10-22 05:40）
- 報告：`Testcases/results/ClaudeOpus55_v6/diagnosis.md`（排序表在最前面、第 6 節是第二階段計畫）。
- L2 4 執行緒（phase1/exp3）：HPWL −0.83% / −0.73%（2 seeds），attempts +29%。
- 2026-10-22 建立 v6 副本、記錄 hash；寫 `scripts/phase1_runs.py`（三版本 local_grade 27 + L1 + L2、run_ts Stage 1 ×(3+1+1)、ts L2）。

- 背景佇列（依序拿 timing.lock）：`phase1_runs.py` → `phase1_exp.py`（L2 共同起點排程、holdout 預設 HPWL 配對、chain 時間掃描、e07×3）／`phase1_exp2.py`（cap 搜尋：比例 1.01 / extra 20 秒 / 兩者）。
  重開機或中斷後：直接重跑同一支腳本，已完成的步驟（`.done.json`）會跳過。
- 分析（E-core）：`scripts/cap_headroom.py`（熱點 ILP：只動違規 cell 周圍、每顆最多移 R 列 / S site，上限 D 下的最小總位移）。
  h04 第一次試「上限內所有位置」= 300 萬欄，跑不動；改成每顆只在原位附近移動。結果寫 `phase1/headroom/`。
- TSan：`phase1/tsan/`，parallel_sa_test 0 個警告、PASS；sa_bench h07 8 執行緒在跑。
- 從 CodexCapDP 既有 stderr 已看到（待本次重跑確認）：e03/h04/h07 的 cap 候選**全部可行**，被拒絕的都是內部總位移比例 1.0048；
  ChainDP 的比例是 1.01（相對 global 之前），而且每個候選給 DP 3.2 秒（CapDP 0.3~0.8 秒）。e08/e16 的總位移超出 = chain 較短少拿的 ~0.55% + cap 搜尋花掉的 0.43~0.47%（最大位移已比 ChainDP 低 3.7~4.1%）。

## 第二階段（2026-10-22 凍結 06:40）
- 最終改動（相對 CodexCapDP）：rowRepair 每步看時間；polish 超過 3 pass 時只在上一 pass 仍降 ≥1% 才繼續、放不下的 pass 不開始；
  平行 SA「餓死」規則（第 0 步量到每 cell < 100 次 → 只留 10% 時間，其餘給 polish）取代 CapDP 的 ≥50 萬 cell 寫死 30/60；
  大設計 SA 執行緒下限 4、上限 12e6/n。撤回：rows/協商雙修復（race1 無效果）；不採用：視窗 DP 16 執行緒、chain 依收穫延長（訓練集無觸發）。
- 凍結 manifest：`results/ClaudeOpus55_v6/frozen_sources.sha256`（52 檔），manifest sha256 a5d9adcfaec931471fcfd97ffce875d8de2d2e8306bc61e7ecbd2bc2fc04e1b8（freeze.json）。凍結時 edge_seed5 尚不存在。
- 最終驗證 `scripts/phase2_final.py` 背景執行（final/*.done.json；中斷就重跑同一支，會跳過已完成步驟；它每步都核對凍結 hash）。
  順序：四版本 testcases+holdout、edge → L1/L2（v6/CapDP/v5）→ 產生 edge_seed5 → 五版本 seed5 → ts（v4/CapDP/v6）→ HPWL fixed/equal/default。
- 之後：彙整（final summary）、README、COMPARISON 第 11 節、edge/make_table.py 加 v6 重建表。

## 第二階段計畫（原本）
依 diagnosis.md 第 6 節：(1) 大設計 Stage 2：polish 依收穫/時間不限 3 pass、SA/polish 依速率分配、大設計 SA 執行緒放寬；
(2) rowRepair 等修復依每步成本檢查時間；(3) chain 依收穫決定時間；(4) rows/協商雙修復短 DP 比較。不改內部比例、不做熱點 MIP。
調參只用 testcases + holdout（L2 用來量大設計排程）；edge 系列只回歸。

## 進度紀錄（腳本自動追加）
- 10-22 01:56 phase1/lg/CodexCapDP_27 完成（1230 秒）
- 10-22 02:00 TSan：sa_bench（CodexCapDP SA 原始碼）h07、--threads 8、固定 300 萬次排程（實做 23,993,852 次，230 秒）rc=0，ThreadSanitizer 警告 0；log 在 phase1/tsan/sa_bench_h07_t8.log
- 10-22 02:13 熱點 ILP h04（CapDP 最終解，上限 231.76，150 顆違規）：±1 列 ±8 site（881 顆）**證明不可行**（180 秒）；±1/±16（1029 顆）、±2/±16（1295 顆）120 秒內 UNKNOWN。背景再跑 e03@129.829、e12@228.259、h04 長時間版（phase1/headroom/）
- 10-22 02:17 phase1/lg/ClaudeOpus55_v5_27 完成（1250 秒）
- 10-22 02:39 phase1/lg/CodexChainDP_27 完成（1276 秒）
- 10-22 02:39 配對 27 筆（同一段時間，參考=本次 ChainDP）：CapDP 27/27 PASS、雙條件 20/27（h04/h06/h07/e03/e12 最大、e08/e16 總）、最大 GM 0.98531、總 0.99961；v5 雙條件 15/27。表：results/ClaudeOpus55_v6/phase1/lg27_vs_chain.md
- 10-22 02:42 phase1/lg/CodexCapDP_L1 完成（210 秒）
- 10-22 02:46 phase1/lg/ClaudeOpus55_v5_L1 完成（207 秒）
- 10-22 02:49 phase1/lg/CodexChainDP_L1 完成（217 秒）
- 10-22 02:53 phase1/lg/CodexCapDP_L2 完成（244 秒）
- 10-22 02:57 phase1/lg/ClaudeOpus55_v5_L2 完成（253 秒）
- 10-22 02:58 大設計配對（單次 seed 1）：L1 CapDP 187.7 秒 HPWL 1.73835e9 / v5 186.9 秒 1.73831e9 / ChainDP 194.4 秒 1.83538e9；L2 CapDP 211.7 秒 **1.62387e9** / v5 216.4 秒 1.67265e9（CapDP −2.92%）。CodexCapDP 報告的 L2 +3.3% 是對歷史 v5，配對重跑沒有重現。
- 10-22 03:02 phase1/lg/CodexChainDP_L2 完成（259 秒）
- 10-22 03:08 phase1/ts/CodexCapDP_s1_r1 完成（392 秒）
- 10-22 03:11 熱點 ILP（CapDP 最終解為起點，上限 = ChainDP 最大）：e03@129.829 違規 96 顆，±2 列 ±16 site 內有違規 cell **根本沒有 ≤ D 的位置**；e12@228.259 ±1/±8（633 顆）、±1/±16（742 顆）**證明不可行**，±2/±16 790 秒 UNKNOWN；h04 ±1/±16（1029 顆）**證明不可行**（876 秒），±2/±16 1265 秒 UNKNOWN。結論：要到 ChainDP 的最大位移，不能只在熱點附近小幅移動（ChainDP 的解是大範圍重排），精確子問題在 6 秒內不可行。
- 10-22 03:15 phase1/ts/CodexCapDP_s1_r2 完成（391 秒）
- 10-22 03:21 phase1/ts/CodexCapDP_s1_r3 完成（387 秒）
- 10-22 03:21 Stage 1 變動（CapDP 同 binary 4 次 = ts r1~r3 + lg）：27 筆中 19 筆完全相同；最大位移變動只有 e11 12.486~13.315（6.6%）、ibm05 0.13%；總位移最大範圍 0.11%（h08、e11）。表：phase1/stage1_variation.csv
- 10-22 03:28 phase1/ts/ClaudeOpus55_v5_s1_r1 完成（411 秒）
- 10-22 03:37 phase1/ts/CodexChainDP_s1_r1 完成（528 秒）
- 10-22 03:37 Stage 1 階段（本次 ts，27 筆合計）：CapDP r1 390.9 秒 = 基本 255.5 + chain 30.0 + cap 104.7；v5 410.0 秒；ChainDP 526.8 秒（global 196.3、chain 67.5）。額外步驟超時：見 phase1/ts_capdp_phases.csv extra_over。
- 10-22 03:40 phase1/ts/CodexCapDP_L2 完成（212 秒）
- 10-22 03:41 L2 CapDP ts：SA 2 執行緒 30.1 秒、17.1M 次（計畫 1.2 億）、62.5% proposal 失敗、最後一步接受率仍 21.8%、窗 28×24（最小）；SA 只把 HPWL 2.642e9→2.367e9（−10.4%），polish 3 個 pass 2.367→1.936→1.747→1.629e9（每 pass 16~18 秒，−18%/−10%/−7%），**polish 停在 polishPasses=3 上限而不是時間**，還在大幅下降。
- 10-22 03:44 phase1/ts/ClaudeOpus55_v5_L2 完成（218 秒）
- 10-22 03:44 phase1_runs.py 全部完成（lg 27+L1+L2 三版本、ts Stage 1、ts L2）
- 10-22 03:44 L2 v5 ts（SA60/P30）：SA 2 執行緒 60.2 秒 35.8M 次、61.8% 失敗，2.642→2.152e9（−18.5%）；polish 2 pass（時間到）→1.675e9。對照 CapDP（SA30/P60）SA 後 2.367e9、polish 3 pass→1.627e9。SA 第 2 個 30 秒拿 −0.215e9（7.2M/秒），polish 第 3 pass −0.118e9/16 秒（7.4M/秒）：邊際效益相近。phase1_runs.py 全部完成，phase1_exp.py 開始。
- 10-22 03:44 exp/l2_CodexCapDP_sa60_p30_s1 FAILED rc=1，看 l2_CodexCapDP_sa60_p30_s1.log
- 10-22 03:44 exp/l2_CodexCapDP_sa30_p60_s1 FAILED rc=1，看 l2_CodexCapDP_sa30_p60_s1.log
- 10-22 03:44 exp/l2_CodexCapDP_sa60_p30_s2 FAILED rc=1，看 l2_CodexCapDP_sa60_p30_s2.log
- 10-22 03:45 exp/l2_CodexCapDP_sa30_p60_s2 FAILED rc=1，看 l2_CodexCapDP_sa30_p60_s2.log
- 10-22 03:45 exp/l2_CodexCapDP_sa60_p30_s3 FAILED rc=1，看 l2_CodexCapDP_sa60_p30_s3.log
- 10-22 03:45 phase1_exp.py 的 L2 步驟因前綴名不是 L2（aux 內寫 L2.nodes）全部失敗；已刪除失敗紀錄、修正、重新排隊（exp2 先拿到 lock）。
- 10-22 03:54 phase1_exp2.py 完成（cap 搜尋：內部比例 / 時間 / 兩者）
- 03:58 exp2 結果（CapDP Stage 1，base / ratio=1.01 / time=extra 20 / both；比對本次 ChainDP；表 phase1/exp2_all_*.csv）：
  e03 只放寬比例就達標（129.63 ≤ 129.83，總 0.9991）＝**預算限制**；h07 要兩者（46.89）；h04 兩者都給仍 234.66 > 231.76、e12 兩者 230.0 > 228.26 ＝**搜尋 / 修復品質**；
  比例放寬讓 h03（總 1.0095）、e08（1.0165）、e16（1.0112）總位移失敗；給時間讓 e08 89866.8（1.00505，差一點）、e16 達標（1.00448）＝**chain/DP 時間**。
  已接受步驟的邊際斜率（Δ總%/Δ最大%）在會失敗與會成功的筆之間沒有分界（e03 0.05~0.15、h03 0.14~0.20、e08 0.04~0.22），單純改接受規則無法同時滿足。
- 10-22 03:56 phase1/exp/l2_CodexCapDP_sa60_p30_s1 完成（103 秒）
- 10-22 03:58 phase1/exp/l2_CodexCapDP_sa30_p60_s1 完成（100 秒）
- 10-22 03:59 phase1/exp/l2_CodexCapDP_sa60_p30_s2 完成（103 秒）
- 10-22 04:01 phase1/exp/l2_CodexCapDP_sa30_p60_s2 完成（99 秒）
- 10-22 04:03 phase1/exp/l2_CodexCapDP_sa60_p30_s3 完成（103 秒）
- 10-22 04:04 phase1/exp/l2_CodexCapDP_sa30_p60_s3 完成（99 秒）
- 10-22 04:06 phase1/exp/l2_ClaudeOpus55_v5_sa60_p30_s1 完成（103 秒）
- 10-22 04:08 phase1/exp/l2_ClaudeOpus55_v5_sa30_p60_s1 完成（99 秒）
- 10-22 04:09 phase1/exp/l2_ClaudeOpus55_v5_sa60_p30_s2 完成（103 秒）
- 10-22 04:11 phase1/exp/l2_ClaudeOpus55_v5_sa30_p60_s2 完成（99 秒）
- 10-22 04:13 phase1/exp/l2_ClaudeOpus55_v5_sa60_p30_s3 完成（103 秒）
- 10-22 04:14 phase1/exp/l2_ClaudeOpus55_v5_sa30_p60_s3 完成（99 秒）
- 10-22 04:28 L2 排程（共同起點 ×2、3 seeds，phase1/l2_sched.md）：SA30→P60 ÷ SA60→P30 = 0.9683（v5 起點，0.9679–0.9689）/ 0.9678（CapDP 起點，0.9670–0.9694），6/6 都好約 3.2%，Stage 2 少 3.5 秒；起點差異（CapDP ÷ v5）只有 +0.31~0.37%。所以 CodexCapDP 的 L2 +3.3% 是對歷史數字比較的雜訊，排程本身是改善。
- 10-22 04:38 phase1/exp/hpwl_default_holdout 完成（1433 秒）
- 10-22 04:38 holdout 預設 HPWL 配對（本次 Stage 1、3 seeds，phase1/exp/hpwl_default_holdout）：CapDP ÷ v5 = 0.99935（各 seed 0.99616 / 1.00239 / 0.99949）；單筆 seed 間變動到 ±1.5%（h08 0.987~1.015）。歷史的 0.9924 vs 0.9919 差 0.05% 在雜訊內，配對沒有變差。
- 10-22 04:49 phase1/exp/e07_r1 完成（7 秒）
- 10-22 04:50 phase1/exp/e07_r2 完成（7 秒）
- 10-22 04:50 phase1/exp/e07_r3 完成（7 秒）
- 10-22 04:50 phase1_exp.py 全部完成（L2 排程、holdout 預設 HPWL 配對、chain 時間掃描、e07 重複）
- 10-22 04:51 e07 超時原因（phase1/e07_diag，CapDP 的插樁副本）：rowRepair phase 1 每 256 步才看一次時間，e07 兩條 row 各約 1360 顆，每步 2~7 ms → 256 步 0.56~1.83 秒，超過 phase1Until 0.32~1.70 秒；成功那次 phase 2/擺放也花 0.5 秒（同樣 256 步才檢查）。capWindows、repairCap 沒有超時。3 次重跑超出 0.177~0.224 秒，可重現。
- 10-22 04:54 ChainDP 子方法（phase1/chain_diag，ChainDP 插樁副本，--no-sa）：e03/e12/h04/h07/h06 所有可行的 global 候選**全部來自 site 擁擠協商**（pass 25~468），ordered/construct/packing 一次都沒成功。CapDP 只在 rowRepair 失敗時才用協商、且只給 ≤0.4 秒。
- 10-22 04:57 repair_compare 完成（rows vs 協商，phase1/repair_compare/）
- 05:20 repair_compare（phase1/repair_compare/，CapDP 本次最終解為起點，D = max×(1−1/2/3%)，修復 2 秒、DP 累計 0.5/1.5/4 秒）：
  協商（ChainDP repairCap）只在降 1% 時成功、而且很快（0.01~0.07 秒）；降 2~3% 多半 0.05~0.8 秒內放棄。
  降 1% 時協商 DP 後總位移比 rows 低：h04 +0.284% vs +0.710%、e12 +0.595% vs +0.929%；但 e03（+0.164 vs +0.125）、h07（+0.818 vs +0.434）、h03（+8.1 vs +0.42）較差。
  raw 不能預測 DP 後誰好（h04 協商 raw 較高但 DP 後較低）；0.5 秒 DP 後的排序在 h04/h07/e03 已與最後一致。
  h06 DP 時間很重要：rows 降 1% 0.5 秒 +3.1% → 3.1 秒 +0.28%。
- 10-22 04:59 phase1/exp3/l2_t2_s1 完成（100 秒）
- 10-22 05:00 phase1/exp3/l2_t4_s1 完成（99 秒）
- 10-22 05:02 phase1/exp3/l2_t2_s2 完成（99 秒）
- 10-22 05:04 phase1/exp3/l2_t4_s2 完成（99 秒）
- 10-22 05:04 phase1_exp3.py 完成（L2 SA 執行緒 2 vs 4）
- 10-22 05:09 第二階段：(2) rowRepair 每步檢查時間 → e07 extra 6.21→4.10 秒（max 相同、總位移 19777→19772）。(1) polish 不限 3 pass（收斂或下一 pass 放不下就停）、大設計 SA 至少 4 執行緒；L1/L2 SA/polish 分配掃描在跑（phase2/stage2_sweep）。(3) chain 依收穫：訓練集（testcases+holdout）第 1 輪最大收穫 0.34%（h06），e08/e16 1.26/0.82%，訓練集沒有會觸發的筆、無法不看 edge 驗證，而且 chain 給滿 6 秒 e08 也不夠 → **不做**。
- 10-22 05:10 phase2/stage2_sweep/L2_base_sa30_s1 完成（100 秒）
- 10-22 05:11 phase2/stage2_sweep/L2_v6a_sa15_s1 完成（100 秒）
- 10-22 05:13 phase2/stage2_sweep/L2_v6a_sa30_s1 完成（99 秒）
- 10-22 05:15 phase2/stage2_sweep/L2_v6a_sa45_s1 完成（99 秒）
- 10-22 05:16 phase2/stage2_sweep/L2_v6a_sa60_s1 完成（99 秒）
- 10-22 05:18 phase2/stage2_sweep/L2_base_sa30_s2 完成（99 秒）
- 10-22 05:20 phase2/stage2_sweep/L2_v6a_sa15_s2 完成（99 秒）
- 10-22 05:21 phase2/stage2_sweep/L2_v6a_sa30_s2 完成（99 秒）
- 10-22 05:23 phase2/stage2_sweep/L2_v6a_sa45_s2 完成（99 秒）
- 10-22 05:25 phase2/stage2_sweep/L2_v6a_sa60_s2 完成（99 秒）
- 10-22 05:26 phase2/stage2_sweep/L1_base_sa60_s1 完成（83 秒）
- 10-22 05:27 phase2/stage2_sweep/L1_v6a_sa15_s1 完成（71 秒）
- 10-22 05:29 phase2/stage2_sweep/L1_v6a_sa30_s1 完成（79 秒）
- 10-22 05:30 phase2/stage2_sweep/L1_v6a_sa45_s1 完成（84 秒）
- 10-22 05:31 phase2/stage2_sweep/L1_v6a_sa60_s1 完成（91 秒）
- 10-22 05:33 phase2/stage2_sweep/L1_base_sa60_s2 完成（83 秒）
- 10-22 05:34 phase2/stage2_sweep/L1_v6a_sa15_s2 完成（71 秒）
- 10-22 05:35 phase2/stage2_sweep/L1_v6a_sa30_s2 完成（84 秒）
- 10-22 05:37 phase2/stage2_sweep/L1_v6a_sa45_s2 完成（84 秒）
- 10-22 05:38 phase2/stage2_sweep/L1_v6a_sa60_s2 完成（91 秒）
- 10-22 05:38 phase2_stage2_sweep.py v6a 完成
- 10-22 05:43 phase2/train/race1/stage1 完成（263 秒）
- 10-22 05:43 phase2/train/race1 完成（預設）：見 phase2/train/race1/compare.md
- 10-22 05:43 race1（rows/協商雙修復短 DP 比較）訓練集 8/11、11 筆最大位移與 CapDP 完全相同；11 筆共只發生 9 次 race、協商贏 2 次（h04、ibm01，都沒變成最後結果）→ **沒有效果，撤回**（資料 phase2/train/race1）。下一步：Stage 1 視窗 DP 16 執行緒 vs 8（phase2/train/thr16）。
- 10-22 05:44 phase2/stage2_sweep/L2_v6b_sa0_s1 完成（100 秒）
- 10-22 05:46 phase2/stage2_sweep/L2_v6b_sa7_s1 完成（99 秒）
- 10-22 05:48 phase2/stage2_sweep/L2_v6b_sa15_s1 完成（99 秒）
- 10-22 05:49 phase2/stage2_sweep/L2_v6b_sa0_s2 完成（100 秒）
- 10-22 05:51 phase2/stage2_sweep/L2_v6b_sa7_s2 完成（99 秒）
- 10-22 05:53 phase2/stage2_sweep/L2_v6b_sa15_s2 完成（99 秒）
- 10-22 05:54 phase2/stage2_sweep/L1_v6b_sa60_s1 完成（83 秒）
- 10-22 05:55 phase2/stage2_sweep/L1_v6b_sa60_s2 完成（83 秒）
- 10-22 05:55 phase2_stage2_sweep.py v6b 完成
- 10-22 06:00 phase2/train/thr16/stage1 完成（255 秒）
- 10-22 06:00 phase2/train/thr16 完成（--threads 16）：見 phase2/train/thr16/compare.md
- 06:10 Stage 2 掃描（phase2/stage2_sweep，同一 L1/L2 起點、sa_bench、2 seeds）：
  L2 SA 0/7/15/30/45/60 秒（其餘 polish）→ 1.545/1.546/1.569/1.616/1.693/1.802e9；CapDP 預設 SA30→P60 1.627e9。L1 SA 15/30/45/60 → 2.17/1.90/1.79/1.736e9，60 秒最好。
  L1 v6 比 CapDP sa_bench 高 0.35%（4 次），但固定 2000 萬次時兩者 HPWL 逐位相同（phase2/l1_det）→ 是計時雜訊，不是程式差異。
  → SA「餓死」規則改成門檻：第 0 步量到的每 cell 次數 < 100 就只留 10% 時間、其餘給 polish（L2 ≈ 45 → 6 秒；L1 ≈ 375 不動）。CapDP 依 cell 數的 30/60 改回 60/30 + 規則。
- thr16（視窗 DP 16 執行緒，phase2/train/thr16）：11 筆最大位移與 8 執行緒相同 → 不採用。
- phase2/large/starve1：完整 dp L2 ×2 seeds + L1，背景在跑。
- 10-22 06:07 phase2/large/starve1/L2_s1 完成（199 秒）
- 10-22 06:11 phase2/large/starve1/L2_s2 完成（213 秒）
- 10-22 06:14 phase2/large/starve1/L1_s1 完成（188 秒）
- 10-22 06:14 phase2/large/starve1 完成
- 10-22 06:19 測試（v6 最終原始碼）：一般 10/10、ASan+UBSan 10/10（phase2/tests/）；TSan parallel_sa_test（含 starved）PASS、sa_bench h07 8 執行緒 + polish 2 秒 rc=0、警告 0（phase2/tsan/）；固定 500 萬次、8 執行緒、seed 1 跑兩次 placement 逐位相同（phase2/determinism/）。annealer 的 8e6/n 上限改 12e6/n（L2 才會真的拿到 4 執行緒）。
- 10-22 06:31 final/lg_ClaudeOpus55_v6 完成（632 秒）
- 10-22 06:41 final/edge_ClaudeOpus55_v6 完成（589 秒）
- 10-22 06:51 final/lg_CodexCapDP 完成（631 秒）
- 10-22 07:01 final/edge_CodexCapDP 完成（591 秒）
- 10-22 07:11 final/lg_CodexChainDP 完成（627 秒）
- 10-22 07:21 final/edge_CodexChainDP 完成（589 秒）
- 10-22 07:32 final/lg_ClaudeOpus55_v5 完成（645 秒）
- 10-22 07:42 final/edge_ClaudeOpus55_v5 完成（596 秒）
- 10-22 07:45 final/lg_ClaudeOpus55_v6_L1 完成（204 秒）
- 10-22 07:49 final/lg_CodexCapDP_L1 完成（203 秒）
- 10-22 07:52 final/lg_ClaudeOpus55_v5_L1 完成（202 秒）
- 10-22 07:56 final/lg_ClaudeOpus55_v6_L2 完成（243 秒）
- 10-22 08:00 final/lg_CodexCapDP_L2 完成（243 秒）
- 10-22 08:04 final/lg_ClaudeOpus55_v5_L2 完成（246 秒）
- 10-22 08:04 final/edge_seed5 完成（15 秒）
- 10-22 08:12 final/seed5_ClaudeOpus55_v4 完成（462 秒）
- 10-22 08:22 final/seed5_CodexChainDP 完成（600 秒）
- 10-22 08:32 final/seed5_ClaudeOpus55_v5 完成（608 秒）
- 10-22 08:42 final/seed5_CodexCapDP 完成（600 秒）
- 10-22 08:52 final/seed5_ClaudeOpus55_v6 完成（600 秒）
- 10-22 08:56 final/ts_ClaudeOpus55_v4 完成（227 秒）
- 10-22 09:02 final/ts_CodexCapDP 完成（323 秒）
- 10-22 09:07 final/ts_ClaudeOpus55_v6 完成（320 秒）
- 10-22 09:07 最終：27 筆配對（參考同輪 ChainDP）v6 雙條件 19/27、CapDP 20/27（差別 e15：v6 最大 4470.19 −8.6%、總 +0.572%，重跑相同）；L2 v6 1.5466e9 ÷ v5 0.9246、CapDP 0.9718；L1 v6 ÷ v5 1.0049（單次）；edge_seed5（凍結後 08:04 產生）v6 10/16、CapDP 10/16、v5 11/16、v4 6/16；額外步驟 v6 0/24 超時、CapDP e07 +0.299。HPWL 三協定在跑。
- 10-22 09:21 final/hpwl_fixed 完成（816 秒）
- 10-22 09:54 final/hpwl_equal 完成（2019 秒）
- 10-22 11:32 final/hpwl_default 完成（5830 秒）
- 10-22 11:32 phase2_final.py 全部完成
- 10-22 11:48 全部完成：HPWL 預設 v6÷CapDP 0.99984/0.99977（holdout/edge）、同總時間 1.00037/1.00284（edge 未達，對調順序重跑仍 1.0097，非順序效應）、同次數逐位相同；額外步驟 27/27 在預算內；README、COMPARISON 第 11 節（原 414 行未動）、edge 表已更新；保護資料夾與凍結原始碼 hash 核對不變。沒有 commit。
- 10-22 22:11 交接給下一版：寫 HANDOFF.md（規則、省 token、工具、現況、死路、方向）與 NEXT_CODEX_PROMPTS.md（A/A2/B/C/D 五段提示詞與排程）；scripts/ 改成可整包複製（路徑依資料夾名）、新增 final_validate.py / final_report.py（通用版）、codex_after.sh（等完成標記再開 codex exec）。預篩：band 邊界損失 h07 無、h08 +1.2%（phase2/band_loss/result.txt）。
- 10-23 00:12 協調者：A 在 Codex 沙箱內 nohup 的 diag.py 隨 session 結束而死；已在沙箱外重新啟動（拿掉計時 worker 的 taskset，說明在 Testcases/results/CodexParallelChain/provenance/orchestrator_note.json）。A2→B→C→D 的自動協調腳本 scripts/orchestrate_codex.py 與 codex_prompts/ 已備好；啟動被 Claude 的安全檢查擋下，需使用者自己執行。
