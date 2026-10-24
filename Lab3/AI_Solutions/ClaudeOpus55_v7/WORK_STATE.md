# ClaudeOpus55_v7 — WORK_STATE

以 CodexParallelChain 為底（src、CMakeLists.txt、Makefile、tests 複製於 2026-10-23 23:30）。
所有執行輸出在 `Testcases/results/ClaudeOpus55_v7/`；本資料夾只放原始碼與說明。

## 開工紀錄（10-23 23:30）

- 保護資料夾 8 個（ClaudeOpus55、CodexWindowDP、ClaudeOpus55_v4、CodexChainDP、ClaudeOpus55_v5、CodexCapDP、ClaudeOpus55_v6、CodexParallelChain）
  共 297 檔的 sha256：`Testcases/results/ClaudeOpus55_v7/provenance/protected_before.sha256`；
  COMPARISON.md / HISTORY.md 原檔 sha256：`provenance/docs_before.sha256`。
- 機器：i7-12700，P-core = CPU 0–15（8 核 × 2 HT），E-core = 16–19。`std::thread::hardware_concurrency()` 在 taskset 下仍回報 20
  （glibc 2.43 讀的是 online CPU，不是 affinity），所以 taskset 限 2 核時程式仍開 8 執行緒（超額訂閱）。
- 第一階段用的插樁（預設行為不變，只加量測選項）：`--trace-dp`（每一輪視窗 DP 印總位移）、`--chain-split T E D`
  （chain 階段 target windows / ejection / post-DP 的時間比例，預設 0.3 0.75 0.5）、`[chain-phase]` 摘要行；
  `tests/sa_bench2.cpp` 加 `--starved A`。

## 第一階段：量測（進行中）

- 10-23 23:44 phase1/cores_all 完成（522 秒）
- 10-23 23:53 phase1/cores_P4 完成（531 秒）
- 10-24 00:02 phase1/cores_P2 完成（551 秒）
- 10-24 00:11 phase1/cores_P4t4 完成（529 秒）
- 10-24 00:20 phase1/cores_P2t2 完成（555 秒）
- 10-24 00:35 **少核心（第一階段 2）分析完**：`phase1/analysis/cores.md`。
  - 時間：L1 都是 187–188 秒、L2 199–211 秒、h08 124 → 168 秒（P2t2）；沒有超時。額外步驟：L2 在 P4t4 / P2t2 用 6.20 / 6.17 秒（**超出 6 秒預算 0.2 秒**），其餘 ≤ 6。
  - L1：Stage 1 卡在 90 秒，平行視窗 DP（final round 0，30 秒）在 8 核解 170 萬個視窗、2 核只解 42 萬 → 總位移 all 2.457e9 / P4 +4.0% / P2 +14.2% / P2t2 +17.7%；HPWL P2 +2.3%、P2t2 +1.3%。
  - L2：P4t4 / P2t2 的 HPWL +7.1% / +6.9%、總位移 +1.8%、最大 +2.5%。原因不是執行緒數：finish() 的 total-refine 一輪 10 秒不看時間，final round 0 在 100.05 秒才結束（all 是 99.28 秒），
    `oopt.until = stage1Time + 10 = 100` → **pareto 整段沒跑**（all 那次 pareto 一輪 13.5 秒拿到總位移 −1.7%、最大 −3.8%、legal HPWL −7%）。是牆鐘門檻的斷崖，TA 機器稍慢就會掉。
  - h08（不受 90 秒限制）：SA 次數 265M → 152M（P4）/ 92M（P2）/ 70M（P2t2），HPWL +1.5% / +3.0% / +4.3%。8 執行緒擠 2 核反而比 2 執行緒多做 30% 次數。
  - 大設計 Stage 1 各步驟每秒收穫（all，總位移 %/秒）：L2 row-opt / max-refine / total-refine / matching sweep 合計約 75 秒、0.004–0.016 %/秒；
    pareto 0.126 %/秒、視窗 DP 0.10 %/秒（只拿到 1 秒）。L1 視窗 DP 1.0 %/秒（2 核 0.56）、total-refine 0.26、max-refine 0.11（但它把最大位移 117,644 → 51,124）。
    total-refine 一輪在 L2 不看時間 10 秒（每 256 個 cell 才檢查一次，最差的 cell 搜尋範圍最大）。
