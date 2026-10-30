# results/MyCode — `Lab3/MyCode/` v1 ~ v5 的量測

一次只跑一個 dp（`timing_global.lock`），照 10/28 的方式在新資料夾裡 `./dp input001 output001`，各版預設參數，CPU 0–15，
跑完用 `Grader/tools/check_pl.py` 檢查兩個輸出（E-core 16–19）。各版 README 的數字都從這裡來。

| 檔案 | 內容 |
|---|---|
| `run_versions.py` | 量測腳本：編譯 `MyCode/<版本>` 到 `build/<版本>/`，跑 ibm01、ibm05、L1、L2（`--suite ibm-place2` 跑 16 筆）；已有的筆跳過 |
| `records.csv` | 四筆的結果，版本名見下表 |
| `records_ibm-place2.csv` | v3、v4、v5 在 ibm-place2 16 筆的結果 |
| `runs/<版本>_<測資>/` | 每一筆的 stdout、stderr、`time.txt`（placement 輸出跑完就刪） |
| `extra/run_extra.sh` → `extra/v3_*.log` | v3 的補充實驗：Stage 1 各部分、關掉低溫起點；`basic_sa_L1_verbose.log` = v2 的基本 SA 在 L1 的 `-v` 紀錄 |
| `extra/run_v4_ablation.sh` → `extra/v4_ablation.log` | v4 拿掉每一項的結果 |

`records.csv` 的版本名：

| 版本名 | 是什麼 |
|---|---|
| `v1_abacus` ~ `v5_final` | 五個版本 |
| `v1_tetris_mode` | v1 用 Tetris 合法化（= `v1_abacus --legalizer tetris`） |
| `v2_basic_sa` | v2 的基本 SA（M6-2：固定 window、時間到就停、每個溫度才存 best） |

```bash
python3 run_versions.py                         # 全部（records.csv 已有的跳過）
python3 run_versions.py v4_match_polish --rerun # 重跑某一版
python3 run_versions.py v3_refine --suite ibm-place2 --cap 120
./extra/run_v4_ablation.sh
```

v1、v2 沒有全程的時間限制，L2 最多等 300 秒（`--cap`）。`build/`、`.pl` 不進 git。
