# 最終版本測試紀錄

測試日期：2026-10-18（Asia/Taipei）。

以既有 `Grader/tools/local_grade.py` 打包後重新編譯，依序執行 3 筆課程與 8 筆額外測資。
這是儲存庫自製的獨立檢查器，不是教師官方評分器。

所有測資使用 `--time 5 --effort 100 --trace trace.csv -v`，SA seed 預設 1。
`--time` 僅限制 SA；表格秒數包含讀檔、Stage 1、SA 與寫檔。
受時間上限影響的結果可能隨主機負載變動；以下均為本次實際結果。

**結果：11/11 PASS**。兩階段皆合法、七個輸出檔完整、stdout 數值與外部檢查器一致，
最終 HPWL 均不高於合法化 HPWL。另檢查 11 份 trace 的 best 單調不增、best ≤ current，
及最後 best 與輸出 HPWL 相符，全部通過。

| 測資 | 總位移 | 最大位移 | HPWL legal | HPWL SA | 降幅 | 秒數 | 結果 |
|---|---:|---:|---:|---:|---:|---:|---|
| ibm01 | 46,210,453.440 | 25,242.460 | 107,244,648.000 | 81,312,168.000 | 24.18% | 3.08 | PASS |
| ibm05 | 2,115,318.807 | 559.790 | 12,431,984.000 | 11,663,291.500 | 6.18% | 8.6 | PASS |
| toy | 49,378.400 | 2,369.100 | 101,622.000 | 61,158.000 | 39.82% | 0.01 | PASS |
| h01 | 3,018.590 | 52.610 | 26,036.500 | 18,959.000 | 27.18% | 0.03 | PASS |
| h02 | 746,007.585 | 4,956.521 | 4,160,990.700 | 3,179,307.800 | 23.59% | 0.13 | PASS |
| h03 | 15,242.974 | 35.218 | 36,737.400 | 20,190.600 | 45.04% | 0.26 | PASS |
| h04 | 458,666.490 | 677.940 | 1,010,279.000 | 461,661.500 | 54.30% | 0.73 | PASS |
| h05 | 1,918,908.544 | 1,880.308 | 26,503,710.200 | 22,180,832.900 | 16.31% | 1.08 | PASS |
| h06 | 2,008,911.040 | 845.230 | 3,273,124.500 | 1,657,195.000 | 49.37% | 2.95 | PASS |
| h07 | 239,840.457 | 97.695 | 521,247.931 | 253,209.367 | 51.42% | 4.66 | PASS |
| h08 | 310,239,009.073 | 71,526.952 | 481,787,677.300 | 256,683,681.800 | 46.72% | 10.49 | PASS |

## 幾何回歸驗證

`tests/geometry.cpp` 以 AddressSanitizer + UndefinedBehaviorSanitizer 編譯執行：

```text
PASS: 5000 exhaustive gap/nearest and transaction invariant trials
```

在不同 spacing、小數座標與不同 cell 寬度下，以窮舉所有 site 的結果比對最近空隙搜尋，
並隨機執行交換、搬移、還原，檢查無重疊、邊界、map 與 placement 一致性。
執行時未出現 sanitizer 診斷；環境設定 `ASAN_OPTIONS=detect_leaks=0`，未測 leak detection。

## 初次測試發現及處理

- toy 的一條 NetDegree 宣告與實際 pin 數不符：採實際 pin 清單、stderr 警告，原始檔不變。
- 第一輪 ibm05 在 5 秒預算內 SA 未改善；加入 60% 進度回到最佳解的重啟機制後，重新跑完整 11 筆。
- README 說明這是啟發式：11/11 合法不代表全域最佳，也不保證所有可行輸入都能配置成功。

## 原始紀錄

- `results/grade/record.csv`、`results/grade/summary.csv`
- `results/grade/logs/CodexGapSA.log`（編譯命令與逐案執行結果）
- `results/grade/results/CodexGapSA/<case>/output_legal.pl`、`output.pl`、`trace.csv`
- `results/grade/submissions/CodexGapSA.tgz`（測試當時的快照，不是學號格式的最終繳交包）

## 已測程式版本（SHA-256）

- `src/db.cpp`: `f76a22837924a6cd6dd3acf464cda797a73532530cf61565cda666e61d6548e6`
- `src/db.hpp`: `2e69652d3d1de2ee537ae28f77984fc29e701ac448d078d0dd94b944d64fa313`
- `src/placer.cpp`: `c0477ee0e111236964548df0554c14ce26eb6148e7e611fc8bbdb79680b2622b`
