## 🧑 我

幫我做 Project 02
先用 Canda 建虛擬環境 eda
先做一次 EXAMPLE 再做一次 評分程式 再來你做一遍你想的 Project 再來教我做
我自己做一遍

## 🤖 Claude

我先看一下 Lab2 目錄裡有什麼，了解 Project 02 的內容。

## 🤖 Claude

我已讀完題目 PDF 和評分程式（verifier.py），接著看 Example 資料，然後建 conda 環境。

## 🤖 Claude

conda 環境 `eda` 已建立。系統沒有 cmake，我把 cmake 和畫圖用的 matplotlib 裝進 `eda` 環境：

## 🤖 Claude

環境 OK：`eda`（Python 3.12 + cmake 3.31 + matplotlib），系統 g++ 15。下面先建立工作目錄結構，然後進行第 1 步：跑 Example。

## 🤖 Claude

我在寫參考解的 C++ 程式（已完成 parser、樹的資料結構、buffer 位置搜尋），先把 `nearestUsable` 裡一段多餘的寫法清乾淨：

## 🤖 Claude

接著是獨立的合法性檢查與輸出（寫檔前一定再從頭驗證一次，驗證失敗就退回初始解，避免交出 INVALID）：

## 🤖 Claude

最後是 `main.cpp` 和 `CMakeLists.txt`：

## 🤖 Claude

Build 乾淨（沒有 warning）。先拿 Example 試跑：

## 🤖 Claude

Example 上拿到 **Score 37**（PDF 範例最好是 100），skew 只有 1。用 TA 的 verifier 和我的逐步檢查器確認：

## 🤖 Claude

先每筆給 3 秒，快速看合法性和各階段結果：

## 🤖 Claude

內部檢查全部合法。先用 TA 的 verifier 確認 15 筆都 VALID：

## 🤖 Claude

Now add a "single root" mode to construction, so the top-level mismatch at SRC can be aligned by a root buffer:

## 🤖 Claude

Now update `main.cpp` to the three-stage flow (build both construction variants → balance each → keep the better → SA):

## 🤖 Claude

The linker error is only because `file(GLOB)` is evaluated at configure time and `balance.cpp` is new — re-configure:

## 🤖 Claude

Stage 2 現在很有效：010 從 50928 降到 21220（比之前「只靠 SA」的 29396 好很多）。但在大測資上 Stage 3 SA 完全沒有改善，我開 verbose 看 SA 的過程：

## 🤖 Claude

gprof call counts look distorted by inlining; let me use `perf` for an accurate picture:

## 🤖 Claude

300/300 VALID, no crashes, including extreme cases (L down to 5, SRC length 2, 3×3 chips). Next, check large-scale runtime and memory to make sure we stay well under the 60-second limit:

## 🤖 Claude

100k sinks：25 秒、83 MB，遠低於 60 秒 / 8 GB 的限制。再確認一下 Example 的 37 分是不是已經很接近最佳（用不同 seed、跑久一點）：

## 🤖 Claude

Release 設定確認有生效（-O3）。接著把 TA 的評分流程做成本機版。driver.py 需要 qemu VM，這裡改成照同樣的步驟在本機跑：

## 🤖 Claude

表格出來了：Stage 2 在 skew 很大時效果最好（010：40430→21220），Stage 3 在所有規模都再降 10–60%。看一下 006 三個階段的圖：

## 🤖 Claude

驗證抓到骨架的一個問題：`addBuffer` / `link` 這些輔助函式定義在 `buildInitial` **後面**，你在 Milestone 3 會呼叫不到。我把它們移到資料結構下面：

## 🤖 Claude

Now redo the validation against the fixed skeleton (same patch, fresh copy):

## 🤖 Claude

`stage_report.py` 現在你的程式和我的都能用了。小修：sink 數和 w_skew 改成直接從輸入檔讀（你的程式不用特別印），並更新教學裡的說明：

## 🤖 Claude

Regenerate the reference report data with the final binary, and move it out of the project folder (otherwise it would get packed into the zip):

## 🤖 Claude

Add the new `reference_report/` folder to the tutorial's folder table:

## 🤖 Claude

最後的整體檢查：骨架移動函式後可以編譯、參考解最後一次的 15 個輸出都通過 TA verifier、資料夾結構正確：

## 🤖 Claude

Project 02 的四個步驟都完成了，你接下來可以照 [TUTORIAL.md](TUTORIAL.md) 自己做。

**1. conda 環境 `eda`**
已建好，裡面有 Python 3.12、cmake 3.31（系統原本沒有 cmake）和 matplotlib。C++ 用系統的 g++。

**2. Example**
- [cbi_explain.py](tools/cbi_explain.py) 會逐條列出邊長、fanout/length 用量和每個 sink 的 arrival，三個範例答案都算出和 PDF 一樣的 131 / 100 / 127。
- [plot_cbi.py](tools/plot_cbi.py) 取代範例附的 gnuplot，圖存在 `step1_example/`。

**3. 評分程式**
- 用 TA 的 verifier 評三個範例答案和五個故意做錯的答案（buffer 壓在邊界、length 超過上限、`.level` 寫錯、漏掉 sink、沒有輸出檔），都判對了。
- verifier 有兩點比 PDF 嚴格：buffer 不能壓在邊界上（要 0 < x < dimx），`.level` 的層數必須等於實際的 BFS 深度。
- 真正的 driver.py 要 qemu VM 映像檔，壓縮包裡沒有，所以我寫了 [local_grade.py](tools/local_grade.py)，在本機照同樣流程跑：打包 zip → 解壓 → cmake → 每筆限時 60 秒 → verifier。

**4. 我的版本 [claude_reference/](claude_reference/README.md)**
- 分三個階段：
  - **建構：** 由下往上分群放 buffer，得到合法初始解。
  - **平衡：** 在太早到的子樹上方插繞路 buffer，拉高 T_min。
  - **模擬退火：** 每一步先試做，不合法就還原，所以全程合法。
