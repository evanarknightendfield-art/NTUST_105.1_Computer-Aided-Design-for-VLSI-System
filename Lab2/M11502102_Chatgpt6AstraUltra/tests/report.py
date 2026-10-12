#!/usr/bin/env python3
import csv,math
from pathlib import Path
p=Path(__file__).resolve().parents[1];e=p/'experiments'
def read(name):return list(csv.DictReader((e/name/'results.csv').open()))
def gm(xs):return math.exp(sum(math.log(x) for x in xs)/len(xs))
def comparison(rows):
 ratios=[float(r['candidate'])/float(r['baseline']) for r in rows]
 w=sum(float(r['candidate'])<float(r['baseline']) for r in rows);t=sum(float(r['candidate'])==float(r['baseline']) for r in rows)
 return len(rows),w,t,len(rows)-w-t,gm(ratios)
hold=read('holdout_5s');held=[]
for name in sorted({r['input'] for r in hold}):
 rs={r['variant']:r for r in hold if r['input']==name}
 held.append({'input':name,'seed':'1','baseline':rs['baseline']['score'],'candidate':rs['spatial_core']['score']})
groups=[('既有測資，5 秒',read('testcases_5s')),('新測資，5 秒',held),('10 萬 sinks，30 秒',read('large_30s'))]
lines=['# 與 M11502102_ClaudeOpus55Ultracode_2 比較','', '兩版使用相同 GCC / Release 設定，固定兩個 CPU 核心及兩條執行緒，依序跑分；seed=1。正式結果都由課程 verifier 獨立驗證，且 stdout 分數一致。Score 越低越好。','', '| 組別 | 比較數 | 勝 / 平 / 負 | 新版 / 基準 Score 幾何平均 |','|---|---:|---:|---:|']
for name,rows in groups:
 n,w,t,l,r=comparison(rows);lines.append(f'| {name} | {n} | {w} / {t} / {l} | {r:.6f} |')
lines+=['','## FLUTE 是否值得採用','', '借用 FLUTE 高階 net breaking 的多方向、多切點思路，並以 CBI 的實際 Score 選建樹候選；沒有實作 FLUTE lookup table，亦不引用其 RSMT 最優性當成本作業保證。','', '同一組 12 筆新測資，關閉分群候選的核心版本與最終組合都有測試：','', '| 版本 | 相對基準 Score 幾何平均 |','|---|---:|']
for variant in ['core','spatial_core']:
 ratios=[]
 for name in sorted({r['input'] for r in hold}):
  d={r['variant']:float(r['score']) for r in hold if r['input']==name};ratios.append(d[variant]/d['baseline'])
 lines.append(f'| {"核心修改，關閉分群候選" if variant=="core" else "核心修改＋FLUTE 啟發分群（最終版）"} | {gm(ratios):.6f} |')
lines+=['', '因此保留多候選分群。它對建樹階段採取不比原始起點差才替換的規則；因後续搜尋路徑及時間會變動，最終 Score 仍可能退步。','', '## 開發集消融實驗','', '8 筆測資，各 5 秒。這組用於選擇方法，不能當成獨立泛化證據。','', '| 版本 | 新版 / 基準 Score 幾何平均 |','|---|---:|']
a=read('ablation_5s');a+=read('spatial_core_5s')
for variant,label in [('core','核心修改'),('line','核心＋精確座標搜尋'),('spatial','核心＋精確座標＋候選分群'),('spatial_core','核心＋候選分群（採用）')]:
 ratios=[]
 for name in sorted({r['input'] for r in a}):
  d={r['variant']:float(r['score']) for r in a if r['input']==name};ratios.append(d[variant]/d['baseline'])
 lines.append(f'| {label} | {gm(ratios):.6f} |')
lines+=['','精確座標搜尋通過正確性測試，但得分收益不足，預設停用；可用 CBI_EXACT_WEIGHT 重現研究。','', '## 逐筆正式比較','']
for name,rows in groups:
 lines += [f'### {name}','','| 測資 | 基準 | Ultra | 比值 |','|---|---:|---:|---:|']
 for r in rows: lines.append(f'| {r["input"]} | {r["baseline"]} | {r["candidate"]} | {float(r["candidate"])/float(r["baseline"]):.6f} |')
 lines.append('')
lines += ['## 正確性與限制','', '- C++17 Release 編譯成功。UBSan 的三項單元／性質測試全部通過。', '- 128,000 次隨機交易，58,235 次 sparse checkpoint 與完整 snapshot 精確對照。', '- 250,000 組菱形解析求交對照；4,386 次精確座標最佳 Score 對照（該 move 預設停用）。', '- 單／雙執行緒 sanitizer solver 與 stage dump 的獨立驗證紀錄見 experiments/validation.txt。', '- 這是啟發式程式，不保证全域最優、每筆測資更好或同 seed 每次完全相同。', '- 固定 5 秒的比較不能直接外推到全部預設 5–30 秒條件；10 萬 sinks 另以兩版皆 30 秒比較。', '- 新測資未用於調整切點、move 權重或溫度；用來確認是否保留候選分群，因此亦不宣稱是盲測排行榜成績。', '- 原始輸出、stderr、CSV、測資產生 seed 及來源 SHA-256 均保留於 experiments/。', '']
(p/'COMPARISON.md').write_text('\n'.join(lines));print('\n'.join(lines[:11]))
