# Testcases

測資都是程式產生的，所以不放進 git。clone 下來後先跑：

```bash
./make_testcases.sh
```

| 資料夾 | 內容 | 產生方式 |
|---|---|---|
| `testcases/` | 15 筆（001 = 課程範例，002–015 從 5 到 10000 個 sinks） | `Grader/tools/gen_input.py --suite`（seed 固定） |
| `holdout/` | 12 筆報告用的新測資 | `Report/scripts/gen_tests.py`（seed 2026） |