- Example 拿到 **37**（官方三個答案最好是 100），8 個不同 seed 都是 37。
- 15 筆測資（5 到 10000 個 sinks）全部 VALID，300 筆隨機的極端測資也全部 VALID。
- 10 萬個 sinks 跑 25 秒、用 83 MB，遠低於 60 秒的限制。
- 一個可以寫進報告的發現：sink 的 arrival 奇偶性由它的 (x+y) 決定，永遠改不了。所以 sink 的 (x+y) 奇偶不一致時，skew 至少是 1。Example 的 37 = cost 36 + skew 1，已經碰到這個下限。

**5. 教你做**
- 骨架 [my_cbi/src/main.cpp](my_cbi/src/main.cpp) 已經接好 main 流程，你要寫的部分分成 Milestone 1–4（讀檔 → 評分與輸出 → 合法初始解 → 最佳化），每個都有檢查用的指令。
- 我在暫存區照教學裡的提示實際做過一遍。過程中發現骨架的輔助函式放錯位置（Milestone 3 會呼叫不到），已經修好。照提示做出來的基本版全部 VALID，分數大約是參考解的 1.2–2.5 倍，教學裡有逐筆對照表。

還有幾件事要你自己處理：
- 我不知道你的學號，參考解在本機評分時用的是假學號 M00000000。
- 報告我沒有寫。我準備了 [stage_report.py](tools/stage_report.py)，可以產生各階段的 cost/skew/Score 表格和圖，參考解的範例在 `reference_report/`。
- 參考解每筆預設最多跑 25 秒（60 秒限制內），小測資約 3 秒。

從這裡開始：

```bash
conda activate eda
```

```bash
cp -r my_cbi M你的學號
```

建議做完 Milestone 3 再去看 `claude_reference/`。每完成一個 Milestone 都可以把程式丟給我，我幫你看。

## 🧑 我

請幫我查看 M11502102_GPT61Sol 與 你做的演算法哪個比較好

## 🤖 Claude

I'll read the whole source of GPT61Sol first, then run both programs on the same testcases.

## 🤖 Claude

Both pipelines finished. My default uses up to 25 s on large cases, so for fairness I'll also run my algorithm with the same 5-second cap as GPT61Sol:

## 🤖 Claude

GPT61Sol crashed on 2 of the 300 cases (no output file, so the case scores zero). My version passed all 300. Let me find out why those two failed:

## 🤖 Claude

**我做的演算法（`claude_reference`）比較好**：分數更低，也更穩定。我把兩個程式放在同樣條件下比：都用 TA 的 verifier 評分，每筆都限 5 秒；我的版本預設會跑到最多 25 秒，所以另外加了 `-t 5` 讓時間相同。

| 比較 | GPT61Sol | 我的版本 |
|---|---|---|
| 原本 15 筆測資 | 13 筆輸、2 筆平手，分數平均高 1.67 倍 | 13 筆贏、2 筆平手 |
| **24 筆新產生的測資**（我調參時沒用過） | 22 筆輸、2 筆平手，分數平均高 **2.11 倍** | 22 筆贏、2 筆平手 |
| 300 筆極端測資 | **2 筆沒有輸出檔**（會被判 MISSING） | 300 筆全部 VALID |
| 10 萬個 sinks 的執行時間 | **55.2 秒**（限制 60 秒） | 25.1 秒 |

- **平手的都是很小的測資**（範例、6 個 sinks、1 個 sink），兩邊都已經找到同一個最佳解。
- **差最多的是 w_skew 大的測資**，例如新測資 013、016（w_skew=10）差 4.5 倍、005 差 4.8 倍。

**為什麼差這麼多**

GPT61Sol 和 TUTORIAL 裡的「基本版」幾乎一樣：用 Morton 順序貪心分群建樹，最佳化只會隨機移動 buffer 位置。差距來自三點：

1. **它不會改樹的結構。** 一群 sink 一開始分在哪個 buffer 底下，之後就不會再改。我的版本可以換 parent、刪 buffer、插 buffer、拆群。
2. **它沒辦法讓太早到的 sink 變晚。** 新測資 013 裡，它的 T_min 和 T_max 差了好幾千，skew 乘上 w_skew=10 後主導了分數。我的 Stage 2 會插繞路 buffer 把 T_min 拉上來。
3. **每試一次就整棵樹重算（還要排序）。** 5 秒內在大測資只能試幾千次，我的增量更新每秒可以試幾十萬次。

**GPT61Sol 的風險**
- **大測資可能超時。** 找 buffer 空位時，每個候選位置都要掃過所有節點，10 萬個 sinks 光建樹就要約 50 秒。TA 的 VM 只有 2 核心，如果比這台慢，超過 60 秒就會被砍掉，沒有輸出檔。只要改用 hash set 記錄已佔用的座標，就能降到幾秒內。
- **建樹卡住時直接 `exit(1)`，不寫輸出檔。** 失敗的兩筆一筆是 SRC length 只有 2，另一筆是晶片只有 3 格寬，TA 測資應該不太會出現，但這兩筆都直接拿 0 分。

**還不知道的事**：這兩組測資都是我用自己的產生器做的，TA 的隱藏測資長什麼樣子不知道。規模接近範例的那 12 筆新測資，我的版本也贏 10 筆、平手 2 筆，所以結論應該不會反過來。

所有結果都放在 scratchpad 裡，`M11502102_GPT61Sol/` 和你的 `local_grade_M11502102/` 都沒有被改動。要我照上面三點幫你看怎麼改進 GPT61Sol 嗎？

## 🧑 我

那請問你有用到 OPENROAD 嗎 題目說有需要用嗎

## 🤖 Claude

沒有用到 OpenROAD，題目也不需要用。

**我的程式**：只用了 C++17 標準函式庫（`<vector>`、`<algorithm>`、`<unordered_set>` 等），沒有任何外部工具或函式庫。

**題目的要求**：我搜過兩份 PDF，都沒有提到 OpenROAD，而且有幾點其實讓它沒辦法用：

