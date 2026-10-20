#!/usr/bin/env python3
"""Publish completed measurement summaries without removing historical data."""
import csv,json,math,re,shutil,statistics,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];LAB=ROOT.parents[1];OUT=LAB/'Testcases/results/CodexChainDP'

def read(p):return list(csv.DictReader(p.open()))
def gm(xs):return math.exp(statistics.mean(math.log(x) for x in xs))
def pct(r):return f'{(r-1)*100:+.2f}%'
def num(v):return f'{v:,.4f}'.rstrip('0').rstrip('.')

def main():
    if 'FINAL VALIDATION COMPLETE' not in (OUT/'final_progress.log').read_text():raise SystemExit('Validation still running')
    subprocess.run([sys.executable,str(ROOT/'tests/summarize.py')],stdout=subprocess.DEVNULL,check=True)
    stats=json.loads((OUT/'summary.json').read_text());pairs=read(OUT/'default_comparison.csv')
    bybench={r['bench']:r for r in pairs}
    default=read(OUT/'default/record.csv');edge=read(OUT/'edge_CodexChainDP/record.csv')
    if len(default)!=11 or len(edge)!=16:raise SystemExit('Incomplete default results')
    for exp,count in [('equal_total_time',144),('fixed_work_holdout',48),('fixed_work_edge',96),('equal_time_default',144),('baseline_seed2',16)]:
        rows=read(OUT/exp/'record.csv')
        if len(rows)!=count or any(r['verdict']!='PASS' for r in rows):raise SystemExit('Incomplete comparison '+exp)
    allstats=stats['all'];l1=stats['L1'][0];fresh=stats['fresh_edge']
    two_rows=next(r for r in read(OUT/'fixed_work_summary.csv') if r['bench']=='e07_tworows')
    lines=['## 正式驗收（2026-10-20，演算法凍結後）','',
           '原始紀錄：[預設比較 CSV](../../Testcases/results/CodexChainDP/default_comparison.csv)、[完整彙總](../../Testcases/results/CodexChainDP/summary.json)。v4 與 ChainDP 都以 local_grade.py 打包、重新編譯、獨立驗證；預設只加 `-v` 診斷。', '',
           '| 分組 | 筆數 | 最大位移幾何平均 ÷ v4 | 總位移幾何平均 ÷ v4 | v4 / ChainDP 合計秒數 |','|---|---:|---:|---:|---:|']
    for group in ['testcases','holdout','edge','all']:
        s=stats[group];lines.append(f"| {group} | {s['cases']} | {s['max_geomean']:.6f}（{pct(s['max_geomean'])}） | {s['disp_geomean']:.6f}（{pct(s['disp_geomean'])}） | {s['v4_seconds']:.2f} / {s['chain_seconds']:.2f} |")
    regress=allstats['max_regressions']
    lines+=['',f"逐筆最大位移比 v4 差的測資：{', '.join(regress) if regress else '無（27/27 不增加）'}；其中 {sum(float(r['max_ratio'])<1-1e-8 for r in pairs)} 筆嚴格下降。總位移比較排除兩筆 0/0，其餘 25 筆；最大位移亦同。預設 HPWL 是 seed 1 的驗收數字，不能獨自當成進步證據。",'',
            '| bench | v4 最大位移 | ChainDP 最大位移 | 總位移變化 | 最大位移變化 |','|---|---:|---:|---:|---:|']
    for r in pairs:lines.append(f"| {r['bench']} | {num(float(r['v4_max']))} | {num(float(r['chain_max']))} | {pct(float(r['disp_ratio']))} | {pct(float(r['max_ratio']))} |")
    lines+=['','這張表逐筆列出總位移／最大位移取捨。預設的 1.01 門檻是相對 global 修復前的 incumbent；最後仍以獨立重跑的 v4 數據判斷整體驗收。','',
            '### 精確最佳值與剩餘差距','',
            '| bench | 本次最大位移 | 精確最小值 | 高於最小值 |','|---|---:|---:|---:|']
    for b,optimum in [('toy',1938.4),('h01',26.5)]:
        x=float(bybench[b]['chain_max']);lines.append(f'| {b} | {num(x)} | {optimum} | {pct(x/optimum)} |')
    near=float(bybench['e01_nearlegal']['chain_max'])
    lines+=['','已重新執行原始 `Grader/tools/exact_bound.py --max --max-ilp --time-limit 600`；[證明紀錄](../../Testcases/results/CodexChainDP/exact_max/record.csv)。整數模型的最小值與啟發式的可行結果分開報。',
            f'e01_nearlegal 的最大位移是 {near}，比產生器已知合法解的 5.4 高 {pct(near/5.4)}；5.4 是可行解，沒有宣稱它是精確最小值。','',
            '### HPWL：固定次數與固定總預算分開看','',
            '| 實驗 | 分組 | 三 seed 平均的 HPWL 幾何平均 ÷ v4 | 平均相對比率 |','|---|---|---:|---:|']
    for name,label in [('fixed_work','固定 500 萬 proposal'),('equal_time_default','相同總預算／預設 SA 排程'),('equal_total_time','相同總預算／10 億次排程壓測')]:
        for group in ['holdout','edge']:
            s=stats[name][group];lines.append(f"| {label} | {group} | {s['hpwl_geomean']:.6f}（{pct(s['hpwl_geomean'])}） | {s['mean_ratio']:.6f} |")
    lines+=['',
            '每一筆、每個版本的三 seed **平均與 [最小, 最大]**：[固定次數詳表](../../Testcases/results/CodexChainDP/fixed_work_table.md)、[同總預算詳表](../../Testcases/results/CodexChainDP/equal_time_default_table.md)。逐次原始紀錄保留實際 SA attempts、SA 秒數與總記帳秒數。',
            f"兩 row 的 e07：固定次數三 seed 平均 v4 {num(float(two_rows['v4_mean']))}、ChainDP {num(float(two_rows['chain_mean']))}（{pct(float(two_rows['ratio']))}）；範圍重疊，這筆沒有證據顯示 move 品質改善。",
            '', '固定次數用共同合法起點、單執行緒，隔離 move 品質；同總預算用各自的 Stage 1 placement、自動執行緒，計入 Stage 1 代價。它們不是同一個實驗，不能混成單一進步數字。固定次數的 holdout 初始解來自開發期快照，edge 初始解來自凍結後 ChainDP 的預設 Stage 1；同一測資的兩版本起點完全相同。fixed-only 案例兩版本均無 SA move，實際 attempts 為 0。三 seed 的範圍重疊時，不宣稱統計顯著或一般性優勝。','',
            '原 10 億次排程會讓部分短時限的大設計停在第一個高溫階段；全部數據保留為壓力測試，不能代表預設 SA。補充實驗只恢復 SA 的預設 effort，沒有改 solver。詳見 [排程檢查](../../Testcases/results/CodexChainDP/schedule_audit/README.md) 與 [壓測三 seed 詳表](../../Testcases/results/CodexChainDP/equal_total_time_table.md)。共同時間上限不保證實際用時相等；小設計可能先做完預設次數，詳表列出實際記帳秒數。','', '### 穩健、時限與最後的新 seed','',
            f"- 標準 27 筆：ChainDP {sum(r['verdict']=='PASS' for r in default+edge)}/27 PASS；L1：{l1['verdict']}，{float(l1['seconds']):.2f} 秒（local_grade 外部 timeout 240 秒）。",
            f"- L1 預設輸出：總位移 {num(float(l1['disp']))}、最大位移 {num(float(l1['max']))}、SA 後 HPWL {num(float(l1['hpwl_sa']))}（seed 1；只作驗收，不宣稱多 seed 品質進步）。",
            f"- 新 seed 20261020（演算法凍結後產生；其後只有量測排程補充，未改演算法）：{sum(r['verdict']=='PASS' for r in fresh)}/{len(fresh)} PASS，資料在 `Testcases/edge_seed2/`；[紀錄](../../Testcases/results/CodexChainDP/edge_seed2/record.csv)。e13、e14 為固定無亂數案例，不把它們宣稱為全新隨機情境。",
            f"- 新 seed 與 v4 的預設比較：最大位移幾何平均 {pct(stats['fresh_comparison']['max_geomean'])}、總位移 {pct(stats['fresh_comparison']['disp_geomean'])}，最大位移退步案例：{', '.join(stats['fresh_comparison']['max_regressions']) or '無'}。[逐筆比較](../../Testcases/results/CodexChainDP/fresh_comparison.csv)。",
            '- 原 seed 的完整檔案重生後逐一 SHA256 比對：[紀錄](../../Testcases/results/CodexChainDP/generator_default_hashes.csv)。原 edge 未被覆寫。',
            f"- e04_sparse10：本次 v4 {float(bybench['e04_sparse10']['v4_seconds']):.2f} 秒，ChainDP {float(bybench['e04_sparse10']['chain_seconds']):.2f} 秒。",
            '- 7 套 C++ 回歸／窮舉測試與 ASan/UBSan 測試通過；最後的離散 L1 median 修正也重新通過 global 回歸的 ASan/UBSan 檢查：[最終紀錄](../../Testcases/results/CodexChainDP/final_global_sanitized/record.csv)。LSan 在 sandbox 的 ptrace 環境不能運作，改到 sandbox 外跑過，沒有隱藏原失敗紀錄。','',
            '### 三個目標的完成程度','',
            f"1. 最大位移：幾何平均 {pct(allstats['max_geomean'])}，總位移 {pct(allstats['disp_geomean'])}；逐筆退步數 {len(regress)}。toy／h01 到精確解的剩餘差距見上表，尚未實作可證明最優的全域 minimax。",
            f"2. HPWL：{'未達標' if any(stats['equal_time_default'][g]['hpwl_geomean']>=1 for g in ['holdout','edge']) else '共同上限下兩組平均下降，但嚴格同耗時優勝尚未證明'}。同總預算的 holdout {pct(stats['equal_time_default']['holdout']['hpwl_geomean'])}、edge {pct(stats['equal_time_default']['edge']['hpwl_geomean'])}。這是共同時間上限的比較，不能把實際用時不同的執行宣稱為嚴格同耗時優勝。worker cache 複製與橫／直分割交替未完成。",
            f"3. 穩健／時間：{sum(r['verdict']=='PASS' for r in default+edge+fresh)+int(l1['verdict']=='PASS')}/{28+len(fresh)} 個要求範圍內的 local_grade 執行通過；L1 實測與稀疏設計時間如上。其他更大規模不由這些結果推論。"]
    block='\n'.join(lines)+'\n'
    p=ROOT/'README.md';s=p.read_text();start=s.index('## 數據與限制') if '## 數據與限制' in s else s.index('## 正式驗收');end=s.index('## 試過但沒有用',start)
    p.write_text(s[:start]+block+'\n'+s[end:])
    # The original seven-version tables stay intact; add one new column.
    p=LAB/'AI_Solutions/COMPARISON.md';s=p.read_text();backup=OUT/'COMPARISON_before.md'
    if not backup.exists():shutil.copyfile(p,backup)
    if '**ChainDP（本次）**' not in s:
        data={r['bench']:r for r in default};metrics=['disp','max','hpwl_sa','seconds'];mode=-1;active=False;orig={};result=[]
        for line in s.splitlines():
            if line.startswith('| bench | v1 |'):
                mode+=1;active=True;orig={};line=line.rstrip()+' **ChainDP（本次）** |'
            elif active and line.startswith('|'):
                cells=[x.strip() for x in line.strip().strip('|').split('|')];key=cells[0]
                value='—'
                if key.startswith('---'):value='---:'
                elif key in data:
                    value=num(float(data[key][metrics[mode]]));orig[key]=float(cells[1].replace('*','').replace(',',''))
                elif '幾何平均' in key:value=f"{gm([float(data[b][metrics[mode]])/v for b,v in orig.items()]):.3f}"
                elif '合計' in key:value=f"{sum(float(r[metrics[mode]]) for r in default):.2f}"
                line=line.rstrip()+' '+value+' |'
            elif active:active=False
            result.append(line)
            if line.startswith('| **v4** |') and 'ClaudeOpus55_v4/' in line:
                result.append('| ChainDP | `CodexChainDP/` | 全域 cap 修復、連續 row packing、多候選 global / vertical swap；見第 8 節本次驗收 |')
        s='\n'.join(result)+'\n'
        s=s.replace('## 1. 結果（11 筆，預設參數）','## 1. 結果（11 筆，預設參數）\n\n新增的 ChainDP 欄是本次獨立重跑；原有七版數值、粗體與最佳筆數保留歷史意義，不重新覆寫。HPWL 單 seed 欄只供預設驗收，多 seed 與同時間結論見第 8 節。')
        s=s.replace('# Lab3 解法比較：七個版本、', '# Lab3 解法比較：七個原始版本與 CodexChainDP、',1)
    marker='## 8. CodexChainDP 本次補充驗證'
    if marker in s:s=s[:s.index(marker)]
    s+='\n'+marker+'\n\n'+f"本次重跑 v4 與 ChainDP 的 testcases + holdout + edge，共 27 筆。最大位移的幾何平均 ÷ v4 = **{allstats['max_geomean']:.6f}**，總位移 = **{allstats['disp_geomean']:.6f}**；逐筆最大位移退步：{', '.join(regress) if regress else '無'}。L1 {l1['verdict']}、{float(l1['seconds']):.2f} 秒；新 seed 20261020 的 16 筆通過 {sum(r['verdict']=='PASS' for r in fresh)}/16。\n\n"
    s+='| HPWL 三 seed | holdout ÷ v4 | edge ÷ v4 |\n|---|---:|---:|\n'
    for name,label in [('fixed_work','同 500 萬次 SA proposal、單執行緒、共同起點'),('equal_time_default','相同總時間上限、預設 SA 排程'),('equal_total_time','相同總時間上限、10 億次排程壓測')]:
        s+=f"| {label} | {stats[name]['holdout']['hpwl_geomean']:.6f} | {stats[name]['edge']['hpwl_geomean']:.6f} |\n"
    s+='\n同時間上限實驗快取 Stage 1 並計入一次實測成本；原 10 億次排程壓測與保留預設 SA 排程的補充實驗分開列出。預算 = 兩版本較慢的 Stage 1 + 12 秒，最後 2 秒給 polish。所有平均、範圍、實際 attempts 和時間：[ChainDP README](CodexChainDP/README.md)、[固定次數詳表](../Testcases/results/CodexChainDP/fixed_work_table.md)、[同時間詳表](../Testcases/results/CodexChainDP/equal_time_default_table.md)。不能把固定次數結果當成時間效率。\n\n'
    s+=f"HPWL 目標未達標：保留預設 SA 排程、共同時間上限下，holdout {pct(stats['equal_time_default']['holdout']['hpwl_geomean'])}、edge {pct(stats['equal_time_default']['edge']['hpwl_geomean'])}；實際耗時不同，也尚未完成嚴格同耗時的比較。新 seed 的預設最大位移幾何平均 {pct(stats['fresh_comparison']['max_geomean'])}、總位移 {pct(stats['fresh_comparison']['disp_geomean'])}，逐筆最大位移沒有退步。\n\n"
    s+=f"精確工具重現 toy 最小最大位移 1938.4、h01 26.5；本次仍分別高出 {pct(float(bybench['toy']['chain_max'])/1938.4)}、{pct(float(bybench['h01']['chain_max'])/26.5)}。全域搜尋失敗是 UNKNOWN，不是不可行證明；大規模的 cache 複製與 band 邊界問題尚未解決。每個目標的不足與總位移取捨均保留在 README，沒有把未達標的項目寫成全面進步。\n"
    p.write_text(s)
    # Preserve the original ChainDP results as an extra historical column.
    p=LAB/'Testcases/results/edge/make_table.py';s=p.read_text()
    s=s.replace("('ChainDP*', 'CodexChainDP')", "('ChainDP（前次）', '../CodexChainDP/edge_before'), ('ChainDP', 'CodexChainDP')")
    s=s.replace('進行中的版本（還沒有 README）。','AugmentDP 為進行中的版本。ChainDP（前次）保留本次之前的歷史數據。')
    p.write_text(s)
    tables=subprocess.check_output([sys.executable,str(p)],text=True)
    (p.parent/'tables.md').write_text(tables)
    readme=p.parent/'README.md';s=readme.read_text();cut=s.index('## 本次 ChainDP 補充') if '## 本次 ChainDP 補充' in s else s.index('## 表（粗體 = 該筆最好）');s=s[:cut]
    s+='## 本次 ChainDP 補充\n\n'
    s+=f"ChainDP 預設 16/16 PASS。用本次新重跑的 v4 比較：最大位移幾何平均 {stats['edge']['max_geomean']:.6f}、總位移 {stats['edge']['disp_geomean']:.6f}；HPWL 三 seed，同 500 萬 proposal 為 {stats['fixed_work']['edge']['hpwl_geomean']:.6f}，同總記帳預算為 {stats['equal_time_default']['edge']['hpwl_geomean']:.6f}。詳細平均與範圍見 [ChainDP README](../../../AI_Solutions/CodexChainDP/README.md)。\n\n"
    s+='上方舊結論與下方其他版本欄位保留歷史資料；ChainDP（前次）也完整保留。新重跑 v4 的精確比較在 [default_comparison.csv](../CodexChainDP/default_comparison.csv)，不覆蓋原 v4 欄位。\n\n## 表（粗體 = 該筆最好）\n\n'+tables
    readme.write_text(s)
    print('Updated README, COMPARISON and preserved-history edge tables.')

if __name__=='__main__':main()