- 下一步：等 lg_* / ts_CPC / dpcurve（完成標記 dpcurve.done.json），再分析配對與 DP 曲線。
- 10-24 00:30 phase1/lg_CodexParallelChain 完成（631 秒）
- 10-24 00:40 phase1/edge_CodexParallelChain 完成（587 秒）
- 10-24 00:51 phase1/lg_ClaudeOpus55_v6 完成（631 秒）
- 10-24 01:00 phase1/edge_ClaudeOpus55_v6 完成（587 秒）
- 10-24 01:11 phase1/lg_CodexChainDP 完成（641 秒）
- 10-24 01:21 phase1/edge_CodexChainDP 完成（584 秒）
- 10-24 01:24 phase1/lg_CodexParallelChain_L1 完成（203 秒）
- 10-24 01:28 phase1/lg_ClaudeOpus55_v6_L1 完成（204 秒）
- 10-24 01:31 phase1/lg_CodexChainDP_L1 完成（213 秒）
- 10-24 01:35 phase1/lg_CodexParallelChain_L2 完成（241 秒）
- 10-24 01:39 phase1/lg_ClaudeOpus55_v6_L2 完成（243 秒）
- 10-24 01:43 phase1/lg_CodexChainDP_L2 完成（253 秒）
- 10-24 01:50 phase1/ts_CPC 完成（384 秒）
- 10-24 01:56 phase1/dpcurve 完成（388 秒）
- 10-24 01:55 **配對重跑（第一階段 1）**：`phase1/analysis/lg27.md`、`large.md`。對同輪 ChainDP：CPC 雙條件 20/27、最大 GM 0.98595、總 GM 0.99944；
  v6 20/27、0.98452、0.99958。CPC 未達標：h04 +2.63%、h06 +2.33%、h07 +0.71%、e03 +6.14%、e12 +2.08%（最大）；e08 +1.10%、e16 +0.64%（總）。
  L1：CPC 187.3 秒 / v6 187.5 / ChainDP 198.9；L2：211.3 / 212.7 / 225.2，全部 PASS。CPC Stage 1 各階段（ts_CPC，27 筆）：legalize 223 秒、主視窗 DP 33、chain+cap 125、合計 383。
- **DP 收斂曲線（第一階段 3）**：`phase1/analysis/dpcurve.md`。finish+pareto+主 DP 合計 182 秒；若在「總位移 ≤ 最終 ×1.0001」就停只省 16.9 秒（訓練 11 筆 6.7 秒），
  主要大設計的尾巴並不低收穫（h06 最後 20% 時間 −2.7%，含 final max pass 後的回收）。真正的浪費：pareto 後的主視窗 DP（32.4 秒）用新的 memo，
  把 finish() 已經解過、沒變的視窗全部重解（e01 / e04 / e06 / h01 / h05 0 改善仍花 0.7–1.1 秒；h08 解 94k 個視窗）。共用 memo 結果不變、省時間。