- Requirement PDF 寫建議用 C++17，並說「只用 C++ 標準函式庫，OS、編譯器、記憶體都不會是問題」。
- 評分 VM 斷網，只裝了 `gcc`、`g++`、`cmake`、`python3`（見 `grader/README.md`），沒有 OpenROAD。程式如果依賴它，在 VM 上會編譯失敗。
- Project02.pdf 明寫 *"No actual clock tree routing is required"*。輸入只有座標和 buffer 的 F/L/C，延遲就是 Manhattan 距離相加，沒有 LEF/DEF/Liberty 這些實際設計檔。OpenROAD 的 clock tree synthesis 工具（TritonCTS）吃的是實際設計，跟這題的格式和計分方式都對不上。

所以這題要自己寫演算法，你的 GPT61Sol 和我的參考解都是這樣做的。OpenROAD 頂多可以在報告的 related work 提一句業界 CTS 怎麼做，不是必要的。

## 🧑 我

好 那請你 再創一的檔案叫 M11502102_ClaudeOpus55Ultracode 寫一份有更好的演算法的程式

## 🤖 Claude

I'll create a new project folder starting from my reference solution, then improve the algorithm with measured experiments. First I need to know where the remaining room for improvement is: how far the current scores are from a lower bound, and whether cost or skew is the bigger loss.

## 🤖 Claude

A benchmark harness: runs a binary on a set in parallel, validates each output with the TA's verifier functions, and compares against a baseline:

## 🤖 Claude

Baseline recorded. Now I'll implement the new moves. Let me re-read the current optimizer in the new project before editing it:

## 🤖 Claude

I'll start with placement: factor the coarse-to-fine search into a generic `searchSpot` that takes any objective, so the new moves can reuse it with their own objectives:

## 🤖 Claude

I'll rewrite `placement.cpp` around a generic search:

## 🤖 Claude

Builds cleanly. Benchmark on the tuning set, same 5 s per case as the baseline:

## 🤖 Claude

Remove the stale comment, then run an ablation on the tuning set. Each configuration runs with 2 seeds to reduce noise, and all are compared against the reference baseline:

## 🤖 Claude

Builds. Now test: (a) adding the SkewReparent move, (b) plus continuation, starting at 10% of w_skew and reaching the full value at 60% of the run:

## 🤖 Claude

All within ±1.5%. The tuning knobs are exhausted, so a real improvement needs a qualitatively different idea. Let me look at an actual solution to see where the buffers go:

## 🤖 Claude

I'll replace the occupancy set with an `Occupancy` class (bitmap, plus a hash-set fallback for out-of-range or huge chips):

## 🤖 Claude

Next, turn `searchSpot` into a header template (so the evaluator inlines), with a fast mode for annealing moves:

## 🤖 Claude

Builds cleanly. Check the speedup and the score effect:

## 🤖 Claude

The profile is more balanced now. `TypeTable::pick` is still 10% because it lives in another .cpp and can't be inlined. I'll move it into the header:

## 🤖 Claude

Regroup doesn't beat the simpler moves, so it stays off (weight 0). Next check is the σ weight β in the energy function (currently 0.3):

## 🤖 Claude

Update the optimizer's header comment so it describes the new moves:

## 🤖 Claude

The full submission pipeline passed: zip, clean build (5.6 s), all 15 cases VALID, and the longest case took 30 s. Final check of the folder contents:

## 🤖 Claude

I put the new program in [M11502102_ClaudeOpus55Ultracode/](M11502102_ClaudeOpus55Ultracode/README.md). It is better than both GPT61Sol and my first version on every test set I ran. The gain over my first version is small (about 2.7%); the gain over GPT61Sol is large.

| Test set (none used for tuning) | New / my first version | New / GPT61Sol |
|---|---|---|
| holdout (24 cases) | 0.973: 16 better, 0 worse | 0.458: 22 better, 0 worse |
| holdout2 (24 cases, generated last) | 0.974: 21 better, 1 worse | 0.579: 24 better, 0 worse |
| testcases (15 cases) | 0.972: 11 better, 0 worse | 0.580: 13 better, 0 worse |

Numbers are average Score ratios, so lower is better; each program ran with its own default settings, and the remaining cases are ties.

- Even when both of mine get the same 5 seconds, the new one is 1.5–2.8% better, so part of the gain comes from the algorithm, not just the longer run time.
- All 300 extreme cases are VALID.
- 100k sinks take 30.1 s and 79 MB. The Score is 302769, versus 308896 for my first version and 333759 for GPT61Sol.
- The full submission pipeline (zip → build → run → TA verifier) passed with all 15 cases VALID.

**What I found and changed**
- **Buffer cost is 90–99% of the Score.** The ways I tried to rebalance skew while keeping the cheapest structure didn't help, so the gain had to come from the search itself.
- **Long runs showed the search was speed-limited:** 180-second runs reached 3–15% lower scores, and two different seeds converged to the same values. About 70% of the time went to searching for buffer positions. I switched the free-position check to a bitmap and made the position search cheaper during optimization, which gives about 4× more attempts per second.
- **New structural moves:**
  - merge two neighboring buffers into one;
  - re-split two neighbors' children between them;
  - re-place both affected buffers after a reparent;
  - place a critical buffer against the absolute T_max target;
  - move an early or late sink under a nearby buffer with a better arrival time.
- **Tried but not kept:** ramping w_skew up gradually, regrouping three buffers at once, and retuning temperature, σ weight, restart count and group size. All stayed within ±1.5% noise. They are listed in the README too.

**Run time changes to watch:** the default per case is now 10 + N/100 seconds, capped at 30 (5 seconds for 12 sinks or fewer), versus at most 25 before. Only the Score is ranked and the limit is 60 seconds per case, so I left half as margin for a slower grading machine. `-t` overrides it.

**Before submitting:** the folder name must be exactly your student ID, so copy it to `M11502102/` and leave out `build/`. `M11502102_GPT61Sol/` and your own folders were not touched.

## 🧑 我

那請你 對比 M11502102_Chatgpt6AstraMedium 
再創一個的檔案叫 M11502102_ClaudeOpus55Ultracode_2 寫一份有更好的演算法的程式

## 🤖 Claude

Let me look at M11502102_Chatgpt6AstraMedium first.

## 🤖 Claude

All of AstraMedium's outputs are VALID. For a fair comparison, I'll re-run Ultracode now under the same conditions (same parallelism) rather than reuse older numbers:

