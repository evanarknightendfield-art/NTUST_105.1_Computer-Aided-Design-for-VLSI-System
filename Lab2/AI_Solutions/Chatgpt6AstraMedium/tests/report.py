#!/usr/bin/env python3
"""Summarize completed benchmarks without selecting only favorable seeds."""
import csv,math
from pathlib import Path
project=Path(__file__).resolve().parents[1]
text=['# 與 ClaudeOpus55Ultracode 比較','', '兩版由相同 GCC / Release 設定編譯；每次總時間預算 3 秒，依序執行，不以平行跑分爭用 CPU。所有輸出由課程 verifier 驗證，stdout 分數亦須一致。既有測資 seed 為 1、2；最終新產生測資 seed 為 1。','', '比值 = 新版 Score / 原版 Score，低於 1 較好。下表列每組所有「測資 × seed」配對，不挑最佳 seed。', '', '| 測資 | 配對數 | 勝 / 平 / 負 | Score 幾何平均比值 | 平均秒數（原 / 新） |','|---|---:|---:|---:|---:|']
groups=[]
for name, folders in [('既有測資', ['hybrid_pilot','hybrid_repeat']), ('新測資', ['final_holdout_3s'])]:
    rows=[]
    for folder in folders:
        rows.extend(csv.DictReader((project/'experiments'/folder/'results.csv').open()))
    groups.append((name,rows))
    win=sum(int(r['candidate'])<int(r['baseline']) for r in rows)
    lose=sum(int(r['candidate'])>int(r['baseline']) for r in rows)
    ratio=math.exp(sum(math.log(float(r['ratio'])) for r in rows)/len(rows))
    bt=sum(float(r['baseline_seconds']) for r in rows)/len(rows)
    ct=sum(float(r['candidate_seconds']) for r in rows)/len(rows)
    text.append(f'| {name} | {len(rows)} | {win} / {len(rows)-win-lose} / {lose} | {ratio:.6f} | {bt:.4f} / {ct:.4f} |')
text += ['', '## 逐筆結果','', '每格是依 seed 順序排列的原始 Score。','']
for name, rows in groups:
    text += [f'### {name}', '', '| 測資 | 原版 Scores | 新版 Scores |','|---|---|---|']
    for inp in sorted({r['input'] for r in rows}):
        selected=sorted([r for r in rows if r['input']==inp], key=lambda r:int(r['seed']))
        text.append(f'| {inp} | '+', '.join(r['baseline'] for r in selected)+' | '+', '.join(r['candidate'] for r in selected)+' |')
    text.append('')
text += ['## 解讀限制','', '- 既有測資及第一組 holdout 曾用於開發，不能視為獨立保留集。', '- 最終新測資（final_holdout_inputs）由 `tests/make_holdout.py` 的固定 seed 產生；在演算法定版前沒有查看其比較分數，未根據這組結果再調整演算法。', '- `pilot`、`polish_pilot`、`testcases_3s`、`holdout_3s` 是已放棄版本的紀錄；本報告使用 hybrid_pilot + hybrid_repeat 及 final_holdout_3s。', '- 這是 3 秒預算的結果，不外推成預設 5–30 秒或所有未知測資都更好，也沒有全域最優保證。', '- 時間制退火受到 CPU 負載及排程影響，同 seed 可能有些微差異。來源 SHA-256 與編譯器記錄在 `experiments/manifest.json`。','']
(project/'COMPARISON.md').write_text('\n'.join(text))
print('\n'.join(text[:12]))