- 10-24 02:00 phase1/chain_def 完成（217 秒）
- 10-24 02:03 phase1/chain_E0 完成（207 秒）
- 10-24 02:07 phase1/chain_E10 完成（210 秒）
- 10-24 02:10 phase1/chain_E25 完成（212 秒）
- 10-24 02:14 phase1/chain_T60E25 完成（212 秒）
- 10-24 02:17 phase1/chain_E25r4 完成（210 秒）
- 10-24 02:21 phase1/chain_x2 完成（229 秒）
- 10-24 02:23 phase1/maxvar_r1 完成（126 秒）
- 10-24 02:25 phase1/maxvar_r2 完成（126 秒）
- 10-24 02:28 phase1/maxvar_r3 完成（126 秒）
- 10-24 02:30 phase1/maxvar_r4 完成（126 秒）
- 10-24 02:32 phase1/maxvar_r5 完成（125 秒）
- 10-24 02:34 phase1/maxvar_p4_r1 完成（137 秒）
- 10-24 02:36 phase1/maxvar_p4_r2 完成（137 秒）
- 10-24 03:03 phase1 全部完成（phase1/ALL_DONE）；下一步：分析、寫 diagnosis.md
- 10-24 03:10 **chain 時間分配（第一階段 4）**：`phase1/analysis/chain.md`（--no-sa --cap-time 0）。target windows 與之後的 DP 在自己的份額內就收斂，
  多給時間用不掉：ejection 0 → chain 階段 11 筆 6.4 秒（def 14.5）、chain 後總位移 GM +0.017%（h05 0.254 → 0.064%）；ejection 0.25 → 10.3 秒、+0.0003%；
  4 輪 +0.035%；2 倍時間 −0.0115%。結論：「ejection 少給、DP/windows 多給」拿不到更多收穫，只會讓 chain 提早結束（剩下的時間流到 cap 搜尋）。
- **最大位移變動（第一階段 5）**：`phase1/analysis/maxvar.md`。h01 / h04 / h07 5 次完全相同，ibm05 5 次相同（P4 慢機一次 199.03 vs 199.918），
  **h06 雙峰**：427.55（5/7）或 435.19（2/7）。差在 cap 搜尋第一次嘗試（D 427.57，rows 修復後總位移 +34%）之後的 DP：4.03 秒 → 890,099.9 ≤ 上限 891,241 接受；
  3.35 秒 → 892,622.5 被拒（total_budget）。chain 階段多用 0.1 秒就會改變結果 → 牆鐘預算造成的不穩定，不是程式改動。
- **SA / polish 分配（第一階段 6b）**：`phase1/analysis/split.md`。h06/h07/h08/e15 一律 SA 越多越好：總 22 秒時 SA 20 + polish 2 最好（SA 10 差 0.8–1.9%、SA 5 差 2.4–11%）；
  總 90 秒時預設 SA 60 + polish 30（polish 實際 2–3 秒就收斂）比 SA 30 + polish 60 好 1.0–3.2%。不改。
- band 損失第一次量錯（sa_bench2 --moves 是每個 band 的次數，auto 做了 8 倍）→ phase1c 重跑（auto 每 band 20M/bands）。phase1b（2 核執行緒數、L1/L2 --no-rowopt 探測）排隊中。
- 10-24 03:18 phase1b/norowopt 完成（386 秒）
- 10-24 03:18 phase1b 完成（phase1b/ALL_DONE）
- 10-24 03:19 phase1c（band 損失重跑，同總次數）完成
- 10-24 03:40 phase1b：2 核上 h08 SA 60 秒（2 seeds）8 執行緒 HPWL 比 2 執行緒 −1.6%、次數 +28%；`--no-rowopt` 探測 L2 總 −2.0% / 最大 −3.3% / HPWL −3.2%、L1 總 −0.47%。
  phase1c（band 損失，總次數同 20M）：平均 +0.634%，ibm01 +2.46%、ibm05 +0.85%、h06 +0.28%、h07 −0.36%、h08 −0.06%。
- **第一階段完成：`Testcases/results/ClaudeOpus55_v7/diagnosis.md`**。排序：A 大設計 Stage 1 排程（pareto 斷崖、低收穫步驟）、B 少核心執行緒數、
  C 主視窗 DP 共用 memo、D 額外步驟保留量；chain 重新分配、SA/polish 分配、DP 尾巴提早停、平行 SA band 不做（數據見 diagnosis 第 4、6 節）。

## 第二階段：實作與訓練（開始 10-24 03:45）