## 🤖 Claude

Both chains run at full speed (about 1.1M it/s each). Benchmark on the tuning set at 5 s against a fresh Ultracode baseline, plus `_2` forced to 1 thread to isolate the effect of parallelism:

## 🤖 Claude

Now restructure the Stage 3 loop: outer restarts (full schedule each), inner sync rounds (consecutive segments of that schedule):

## 🤖 Claude

Works: the chains alternate in the lead (epoch 1: chain 0, epochs 2–3: chain 1) and cooling continues across epochs. Now benchmark (fair thread count) and try a few sync intervals on the large set:

## 🤖 Claude

Now the optimizer side: region parameters, the journal check after every move, and choosing move subjects only from the thread's own region (so few moves are wasted):

## 🤖 Claude

Now the decomposition itself, `parallel.h/.cpp`: building the cut and merging the two halves.

## 🤖 Claude

Now integrate into Stage 3: when there are ≥ 2 threads and the case is large (default N ≥ 1000), each epoch uses decomposition; otherwise it falls back to "best of 2 chains":

## 🤖 Claude

The merges happen (4 of 4) but the merged tree fails the consistency check. Let me print why:

## 🤖 Claude

Sink 168 ends up with an invalid parent after the merge. Rather than keep guessing, I'll add a temporary debug check that reports node 168's state in both copies and in the cut:

## 🤖 Claude

Thread 1 changed the parent of sink 168, which belongs to region 0. The region check should have rejected that move. Let me find which code path commits moves without going through the check:

## 🤖 Claude

Both are preceded by a rollback, so that's not it. I'll measure directly: after each thread finishes, count nodes outside its region whose parent changed compared with the starting state:

## 🤖 Claude

The failing node changes from run to run, so I'll make the debug check scan the whole merged tree and describe every node with a bad parent:

## 🤖 Claude

The script produced no output because `conda run` doesn't pass stdin through. I'll write it to a file and run it from there:

## 🤖 Claude

The full pipeline works with `std::thread` (clean build in 6.6 s, all 15 cases VALID). Now the README:

## 🤖 Claude

I compared AstraMedium with Ultracode and built [M11502102_ClaudeOpus55Ultracode_2/](M11502102_ClaudeOpus55Ultracode_2/README.md). The new version is better than both, but only by about 0.6% on average. The clear gain (about 1.4%) is on large cases where the buffer length limits are short compared with the chip.

**AstraMedium vs Ultracode: tied**

AstraMedium is built on top of my Ultracode and changes about 200 lines:
- an exact whole-tree position evaluation, used in only about 1% of moves;
- a faster calculation of where two diamond-shaped distance ranges intersect, which only speeds things up and doesn't change scores;
- saving the best solution on every improvement.

On 63 test cases (default settings) it scores 1.0001 against Ultracode (23 better, 19 worse); at an equal 5 s it's 0.997. That matches its own report ("about even on new tests").

**What Ultracode_2 changes:** the grading VM has 2 cores but both earlier versions use only one.
- **Splitting the work (large cases with short length limits):**
  - The tree is cut near the top into two groups of subtrees, and each thread may only change its own group; a check on every move enforces this.
  - At sync points the two halves are merged and fully re-verified.
  - Every other round, and in the final 30%, both threads work on the whole tree again, so the frozen top levels still get optimized for skew.
- **Other cases:** two chains run from the same solution, and the better one continues each round.
- Sync points continue the cooling instead of restarting it; my first version restarted it and gave no gain at all.

