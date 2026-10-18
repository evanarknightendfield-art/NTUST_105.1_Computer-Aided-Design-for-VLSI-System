# Example — 題目給的範例（Project03.pdf 第 2 頁）

| 檔案 | 內容 |
|---|---|
| `toy.aux` `toy.nodes` `toy.nets` `toy.pl` `toy.scl` `toy.wts` | 範例測資（= `Testcases/testcases/toy/`，從 `bench.tgz` 解出來） |
| `example_input.png` | PDF 裡的輸入圖（global placement，31 個 cell 疊在一起） |
| `example_output.png` | PDF 裡的輸出圖（合法化之後） |

PDF 的執行範例：

```
%> dp toy output
Total displacement (legal): 62531.2
Maximum displacement (legal): 5219.7
Total HPWL (legal): 757296
Total HPWL (SA): 690185
%> ls output*
output.aux output.nets output.nodes
output.pl output_legal.pl output.scl
output.wts
```

⚠️ 這組數字**跟附的 toy 對不起來**：附的 `toy.pl` 的 global placement HPWL 只有 2,698.3
（`python3 ../Grader/tools/check_pl.py toy toy.pl`），範例卻印 legal 757,296。
`toy.pl` 的檔案權限也跟其他檔案不同，可能是 TA 之後改過。所以只拿來確認**輸出格式**，
數字用 `Grader/tools/check_pl.py` 檢查自己的輸出。

參考解在附的 toy 上的結果和圖在 `AI_Workspace/step1_example/`。