下一步：A（大設計排程）→ B（執行緒）→ C（memo）→ D（保留量），每項配對訓練（`scripts/phase2_train.py`，對照同輪 CPC）。
- 10-24 03:30 phase2/train/A1/cand 完成（265 秒）
- 10-24 03:34 phase2/train/A1/cpc 完成（258 秒）
- 10-24 03:35 phase2/train/A1/obs 完成（21 秒）
- 10-24 03:35 phase2/train/A1（預設）：雙條件 8 vs CPC 8/11，最大 GM 1.00185，總 GM 0.99979，Stage1 265/258 秒；見 phase2/train/A1/pair.md
- 10-24 03:41 phase2/large/A1/v7_all 完成（386 秒）
- 10-24 03:48 phase2/large/A1/cpc_all 完成（387 秒）
- 10-24 03:54 phase2/large/A1/v7_P2t2 完成（386 秒）
- 10-24 04:01 phase2/large/A1/cpc_P2t2 完成（387 秒）
- 10-24 04:01 phase2/large/A1 完成：見 phase2/large/A1/summary.md
- 10-24 04:02 **A1（大設計 Stage 1 排程）保留**。`phase2/large/A1/summary.md`（v7 ÷ 同輪 CPC）：all L1 總 −0.42%、最大 +0.04%、HPWL +0.20%；
  **all L2 總 −6.42%、最大 −7.25%、HPWL −7.39%**（這次 CPC 自己也掉進 pareto 斷崖：Stage 1 107.5 秒）；P2t2 L1 總 −4.32%、L2 總 −3.52% / 最大 −6.14% / HPWL −8.93%。
  全部合法、時間 ≤ CPC。L2 額外步驟 6.65 / 6.46 秒（CPC 6.16 / 6.17）→ 由 D 處理。訓練集 `phase2/train/A1/pair.md`：pareto 之前與 CPC 完全相同，
  h04 / h07 的差異是我同時在 E-core 跑 h04 驗證拖慢了計時程式（legalize 均勻慢 37%），h06 是已知雙峰 → 用 ABCD 重跑乾淨的配對。
- B（自動 8 執行緒）、C（finish 的 memo 交給主 DP）、D（cap 搜尋依量到的超時量決定是否開始）已寫好；C 在 h01/h03/toy/h04 的 legal 輸出逐位相同，
  主 DP 解的視窗 h01 291 → 0、h03 2739 → 1458、h04 8446 → 6825。C++ 測試 10/10（新增 pareto 平行與執行緒數無關、過期限只跑一輪）。
- 教訓：計時中的程式沒綁核，會用到 E-core；計時期間不要在 E-core 跑重的東西（只編譯／分析）。
- 10-24 04:06 phase2/train/ABCD/cand 完成（251 秒）
- 10-24 04:11 phase2/train/ABCD/cpc 完成（258 秒）
- 10-24 04:11 phase2/train/ABCD/obs 完成（20 秒）
- 10-24 04:11 phase2/train/ABCD（預設）：雙條件 8 vs CPC 8/11，最大 GM 1.00161，總 GM 0.99955，Stage1 250/258 秒；見 phase2/train/ABCD/pair.md
- 10-24 04:20 phase2/large/ABCD/v7_all 完成（506 秒）
- 10-24 04:28 phase2/large/ABCD/cpc_all 完成（510 秒）
- 10-24 04:37 phase2/large/ABCD/v7_R2 完成（534 秒）
- 10-24 04:46 phase2/large/ABCD/cpc_R2 完成（555 秒）
- 10-24 04:46 phase2/large/ABCD 完成：見 phase2/large/ABCD/summary.md
- 10-24 04:47 **ABCD 訓練**（`phase2/train/ABCD/pair.md`）：與同輪 CPC 除 h06 外逐筆相同（h06 這次落在 435.19 那一峰，CPC 427.56），雙條件 8/11 = 8/11，
  總 GM ÷CPC 0.99955，Stage 1 250.4 vs 257.7 秒（C 省 7.3 秒）。h06 重跑 5 次排在 `phase2/h06rep/ABCD`。