**Results** (ratio of Score, lower is better; validation sets weren't used for tuning, and holdout3 was generated at the very end):

| Class | Cases | _2 / Ultracode | _2 / AstraMedium |
|---|---:|---|---|
| All | 63 | **0.9941** (35 better / 13 worse) | **0.9939** (36 / 16) |
| Large, short length limit (uses splitting) | 8 | **0.9865 (8 of 8 better)** | 0.9894 (7 / 1) |
| Large, long length limit | 20 | 0.9970 | 0.9986 (about even) |
| Under 1500 sinks | 35 | 0.9941 | 0.9923 |

- **Very large cases:** 20k / 50k / 100k sinks score 57958 / 140844 / 299803, versus 58757 / 141704 / 302769 for Ultracode. 100k takes 30.2 s and 129 MB.
- **Validity:** the 300 extreme cases are all VALID in both 2-thread and forced 1-thread mode. The local grading pipeline (zip → build → verifier) is all VALID.

**Why the gain is small:** small and medium cases have already converged within the default time; very long runs (180 s) from different seeds land on the same scores. So the versions differ by about ±0.5% there, which is noise. Better scores there would need new kinds of moves, not more search.

Splitting is switched on by a rule (at least 1500 sinks, and max length limit ≤ 0.65 × chip size) that I fitted on 10 large cases, and it held on the later tests. The README lists the attempts that didn't help, with their numbers.

**Two things to know before submitting:**
- The program uses 2 threads via `std::thread`. That's part of the C++ standard library, but the TA said "standard library" in general terms and never mentioned threads. If you want to avoid any risk, `CBI_THREADS=1` runs it single-threaded, which is about the same as Ultracode.
- The folder name must be exactly `M11502102` when you submit, without `build/`. AstraMedium and Ultracode weren't modified.

## 🧑 我

@"/home/evan/Desktop/EDA/brief/"
那請你 對比 M11502102_Chatgpt6AstraUltra 與 參考brief FLUTE: Fast Lookup Table Based Rectilinear Steiner Minimal Tree Algorithm
(看能不能做得更好 不能就不要用)
再創一個的檔案叫 M11502102_ClaudeOpus55Ultracode_3 寫一份有更好的演算法的程式

## 🤖 Claude

Let me first look at what's there: the AstraUltra project and the FLUTE paper. I'll also check the clock-tree lecture (`L04_clkpwr.pdf`), since it's directly on topic.

## 🤖 Claude

No difference. So the number of restarts isn't the issue. Before chasing the 180 s gap, let me check that it's real by putting all budgets side by side on the cases where it showed up:

## 🤖 Claude

Back to the exact `_2` code. Now I'll add the four candidates behind experiment switches so each can be measured on its own:

## 🤖 Claude

Builds. Ablations on the tuning set (default time) for capture, diversify and Hanan seeds, each compared with `_2`:

## 🤖 Claude

About 18% more moves per second on case 018. The question that matters is quality. Tuning set at default time, against `_2`:

## 🤖 Claude

All VALID. 100k sinks: 45.2 s, 137 MB, Score **296820** (`_2`: 299803, Ultracode: 302769). For completeness, AstraUltra on the same three very large inputs, run in parallel:

## 🤖 Claude

The final runs take about half an hour and are running in the background. Meanwhile I'll add holdout5 to the comparison script and write `_3`'s README (final numbers go in once the runs finish):

## 🤖 Claude

holdout5 is done for all three programs (all VALID); the `_3` re-runs on the other sets are still going. holdout5 results first:

## 🤖 Claude

I'll wait for the background re-runs to finish before running anything else, so the timings aren't disturbed:

## 🤖 Claude

Three of the four sets are still running. I'll wait for the background job to finish:

## 🤖 Claude

I'm waiting for the last re-runs (holdout3, holdout4 and testcases) of the final `_3`. When they finish I'll aggregate all 111 cases, re-measure the three very large inputs with the final code, and run the full grading pipeline.

## 🤖 Claude

I built [M11502102_ClaudeOpus55Ultracode_3/](M11502102_ClaudeOpus55Ultracode_3/README.md). On 111 test cases it beats both Ultracode_2 and AstraUltra, but only slightly overall (0.4% and 0.2%). The real gains are on large cases; small and medium cases are tied.

**AstraUltra vs Ultracode_2: tied (0.9983).** AstraUltra is built on my Ultracode_2. Its three main ideas, each tested separately:
- Saving any candidate that sets a new best Score, even if annealing rejects it: no gain when I added it to my code (1.0023).
- Keeping chain 0, chain 1 and the merged tree as three candidates: sound and harmless, so I adopted it.
- Its FLUTE-inspired construction candidates: turning them off made it slightly better (0.9947).

**FLUTE: one idea worked.** FLUTE minimizes total wirelength when wires can be shared. Here every buffer connects to each child separately, every branch point is a paid buffer, and skew matters. So its lookup tables and Hanan-grid results don't apply.

| FLUTE idea | Result | Used? |
|---|---|---|
| Hanan-grid points as extra buffer positions | 1.0036, no gain | No |
| AstraUltra's FLUTE-style construction | turning it off was slightly better | No |
| "Try several directions and cut points, keep the best" from its large-net splitting, applied to my grouping step | starting trees 2–3% better; final results on large cases 0.3–1% better | **Yes** |

**What changed in `_3`:**
1. **New grouping cuts:** each split tries 4 directions (x, y and the two diagonals) and 3 cut points.
2. **A flaw fix:** in `_2`, re-placing a buffer always moved it, even when it was already at the best spot. Now it only moves if the new spot is better, and unchanged buffers aren't searched again (about 18% more moves per second).
3. **Three candidates kept after merging** (AstraUltra's idea).
4. **Large cases (≥ 1500 sinks) get 45 s instead of 30 s**, worth about 0.5%. 100k sinks finish in 45.2 s, still 15 s under the limit. If you prefer the old margin, run with `-t 30`.

**Results** (lower is better; each program on its default settings, all outputs checked by the TA verifier):

| Class | Cases | _3 / _2 | _3 / AstraUltra |
|---|---:|---|---|
| All | 111 | **0.9963** (66 better / 24 worse) | **0.9980** (52 / 36) |
| Large, short buffer length limit | 24 | **0.9898** (21 / 2) | 0.9927 |
| Large, long buffer length limit | 36 | 0.9988 (29 / 7) | 0.9985 |
| Under 1500 sinks | 51 | 0.9977 (16 / 15) | 1.0002 (tied) |

- **Fresh set:** holdout5 was generated at the very end and was never used to tune any version. There `_3` scores 0.9954 against `_2` and 0.9951 against AstraUltra.
- **Very large cases:** 20k / 50k / 100k sinks score 57244 / 140454 / 294863, versus AstraUltra's 57606 / 140840 / 299300.
- **Validity:** the 300 extreme cases are all valid, and the full local grading pipeline is all valid.

**Why the gains stop here:** small and medium cases already reach the same results within the default time. 180-second runs still find solutions 3–5% better, but I tried six more ideas to close that gap and none beat the noise: ruin-and-rebuild of subtrees, saving rejected best candidates, diversified restarts, repairing parents right after a merge, independent restarts for tiny cases, and the Hanan-grid points. The README lists each with its numbers. A real gain on small and medium cases would need a different search method, not more tuning of this one.

**Before submitting:** the program uses 2 threads via `std::thread`; set `CBI_THREADS=1` to run single-threaded. The folder must be renamed to exactly `M11502102` and submitted without `build/`. AstraUltra and the other folders weren't modified.

## 🧑 我

幫我閱讀所有檔案，並詳細解釋Lab2 每個小專案的演算法 時間由遠至近

## 🤖 Claude

我正在逐一讀 Lab2 各專案的 README 和原始碼，接著會整理出各版演算法的說明。

## 🤖 Claude

參考解的建構與放置已讀完，接著讀平衡（balance）、退火（optimizer）與 main 流程。

## 🤖 Claude

已讀到 Ultracode_2 的平行分工；接著看 AstraUltra 的 FLUTE 式建樹和 Ultracode_3 的分群改動，然後開始寫說明。

## 🤖 Claude

Lab2 共有 9 個專案資料夾，下面按時間由遠到近說明。時間依據是資料夾修改時間和 git log。這些專案的程式碼大致是兩條路線：

- **路線 A：基本版。** Morton 排序分群，加上 hill climbing（只隨機移動 buffer）。包括 `my_cbi`、`M11502102`、`GPT61Sol`。
- **路線 B：三階段版。** 建構、插繞路 buffer 平衡、模擬退火。從 `claude_reference` 開始，後面每一版都在前一版上改。

```
claude_reference ─► Ultracode ─► AstraMedium ─► Ultracode_2 ─► AstraUltra ─► Ultracode_3
    (10/8)          (10/12 04:54)  (10:07)        (12:16)         (12:57)        (15:06)
my_cbi 骨架 ─► M11502102（空骨架）   GPT61Sol（照 TUTORIAL 基本版寫）
```

---

## 0. 題目重點（所有版本都要處理）

- **輸入：** SRC 和 N 個 sink 的座標、buffer 型號庫（每型號有 fanout 上限 F、總線長上限 L、成本 C）、`w_skew`。
- **要做的事：** 插入 buffer，組成一棵以 SRC 為根的樹。
- **限制：** 每個 parent 的 child 數 ≤ F，且到所有 child 的 Manhattan 距離**總和** ≤ L。
- **到達時間：** sink 的 arrival = 路徑邊長總和，buffer 本身沒有延遲。
- **目標：** **Score = Σ buffer cost + w_skew × (T_max − T_min)**，越小越好。
- **關鍵觀念：** buffer 的位置和 children 決定之後，「最便宜的合法型號」就確定了，所以演算法只需要決定「樹的形狀」和「buffer 的座標」。

---

## 1. `my_cbi` 骨架（10/8）與 `M11502102`（10/12 02:25）

- 這兩個是同一份 229 行的骨架，`diff` 結果完全相同，`readProblem`、`evaluate`、`buildInitial`、`optimize` 都還是 TODO。
- [TUTORIAL.md](TUTORIAL.md) 給的「基本版」演算法就是下一節 GPT61Sol 實際寫出來的內容。
- 骨架的 `main` 已經寫好一個保險：最佳化後如果不合法或分數變差，就退回初始解。

## 2. `claude_reference`（10/8，第一個完整解）

後面每一版都以它為基礎，所以這裡講最詳細。

### 資料結構（`tree.h/.cpp`）：增量計算 + transaction

- **每個節點記錄子樹統計：**
  - `lo` / `hi`：從自己往下到子樹內 sink 的最短 / 最長路徑。
  - `cnt`：子樹內的 sink 數。
  - `s1` / `s2`：latency 的總和與平方和，用來算標準差 σ。
- **整棵樹的值直接從根讀：** `T_max = nd[0].hi`、`T_min = nd[0].lo`。
- **只往上重算：** 修改一個節點後，`refreshUp` 沿祖先往上重算，統計不再改變就停，不用整棵 O(N) 重算。
- **型號自動選：** `recompute` 會挑「能撐住目前 fanout 與總線長」的最便宜型號，並同步更新 `totalCost`。
- **Transaction：** `begin()` 之後的所有修改都記進 journal，包括節點內容、佔用座標、free list。`rollback()` 完整還原，`commit()` 確定修改。所以任何 move 都可以「先試做 → 檢查 → 不行就還原」，全程保持合法。

### Stage 1 建構（`construct.cpp`）：由下往上分群

1. `frontier` 一開始是所有 sinks。
2. 如果 frontier 數量 ≤ SRC 的 F，且總長 ≤ SRC 的 L，就全部接到 SRC，結束。
3. 否則做**遞迴二分**（`partition`）：
   - 沿 bounding box 的長軸排序，切成兩半；
   - 一直切到每群 ≤ Fg 個，且在中位數放 buffer 時總長 ≤ 某型號的 L。
   - Fg 取 `C/(F−1)` 最小的型號的 F。原因是 N 個 sink 大約需要 N/(F−1) 個 buffer，這個比值越小越省。
4. 每群放一個 parent buffer（`placeGroup`），放不下就再切半。新 buffer 成為下一輪的 frontier。
5. 如果一群都合不起來（點彼此太遠），就在離 SRC 遠的節點上方插 **relay buffer**（`relayToward`），往 SRC 方向前進一步，步長取 `C/L` 最便宜型號的 L。
6. 收尾有兩種，兩種都做，最後取好的：
   - attach-early：能接 SRC 就直接接；
   - single-root：先合成單一根，讓 SRC 的子樹在根部就能平衡。

### Buffer 位置（`placement.cpp: findParentSpot`）

- **不是只取中位數。** 它在 children 的 bounding box 內先用 12×12 格點粗搜，保留最好的 3 個起點，再把步長減半，在 5×5 鄰域細搜。
- **目標函數：** `w·(max(d+hi) − min(d+lo)) + 型號 cost`，也就是讓各 child 的 latency 對齊。
- **只搜 bounding box 的理由：** 在 box 外移動時，到每個 child 的距離同步增加，range 不會變好。

### Stage 2 平衡（`balance.cpp`）：插繞路 buffer 拉高 T_min

- **兩個量：** 對節點 v，
  - `slack = T_max − (A_v + hi_v)`：整棵子樹最多還能延後多少；
  - `need = low − (A_v + lo_v)`：要讓子樹最早的 sink 達到目標 `low`，至少要延後多少。
- **找插入點：** 從上往下走，第一個 `need ≤ slack` 的節點，只要插**一個** delay buffer 就能解決整棵子樹。
- **找繞路點（`pointOnDiamond`）：** 繞路點 n 要滿足 `d(n,v)=a`、`d(p,n)=b`，也就是兩個 Manhattan 菱形的交點。
- **奇偶性：** 繞路長度一定是偶數，所以 **sink 的 arrival 奇偶性由 (x+y) 決定，永遠改不了**。sinks 的奇偶不一致時，skew ≥ 1。範例的 37 = 36 + 1，已經碰到這個下限。
- **一次插不夠：** 先插一個盡量長的，剩下的再串一串 buffer。
- **選 `low`：** 掃描 128 個均勻值，加上抽樣的 sink arrival，估計 `插入成本 + w·(T_max − low)`，取最小的方案。

### Stage 3 模擬退火（`optimizer.cpp`）

- **能量函數：** `E = cost + w·(skew + 0.3σ)`。skew 只看最大最小值，大部分 move 改不到它（能量是平的），所以加 σ 提供方向感。最佳解仍然以真正的 Score 記錄。
- **溫度尺度：** 以「最便宜 buffer 的 cost」為單位，指數降溫。
- **7 種 move：**

| Move | 機率 | 作用 |
|---|---|---|
| Shift | 40% | buffer 隨機移動，半徑 1、2、4…（30% 挑關鍵路徑上的） |
| Replace | 10% | 用 `findParentSpot` 移到最佳位置 |
| Reparent | 20% | 節點換到附近 buffer 底下；原 parent 變空就連鎖刪除，省 cost |
| Swap | 10% | 兩個節點交換 parent |
| Dissolve | 7% | 刪掉一個 buffer，children 分給附近的 buffer |
| Pad | 9% | 在太早到的節點上方插繞路 buffer |
| Split | 4% | 把一個 buffer 的部分 children 拉出來給新 buffer |

- **找附近 buffer：** 用空間格網，定期重建。
- **最後驗證：** 每個階段結束都用獨立的 `evaluate()` 從頭驗證，失敗就退回上一階段的解。

## 3. `M11502102_GPT61Sol`（10/12 03:32）：基本版

就是 TUTORIAL 的 Milestone 3 + 4，寫在單一檔案 [main.cpp](M11502102_GPT61Sol/src/main.cpp)，共 998 行。

- **建構：**
  - frontier 用 **Morton（Z-order）key** 排序，也就是把 x、y 的 bit 交錯，讓空間上相鄰的點排在一起。
  - 從左到右貪心地加點，直到放不下 buffer。放 buffer 的方法是從中位數往外最多 8 圈找空位。
  - 一輪都合不起來，就插 relay 往 SRC 靠近。
- **最佳化：** 純 hill climbing。隨機挑一個 buffer、隨機移動半徑 1～32，**每次整棵樹重算**，合法且 Score 不變差才保留。
- **弱點**（第二次對話的比較結果）：
  - 不會改樹的結構；
  - 沒辦法讓太早到的 sink 變晚；
  - 整棵重算很慢；
  - 找空位時要掃過所有節點，10 萬個 sinks 要 55 秒；
  - 建樹卡住時直接 `exit(1)`，不寫輸出檔。
- **分數：** 平均是 claude_reference 的 1.67～2.11 倍。

## 4. `M11502102_ClaudeOpus55Ultracode`（04:54）

以 claude_reference 為基礎，分析後發現 buffer cost 佔 Score 的 90～99%，而且搜尋速度是瓶頸。

- **4 種新 move，加上 Reparent 新增 relocate 版本，共 11 種：**
  - **Merge：** 兩個鄰近 buffer 合成一個，或把 buffer child 收進 parent，直接省一個 buffer。先用「中位數總線長 > 最大 L」快速排除不可能的組合。
  - **Rebalance：** 兩個鄰近 buffer 的 children 沿兩者的連線排序，枚舉切點重新分成兩群，再各自重新放置。
  - **Retarget：** 關鍵路徑上的 buffer 以「絕對目標 T_max」重新放置。超過目標的懲罰是不足的 2 倍。
  - **SkewRepar：** 關鍵路徑上的節點換到「arrival 最合適」的鄰近 buffer 底下，不加 buffer 就能平衡 skew。
  - **Reparent 的 relocate 版：** 換完 parent 後，新舊 parent 都重新放置。
- **速度約 4 倍**（每秒 70～90 萬個 move）：
  - 佔用表從 hash set 改成 bitmap；
  - `searchSpot` 改成 template，讓評估函式 inline；
  - 最佳化時用便宜的 `fastSearch`（只保留 1 個起點、3×3 鄰域）。
- **時間上限：** 改成 `10 + N/100` 秒，最多 30 秒。
- **效果：** 比第一版好約 2.7%，約是 GPT61Sol 的 0.46～0.58 倍。

## 5. `M11502102_Chatgpt6AstraMedium`（10:07）

以 Ultracode 為基礎，只改約 200 行。

1. **全樹精確的 Retarget：**
   - 沿祖先收集兄弟子樹的最早 / 最晚 arrival，得到「不受這個 buffer 移動影響」的範圍。
   - 每個候選座標只要 O(fanout) 就能算出**全樹**的 skew 和 σ，成本也計入 parent 型號的變化。
   - 這種評估只佔 Retarget 的 25%，其餘 75% 用原版方法。
2. **`diamond.h`：** 用解析解求兩個 Manhattan 菱形的交點，取代逐點掃描。複雜度與半徑無關，選出的點和原版相同，所以只加速、不改變分數。
3. **立即保存最佳解：** 每次 Score 創新低就存一次。原版是每隔一段操作才存。

結果和 Ultracode 打平（1.0001）。

## 6. `M11502102_ClaudeOpus55Ultracode_2`（12:16）：平行化

評分 VM 是 2 核心，但前面的版本都只用 1 核心。

- **分工模式**（N ≥ 1500 且最大 L ≤ 0.65 × 晶片邊長時啟用，`parallel.cpp`）：
  1. `makeCut`：從 SRC 往下，反覆展開 sink 最多的節點，直到每棵子樹 ≤ N/3 個 sink。上層節點凍結。
  2. 剩下的子樹根沿 x 或 y 排序，按 sink 數分成兩組，兩條執行緒各負責一組。
  3. 每個 move 用 transaction journal 檢查有沒有碰到別組的節點，越界就 rollback。
  4. `mergeCut`：每個 epoch 結束把兩半合併，新 buffer 重新編號，再整棵重新驗證。
  5. 奇數 epoch 和最後 30% 的時間，兩條鏈都看整棵樹，因為上層凍結時 skew 修不好。
- **其他測資：** 兩條鏈用不同 seed 從同一個解出發，每個 epoch 取較好的那條繼續。
- **降溫不中斷：** 同步之後接著原本的溫度繼續降，不重新加熱。重新加熱的版本實驗上完全沒有幫助。
- **效果：** 整體好 0.6%，大且線長短的測資好 1.4%（8 筆全勝）。

## 7. `M11502102_Chatgpt6AstraUltra`（12:57）

以 Ultracode_2 為基礎。

1. **保存所有創新低的候選：** 只要合法候選的 Score 創新低就存，即使退火因為 σ 拒絕了它。用 sparse checkpoint，只複製有變動的節點。
2. **合併後保留三個候選：** chain 0、chain 1、合併解分別驗證，取最好的。
3. **FLUTE 啟發的分群**（`construct_spatial.cpp`）：
   - 試 x、y 兩個方向和多個切點，用「每群平均最小總線長」篩選；
   - 底層最多試 6 個候選，實際建出兩層子樹，算成本與 skew 後 rollback，挑最好的。
   - 它沒有套用 FLUTE 的查表或 RSMT 最佳解，原因是本題不共用線段，而且每個分支點都是要付錢的 buffer。
4. **精確座標搜尋**（`exact_placement.cpp`）：已實作並測試過，但沒有得分收益，預設關閉。

結果約和 Ultracode_2 打平（0.998）。

## 8. `M11502102_ClaudeOpus55Ultracode_3`（15:06，目前最強）

以 Ultracode_2 為基礎，逐項測過 AstraUltra 和 FLUTE 的想法後：

1. **多方向、多切點分群**（借用 FLUTE net breaking 的「試多種切法、取最好」）：
   - 每一刀試 4 個方向：x、y、x+y、x−y。對角線方向在 Manhattan 距離下也重要。
   - 每個方向試 3 個切點：平均切點，以及前後各移一群。
   - 最後一刀直接比兩群的實際型號成本；上層則比兩半的外框半周長總和，越小越緊密。
   - 起點的 Score 好 2～3%。
2. **修正「重新放置一定會移動」的問題：** 以前 buffer 自己的座標被當成已佔用，所以即使原地就是最佳也會被移走。現在只在新位置更好時才移動，並用 children 的指紋快取上次的結果。每秒 move 數約多 18%。
3. **採用 AstraUltra 的三候選合併。**
4. **大測資（N ≥ 1500）的時間上限從 30 秒放寬到 45 秒。**

測過但沒採用的：Hanan grid 起點、AstraUltra 的 FLUTE 式建樹、保存被拒絕的最佳候選、大鄰域重建（ruin-and-rebuild）。

結果：111 筆測資上，比 Ultracode_2 好 0.37%，比 AstraUltra 好 0.2%。10 萬個 sinks 的 Score 是 294863。

## 9. `my_cbi` 目前狀態（最新）

- HEAD（commit a523927）裡的 `my_cbi/src/main.cpp` 和 GPT61Sol **完全相同**。
- 工作目錄裡它已被改回空骨架，還沒 commit。看起來你正準備自己從頭寫。

---

## 演進重點

| 版本 | 主要貢獻 | 相對前一版 |
|---|---|---|
| GPT61Sol | Morton 分群 + hill climbing | 基準（最弱） |
| claude_reference | 增量樹 + transaction、bounding box 位置搜尋、繞路平衡、7 種 move 的退火 | 約好 40～50% |
| Ultracode | 4 種新 move + 搜尋快 4 倍 | 約好 2.7% |
| AstraMedium | 全樹精確 Retarget、解析菱形交點 | 持平 |
| Ultracode_2 | 2 執行緒分工 / 雙鏈 | 約好 0.6% |
| AstraUltra | 保存創新低候選、FLUTE 式分群 | 持平 |
| Ultracode_3 | 4 方向 × 3 切點分群、修正重新放置、大測資 45 秒 | 約好 0.4% |

結論：真正的大躍進只有兩次。一次是 GPT61Sol → claude_reference，原因是演算法本身變了（可以改結構、可以插繞路）；另一次是 Ultracode 的新 move 加上提速。之後各版的差距都在 1% 以內，小、中型測資基本上已經收斂。

**兩件要注意的事：**
- **Ultracode_3 的 README 過期了。** [M11502102_ClaudeOpus55Ultracode_3/README.md](M11502102_ClaudeOpus55Ultracode_3/README.md) 在 commit a523927 被換成 Ultracode_2 的內容，寫的是 30 秒上限和 Ultracode_2 的數據。但程式碼（[main.cpp:129](M11502102_ClaudeOpus55Ultracode_3/src/main.cpp:129)）實際用的是 45 秒。正確的版本在 commit bec91b1，要交這版的話記得還原。
- **報告可用的觀察：** 奇偶性下限（skew ≥ 1）、buffer cost 佔 Score 90% 以上，都適合寫進報告。

## 🧑 我

進入 Conda 環境 指令

## 🤖 Claude

要進入這次建的 `eda` 環境，執行：

```bash
conda activate eda
```

離開環境：

```bash
conda deactivate
```

如果出現 `conda: command not found`，或提示要先執行 `conda init`，先跑這行（只需要做一次），然後重開 terminal：

```bash
conda init bash
```

`eda` 環境裡有 Python 3.12、cmake 3.31 和 matplotlib。評分機上不需要這個環境，只有本機編譯和跑 `tools/` 裡的 Python 工具時才要啟用。

## 🧑 我

Ultracode_3 的 README 過期了。 M11502102_ClaudeOpus55Ultracode_3/README.md 在 commit a523927 被換成 Ultracode_2 的內容，寫的是 30 秒上限和 Ultracode_2 的數據。但程式碼（main.cpp:129）實際用的是 45 秒。正確的版本在 commit bec91b1，要交這版的話記得還原。

幫我改正確

## 🤖 Claude

我已經從 commit bec91b1 把 [M11502102_ClaudeOpus55Ultracode_3/README.md](M11502102_ClaudeOpus55Ultracode_3/README.md) 還原成正確版本，現在和程式碼一致：

- **時間上限：** N ≥ 1500 時最多 45 秒，和 [main.cpp:129](M11502102_ClaudeOpus55Ultracode_3/src/main.cpp:129) 相同。
- **改進說明：** 寫的是相對 Ultracode_2 的四項改進：多方向切分、修正「重新放置一定會移動」、三候選合併、45 秒上限。
- **其他內容：** 也包含 Ultracode_3 自己的實驗數據、FLUTE 的比較表，以及環境變數 `CBI_PART`。

這個修改已經 stage，但還沒 commit。要提交的話：

```bash
git commit -m "Restore Ultracode_3 README" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

工作目錄裡另外還有你原本未 commit 的修改：`.gitignore`、`conversation.md`、`my_cbi`。這些我都沒動，上面的指令也不會把它們一起提交。
