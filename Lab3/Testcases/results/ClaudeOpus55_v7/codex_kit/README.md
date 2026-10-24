# codex_kit：讓 Codex 接著做 v7 之後的下一版（一個指令跑完）

## 怎麼跑

```bash
cd Lab3
bash Testcases/results/ClaudeOpus55_v7/codex_kit/run_codex.sh CodexAstra
```

`CodexAstra` 是新版本的資料夾名，可以換。跑在背景，關掉終端機也會繼續；看進度（不花 token）：

```bash
bash Testcases/results/ClaudeOpus55_v7/codex_kit/codex_status.sh CodexAstra
```

中途停了（使用量上限以外的錯誤、保護資料夾被改等，原因寫在 `Testcases/results/CodexAstra/orchestrator.log`），處理完**重跑同一個指令**就會從停下的地方接著做。

## 流程與時間

| 階段 | 誰 | 大約時間 |
|---|---|---|
| 準備：建 `AI_Solutions/<NEW>/`（複製 v7）、複製工具、記保護資料夾 sha256 | 協調者 | 秒 |
| A：量測（M1–M3 中大型設計、SA 速度與 gprof 熱點、預設 HPWL 協定的雜訊） | 協調者 | 1.7 小時 |
| A2：寫 diagnosis.md | Codex 1 個 session | 短 |
| B：每個 session 一輪「改程式 → 排訓練 → 結束」，下一個 session 判定保留或撤回；最多 12 + 2 個 session | Codex | 每輪 0.3–3 小時計算 |
| 沙箱外完整 ASan/UBSan | 協調者 | 數分鐘 |
| C：凍結 + 最終驗證（27、L1/L2 × 3 seeds、M1–M3、edge_seed8、三種 HPWL 協定 + v7 雜訊對照） | 協調者 | 約 6.5 小時 |
| D：README、COMPARISON 第 14 節、HISTORY 加列、edge 表 | Codex 1 個 session | 短 |

整體大約 15–25 小時，視 B 的輪數而定。

## 為什麼省 token

- 等待都在 shell 裡（協調者），Codex 不查進度；`codex exec` 每個 session 都是新的、只讀 HANDOFF 指定的節與 WORK_STATE 最後一節。
- 不需要判斷的事（建資料夾、量測、凍結、最終驗證、核對 sha256）都由協調者做，不開 Codex session。
- 量測腳本、訓練腳本、最終驗證與報告腳本都已寫好並乾跑過（`DRY_RUN=1`），Codex 不必再寫或除錯工具。
- 遇到使用量上限，協調者等到重置時間再重跑同一個 session。

## 安全欄

- Codex 寫的 `RUN:` 只接受 `python3 Testcases/results/<NEW>/scripts/<腳本>.py 參數…`，其他一律拒絕並告訴下一個 session。
- 每個 session 結束後核對 9 個保護資料夾（含 v7）的 sha256，有變就停。凍結後的原始碼在最後再核對一次。
- 沒有 commit、沒有 AI 署名（提示詞與 HANDOFF 都寫了）。

## 模型與思考強度

預設用 `~/.codex/config.toml` 的模型（目前是 `gpt-6-astra`）。階段 B 預設 `model_reasoning_effort="high"`，A2 / D 用設定檔的值。要換：

```bash
CODEX_MODEL=<模型 id> CODEX_EFFORT_B=high CODEX_EFFORT=medium bash Testcases/results/ClaudeOpus55_v7/codex_kit/run_codex.sh CodexAstra
```

## 檔案

| 檔案 | 用途 |
|---|---|
| `AI_Solutions/ClaudeOpus55_v7/HANDOFF.md` | Codex 唯一要讀的交接文件（規則、工具、v7 現況、已確認事實、不要再試的方向、目標 T1–T4、保留條件、最終驗收） |
| `prompts/*.md` | 各階段的提示詞（`_footer.md` 附在每一個後面：RUN/WAIT 規則、輸出長度、編譯位置） |
| `orchestrate_codex.py` / `run_codex.sh` / `codex_status.sh` | 協調者、啟動、看進度 |
| `phaseA_queue.py`、`phaseA_report.py` | 階段 A 的量測與摘要 |
| `phase2_train.py`、`phase2_large.py`、`phase2_sa_speed.py`、`rep_queue.py`、`tests_queue.py` | 階段 B 的配對訓練（對同輪 v7） |
| `final_validate.py`、`final_report.py`、`hpwl_compare.py` | 最終驗證與摘要 |
| `v6q.py`、`compare_lg.py`、`stage1_phases.py` | 共用（串行佇列、timing.lock、`.done.json`） |