- **ABCD 大設計**（`phase2/large/ABCD/summary.md`，v7 ÷ 同輪 CPC）：all L1 總 −0.78% / 最大 −0.10% / HPWL +0.06%；all L2 −6.41% / −6.61% / −7.38%（額外步驟 4.56 秒，D 有效）；
  all h08 −0.11% / 0 / −0.34%。R2（真 2 核：v7 自動、CPC --threads 2）：L1 −7.78% / 0 / **HPWL +1.38%**；L2 −3.65% / −6.61% / −8.80%；h08 +0.52% / −1.10% / −0.53%、149 vs 168 秒。
  → L1 在 2 核的 HPWL 變差來自 SA：受記憶體上限的大設計 4 個 worker 擠 2 核只多 6% 次數、band 邊界加倍。**B 修正（B2）**：SA 在受記憶體上限（2e6/n < 8）時 worker ≤ 可用 CPU
  （`availableCpus()` = sched_getaffinity，taskset/cpuset 看得到）；DP / pareto 仍固定 8。重測排在 `phase2/large/ABCD2`（R2、R4，含 h08）。
- SA band 數在預設 60 秒下的影響（4 vs 8，訓練 5 筆 × 2 seeds）排在 `phase2/bands60`。
- 10-24 04:49 phase2/h06rep/ABCD：h06 最大位移 5 次 = 427.56 427.56 427.56 427.53 435.19
- 10-24 05:09 phase2/bands60 完成（SA 60 秒，4 vs 8 band）
- 10-24 05:18 phase2/large/ABCD2/v7_R2 完成（534 秒）
- 10-24 05:27 phase2/large/ABCD2/cpc_R2 完成（567 秒）
- 10-24 05:36 phase2/large/ABCD2/v7_R4 完成（514 秒）
- 10-24 05:45 phase2/large/ABCD2/cpc_R4 完成（529 秒）
- 10-24 05:45 phase2/large/ABCD2 完成：見 phase2/large/ABCD2/summary.md
- 10-24 05:49 phase2/tests/B2：一般 / ASan+UBSan / TSan 全部通過（src 43fc6388fa8b）
- 10-24 05:50 **ABCD2（B2 修正後，真 4 / 2 核）**（`phase2/large/ABCD2/summary.md`）：R2 L1 總 −7.73% / 最大 +0.09% / HPWL +1.26%；R2 L2 −1.98% / −4.48% / −2.63%；
  R2 h08 +0.51% / −1.11% / −0.61%（149 vs 168 秒）；R4 L1 −5.17% / −0.06% / +0.43%；R4 L2 −4.70% / −7.16% / −8.48%（CPC 額外步驟 6.20 秒、v7 4.57）；R4 h08 +0.12% / 0 / −1.63%。
  L1 的 HPWL 在 2 核仍 +1.26%：SA 已與 CPC 同為 2 worker、4,900 萬次，差在起點（v7 legal HPWL 5.433e9 vs CPC 5.114e9，總位移 −7.7%）：
  視窗 DP 拿到更多時間只壓位移、打亂 cell 順序。是位移與 HPWL 的取捨，全核時 SA 之後 +0.06%（ABCD）/ +0.20%（A1）；最終驗證 L1 跑 3 seeds 看。
- h06 重跑 5 次（ABCD）：427.56 ×3、427.53、435.19（CPC 5/7 為 427.55）→ 雙峰是雜訊，不是變差。
- bands60（SA 60 秒、無 polish、2 seeds）：8 band 對 4 band：ibm01 −0.6%、ibm05 0.0%、h06 0.0%、h07 −1.0%、h08 −1.0% → 預設時間下 band 邊界不是瓶頸，平行 SA 不改。
- 測試（`phase2/tests/B2`，src 43fc6388）：一般 / ASan+UBSan（detect_leaks=1）/ TSan 各 10 程式 28 項全過，無 sanitizer 警告；新增測試：pareto 平行與執行緒數無關、
  過期限只跑一輪、availableCpus 跟著 affinity。固定次數決定性（`phase2/determinism`）：h06 SA 1M 次 × 4 / 自動執行緒各兩次，.pl 逐位相同。
