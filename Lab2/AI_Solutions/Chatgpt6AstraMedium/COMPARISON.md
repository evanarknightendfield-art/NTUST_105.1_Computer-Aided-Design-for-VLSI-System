# 與 ClaudeOpus55Ultracode 比較

兩版由相同 GCC / Release 設定編譯；每次總時間預算 3 秒，依序執行，不以平行跑分爭用 CPU。所有輸出由課程 verifier 驗證，stdout 分數亦須一致。既有測資 seed 為 1、2；最終新產生測資 seed 為 1。

比值 = 新版 Score / 原版 Score，低於 1 較好。下表列每組所有「測資 × seed」配對，不挑最佳 seed。

| 測資 | 配對數 | 勝 / 平 / 負 | Score 幾何平均比值 | 平均秒數（原 / 新） |
|---|---:|---:|---:|---:|
| 既有測資 | 30 | 13 / 10 / 7 | 0.993246 | 3.0027 / 3.0027 |
| 新測資 | 12 | 5 / 1 / 6 | 0.999754 | 3.0022 / 3.0022 |

## 逐筆結果

每格是依 seed 順序排列的原始 Score。

### 既有測資

| 測資 | 原版 Scores | 新版 Scores |
|---|---|---|
| input001.cbi | 37, 37 | 37, 37 |
| input002.cbi | 30, 30 | 30, 30 |
| input003.cbi | 132, 134 | 132, 132 |
| input004.cbi | 390, 396 | 392, 396 |
| input005.cbi | 881, 883 | 867, 855 |
| input006.cbi | 750, 735 | 772, 731 |
| input007.cbi | 2095, 2115 | 2115, 2120 |
| input008.cbi | 3546, 3546 | 3530, 3493 |
| input009.cbi | 7195, 7281 | 7149, 7176 |
| input010.cbi | 18684, 18752 | 18616, 18728 |
| input011.cbi | 32891, 32791 | 32951, 32797 |
| input012.cbi | 837, 907 | 817, 802 |
| input013.cbi | 883, 876 | 881, 887 |
| input014.cbi | 10, 10 | 10, 10 |
| input015.cbi | 41, 41 | 41, 41 |

### 新測資

| 測資 | 原版 Scores | 新版 Scores |
|---|---|---|
| input001.cbi | 127 | 127 |
| input002.cbi | 296 | 294 |
| input003.cbi | 1442 | 1400 |
| input004.cbi | 2004 | 2031 |
| input005.cbi | 138 | 140 |
| input006.cbi | 370 | 338 |
| input007.cbi | 822 | 815 |
| input008.cbi | 2270 | 2323 |
| input009.cbi | 150 | 162 |
| input010.cbi | 237 | 239 |
| input011.cbi | 1017 | 997 |
| input012.cbi | 2722 | 2766 |

## 解讀限制

- 既有測資及第一組 holdout 曾用於開發，不能視為獨立保留集。
- 最終新測資（final_holdout_inputs）由 `tests/make_holdout.py` 的固定 seed 產生；在演算法定版前沒有查看其比較分數，未根據這組結果再調整演算法。
- `pilot`、`polish_pilot`、`testcases_3s`、`holdout_3s` 是已放棄版本的紀錄；本報告使用 hybrid_pilot + hybrid_repeat 及 final_holdout_3s。
- 這是 3 秒預算的結果，不外推成預設 5–30 秒或所有未知測資都更好，也沒有全域最優保證。
- 時間制退火受到 CPU 負載及排程影響，同 seed 可能有些微差異。來源 SHA-256 與編譯器記錄在 `experiments/manifest.json`。
