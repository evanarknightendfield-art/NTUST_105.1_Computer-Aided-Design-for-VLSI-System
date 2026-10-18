# step1_example — 參考解跑附的 toy

```bash
../../AI_Solutions/ClaudeOpus55/build/dp toy output
```

| 檔案 | 內容 |
|---|---|
| `toy.*` | 輸入（= `Testcases/testcases/toy/`） |
| `output.txt` | stdout 的 4 行 |
| `output_legal.pl` / `output.pl` | Stage 1 / Stage 2 的結果（都通過 `check_pl.py`） |
| `input.png` | 輸入：31 個 cell 疊在一起 |
| `output_legal.png` | Stage 1：菱形（Manhattan 距離下總位移最小的形狀），紅色箭頭 = GP → legal |
| `output.png` | Stage 2：整張晶片 |

```
Total displacement (legal): 46619.2
Maximum displacement (legal): 1987.6
Total HPWL (legal): 101556
Total HPWL (SA): 48054
```

**看 `output.png` 會發現兩件怪事，都是正常的：**

1. **整群 cell 搬到別的地方了。** toy 沒有任何 terminal（fixed pin），HPWL 只看 cell 之間的相對位置，
   整群平移 HPWL 不變，所以 SA 接受 Δ = 0 的 move 時，整群會慢慢飄走。Stage 2 只看 HPWL，題目沒有限制位移。
   有 pad 的設計（ibm05）會被 pad 拉住，不會這樣。
2. **有 3 個 cell 飛得很遠。** 它們沒有接到任何（兩個 pin 以上的）net，放哪裡 HPWL 都一樣。
   想避免的話，SA 挑 cell 時可以跳過沒有 net 的 cell（也省時間）。

參考解跑出來的數字跟 `Example/` 裡 PDF 的範例不同，原因見 `Example/README.md`。