- **B 完成**：保留 A（大設計 Stage 1 排程 + 細期限 + pareto 平行）、B2（自動 8 worker；SA 受記憶體上限時 ≤ 可用 CPU）、C（memo 交給主 DP）、D（cap 搜尋超時量）。
  不做：chain 重新分配（無收穫）、平行 SA band（預設時間下 8 band ≥ 4 band）、SA/polish 分配（現行最好）、DP 尾巴提早停（併入 C）。
  之後只改了 main.cpp 的註解與 usage。**凍結 10-24 05:52**：`frozen_sources.sha256`（53 檔，manifest sha256 02eb0f9e…，src 串接 sha256 86440a71…）、`freeze.json`。
- 下一步：`final_validate_v7.py`（背景，完成標記 `final/ALL_DONE`），之後 `final_report_v7.py`。
- 10-24 06:03 final/lg_ClaudeOpus55_v7 完成（622 秒）
- 10-24 06:12 final/edge_ClaudeOpus55_v7 完成（578 秒）
- 10-24 06:23 final/lg_CodexParallelChain 完成（630 秒）
- 10-24 06:33 final/edge_CodexParallelChain 完成（583 秒）
- 10-24 06:43 final/lg_CodexChainDP 完成（643 秒）
- 10-24 06:53 final/edge_CodexChainDP 完成（579 秒）
- 10-24 06:55 final/lg_ClaudeOpus55 完成（130 秒）
- 10-24 06:59 final/lg_ClaudeOpus55_v7_L1_s1 完成（204 秒）
- 10-24 07:02 final/lg_CodexParallelChain_L1_s1 完成（203 秒）
- 10-24 07:05 final/lg_ClaudeOpus55_v7_L1_s2 完成（204 秒）
- 10-24 07:09 final/lg_CodexParallelChain_L1_s2 完成（203 秒）
- 10-24 07:12 final/lg_ClaudeOpus55_v7_L1_s3 完成（204 秒）
- 10-24 07:16 final/lg_CodexParallelChain_L1_s3 完成（204 秒）
- 10-24 07:19 final/lg_ClaudeOpus55_v7_L2_s1 完成（227 秒）
- 10-24 07:23 final/lg_CodexParallelChain_L2_s1 完成（243 秒）
- 10-24 07:27 final/lg_ClaudeOpus55_v7_L2_s2 完成（227 秒）
- 10-24 07:31 final/lg_CodexParallelChain_L2_s2 完成（243 秒）
- 10-24 07:35 final/lg_ClaudeOpus55_v7_L2_s3 完成（227 秒）
- 10-24 07:39 final/lg_CodexParallelChain_L2_s3 完成（241 秒）
- 10-24 07:39 final/edge_seed7 完成（15 秒）
- 10-24 07:47 final/seed_ClaudeOpus55_v4 完成（439 秒）
- 10-24 07:56 final/seed_CodexChainDP 完成（581 秒）
- 10-24 08:06 final/seed_ClaudeOpus55_v6 完成（576 秒）
- 10-24 08:16 final/seed_CodexParallelChain 完成（576 秒）
- 10-24 08:25 final/seed_ClaudeOpus55_v7 完成（566 秒）
- 10-24 08:29 final/ts_ClaudeOpus55_v4 完成（227 秒）
- 10-24 08:35 final/ts_CodexParallelChain 完成（321 秒）
- 10-24 08:40 final/ts_ClaudeOpus55_v7 完成（302 秒）
- 10-24 08:53 final/hpwl_fixed 完成（804 秒）
- 10-24 09:27 final/hpwl_equal 完成（2043 秒）
- 10-24 11:03 final/hpwl_default 完成（5774 秒）
- 10-24 11:04 final/extra_testcases_ClaudeOpus55_v7 完成（65 秒）
- 10-24 11:06 final/extra_testcases_CodexParallelChain 完成（66 秒）
- 10-24 11:06 final_validate_v7.py 全部完成（final/ALL_DONE）；下一步：python3 scripts/final_report_v7.py
- 10-24 11:13 final_extra/satput 完成（同起點的 SA 次數：binary 還是起點）

