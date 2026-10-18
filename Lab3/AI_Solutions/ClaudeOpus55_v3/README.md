# dp v3 — Standard Cell Detailed Placer（第三版）

`ClaudeOpus55_v2` 再改良。重讀 v1 / v2 的程式和課程講義（L06 SA、L07 detailed placement），
並且**先量出離最佳解還有多遠**（`Grader/tools/exact_bound.py`），再決定改哪裡。用法、輸出跟 v1 / v2 一樣。

五個版本（含兩個 Codex 版）的完整比較、下界分析、還能改良的地方：**`../COMPARISON.md`**。

## 結果（11 筆測資，全部通過 `local_grade.py`）

| 幾何平均 | v3 ÷ v1 | v3 ÷ v2 |
|---|---:|---:|
| 總位移 | **−2.9%** | −0.9%（沒有一筆變差） |
| 最大位移 | **−5.2%** | 0.0% |
| HPWL（SA） | **−7.4%** | **−2.2%**（11 筆 9 筆變好） |
| 時間 | 約 4 倍 | 約 1.7 倍 |

ibm01：HPWL 60.05M（v1）→ 53.07M（v2）→ **51.62M**（v3）；ibm05：10.31M → 9.86M → **9.69M**。

## 先量差距

| 部分 | 差距 | 怎麼量的 |
|---|---|---|
| 最大位移 | 小設計離下界 **2 ~ 4%** → 幾乎最佳 | site 層級 LP 的可行性二分 |
| 總位移（同樣最大位移下） | 離最佳 **3.5% ~ 8%** | toy、h01 整數規劃直接解出最佳解，h02 的已知最好解 |
| HPWL | 跑 2 ~ 4 倍久還能再降 **0.5 ~ 4%** → SA 沒收斂 | 長時間實驗 |

所以 v3 的重點放在：**SA 每一步的品質和次數**、**總位移最後的收尾**。

## v3 的改動

### Stage 2（`src/annealer.cpp`）

| # | 改動 | 來源 | 效果 |
|---|---|---|---|
| 1 | **Linear placement move**：一個 row 裡連續 12 個 cell，順序不變，移到讓 x 方向 HPWL 最小的位置 | 講義 L07：Kahng et al., ASPDAC'99 的 clumping 演算法 | 順序固定時，每條 net 只有視窗裡最左 / 最右的 cell 有影響，每個 cell 的 cost 是凸的分段線性函數 → pool-adjacent-violators 解出**精確最佳**；接受率 100%、88% 有變好 |
| 2 | **Independent set matching move**：a 附近 12 個同寬、彼此不共用 net 的 cell，用 Hungarian 重新指派位置 | NTUplace3、ABCDPlace | 彼此不共用 net → 每個 cell 的 cost 只跟自己的位置有關 → 指派問題的最佳解就是這組 cell 的 HPWL 最佳解；96% 有變好 |
| 3 | **1、2 只在後半段（低溫）用** | 「SA 探索、貪心收尾」 | 一開始就用的話，稀疏的 h05 變差 1.5%（太早掉進 local minimum）；只在後半段用，5 筆測資都比不用好 0.5 ~ 1.6% |
| 4 | **net bounding box 快取** | — | 被移動的 pin 原本嚴格在 box 內部時，新 box = 舊 box 延伸到新位置（O(1)）；optimal region 也用它。結果完全一樣，只快 8% |
| 5 | **SA 次數依大小**：5000 × cell 數，夾在 500 萬 ~ 6000 萬次 | 長時間實驗 | 中型設計（ibm01、h03 ~ h07）多做 2 ~ 2.5 倍；最大的（ibm05、h08）只多 7% / 20%。這一項大約貢獻 v3 vs v2 HPWL 進步的一半（見 `../COMPARISON.md` 第 2 節） |

### Stage 1（`src/legalizer.cpp`）

| # | 改動 | 效果 |
|---|---|---|
| 6 | **最後的收尾輪**：每 row 精確重排 → 匹配掃描 → local move，重複到總位移不再下降 | 最大位移不變；總位移 h08 −3.0%、h07 −1.9%、h06 −1.8%、h04 −1.7%，其他持平 |
| 7 | **交換 + 推擠**（`--swappush`，預設關） | 精確解顯示差距來自少數 cell 一起換 row，所以試了「兩個不同 row 的 cell 互換，各自推開新鄰居」。總位移 −0.1%、最大位移 −0.8%，但 Stage 1 慢 1.7 倍 → 關掉 |

## 編譯與執行

```bash
cmake -S . -B build && cmake --build build
./build/dp INPUT_FILE OUTPUT_FILE [options]
```

新選項（`./build/dp` 會列出全部）：

| 選項 | 說明 |
|---|---|
| `--rowopt P L` | linear placement move 的機率、視窗 cell 數（0.01 12） |
| `--ism P K` | independent set matching move 的機率、cell 數（0.01 12） |
| `--polish-from F` | 上面兩個從第幾成的溫度開始用（0.5） |
| `--effort K` | SA 次數 = K × cell 數（預設 5000，夾在 500 萬 ~ 6000 萬；指定 K 時不設上限） |
| `--finish N` | Stage 1 收尾輪數（4） |
| `--swappush` | 開啟交換 + 推擠 |
| `--sweep R S`、`--total-tol X` | 匹配掃描視窗（6 rows × 120 sites）、總位移輪的停止門檻（5e-4） |

時間：ibm01 約 57 秒、ibm05 約 79 秒、h08 約 88 秒（SA 上限 60 秒）。要快一點：`--effort 2000`（約 v2 的時間）。

`-v` 會多印出 rowopt、ism 兩種 move 的統計：

```
[sa] rowopt     0.8% of attempts, accepted 100.0%, improving  88.1%
[sa] ism        0.5% of attempts, accepted 100.0%, improving  96.2%
```

## 參考資料

- 課程講義 L07：Linear placement（Kahng, Tucker, Zelikovsky, ASPDAC'99）、Window interleaving（Hur & Lillis, ICCAD'00）
- T.-C. Chen et al., "NTUplace3," TCAD 2008；Y. Lin et al., "ABCDPlace," TCAD 2020 — independent set matching
- 其他（Abacus、RippleLG、ISPD'17 flow、FastPlace-DP、VPR）見 `../ClaudeOpus55_v2/README.md`