## 最終驗證與交付（10-24 05:55 – 11:40）

- `final_validate_v7.py` 全部完成（`final/ALL_DONE`），`final_report_v7.py` → `final/summary.md / summary.json`。開跑前、跑完後 frozen_sources.sha256 都相符。
- 27 筆：v7 27/27、雙條件 20/27（CPC 21/27，差在 e15 總位移超上限 0.04%）、最大 GM 0.98190（CPC 0.98409）、總 GM 0.99986（0.99977）；逐筆最大沒有比 CPC 差 > 0.2%。
  新 seed edge_seed7（凍結後產生）：v7 16/16、雙條件 12/16（CPC 11、v6 11、v4 6）。主要 11 筆 ÷ 同輪 v1：0.8761 / 0.9187 / 0.9111 / 607 秒（CPC 0.8758 / 0.9187 / 0.9118 / 616）。
- L1（3 seeds）：總 ÷ CPC 0.9930、最大 0.9998、HPWL 1.0009（0.9966–1.0079）；L2：0.9527 / 0.9269 / 0.9904，196.6 vs 212.2 秒。額外步驟 v7 L1 5.72、L2 4.58–4.60；
  CPC L2 6.36 / 6.39 / 5.05（超出 2/3）。27 筆與新 seed 額外步驟兩版 0 超出。
- HPWL ÷ CPC（3 seeds）：同次數 1.00000 / 1.00000；同總時間 holdout 0.99979（0.99888–1.00154）、edge 0.99883（0.99651–1.00009）；
  預設 holdout 1.00030（0.99955–1.00138）、edge 1.00100（1.00052–1.00139）。預設差在 e15 +0.98%、e11 +0.24%、e12 +0.19%、h06 +0.31%、h07 +0.18%；
  `final_extra/satput`：hpwl_compare 另外編的 v7 sa_bench 在 h08 同起點仍少 5–13% 次數，但真正 dp 的 SA 次數相同（L1 75M / 75M、L2 4M / 4M、h08 266M vs 253M）。
- edge：`run_edge.sh ClaudeOpus55_v7` 由 final_validate 跑（`Testcases/results/edge/ClaudeOpus55_v7/`）；`make_table.py` 版本清單加 v7、重建 `tables.md`（除了多一欄，其餘逐字相同）。
- 文件：`README.md`（本資料夾）；COMPARISON.md 加第 13 節（原檔 sha256 e6b240b6…，前 44,166 bytes 逐位相同，備份 `provenance/COMPARISON.before.md`）；
  HISTORY.md 表 1、2、5.1、5.2、6 只加 8 列（原檔 5bbeb535…，備份 `provenance/HISTORY.before.md`）；Testcases/README.md 加 2 列。
- 保護資料夾 8 個 297 檔在交付時與開工時相同（`provenance/protected_at_delivery.sha256`）。沒有 commit，沒有 AI 署名。
- **完成。** 未達成項目見 README 第 5 節。

## 交接給下一版（10-24 15:40）

- `AI_Solutions/ClaudeOpus55_v7/HANDOFF.md`：給 Codex 的唯一交接文件（規則、工具、v7 現況、事實、不要再試、目標 T1 SA 速度 / T2 大設計 legal HPWL / T3 中大型穩健 / T4 cap 搜尋、保留條件、最終驗收）。
- `Testcases/results/ClaudeOpus55_v7/codex_kit/`：一個指令跑完的協調者（`run_codex.sh NEW`）、各階段提示詞、量測 / 訓練 / 最終驗證腳本（基準改成 v7）。
  `DRY_RUN=1` 乾跑整條流程通過（暫時資料夾已刪）；新版 `final_report.py` 用 v7 的 final 資料自我測試，數字與 v7 summary 相同。
- 已先產生中大型測資 `Testcases/large/M1~M3`（gen_bench seed 31 / 32 / 33；7 萬 / 13 萬 / 18 萬 cell；git 忽略），v7 讀得進 M3。
