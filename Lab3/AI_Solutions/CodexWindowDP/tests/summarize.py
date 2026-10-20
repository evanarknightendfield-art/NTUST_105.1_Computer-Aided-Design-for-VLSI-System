#!/usr/bin/env python3
"""Generate reproducible tables from verified runs, without hand-picked cases."""
import csv
import json
import math
from pathlib import Path
import re
import statistics

ROOT=Path(__file__).resolve().parents[1]
RES=ROOT.parents[1]/'Testcases/results'/ROOT.name   # Lab3 convention: run outputs live in Testcases/results
LINK='../../Testcases/results/'+ROOT.name   # the same folder, as seen from COMPARISON.md
LABELS=['ClaudeOpus55','CodexGapSA','ClaudeOpus55_v2','CodexParetoDP','ClaudeOpus55_v3','CodexWindowDP']
SHORT=['v1','GapSA','v2','ParetoDP','v3','WindowDP']
BENCHES=['toy','ibm01','ibm05']+[f'h{i:02d}' for i in range(1,9)]


def geo(values):
    return math.exp(statistics.mean(math.log(x) for x in values))


def main():
    records=list(csv.DictReader((RES/'comparison/records.csv').open()))
    assert len(records)==66 and all(r['verdict']=='PASS' for r in records)
    data={(r['solution'],r['bench']):r for r in records}
    def value(v,b,key): return float(data[v,b][key])
    def ratios(v,key): return [value(v,b,key)/value('ClaudeOpus55_v3',b,key) for b in BENCHES]
    new='CodexWindowDP';base='ClaudeOpus55_v3'
    disp=100*(1-geo(ratios(new,'disp')))
    hpwl=100*(1-geo(ratios(new,'hpwl_final')))
    win=sum(value(new,b,'hpwl_final')<value(base,b,'hpwl_final')-1e-7 for b in BENCHES)
    lines=['# 六個版本的完整比較與仍可改良之處','',
        f'全部 **66/66** 次測試通過獨立 checker。相對 v3，WindowDP 的總位移幾何平均降低 **{disp:.2f}%**，最大位移全部不增加；最終 HPWL 幾何平均'+('降低' if hpwl>=0 else '增加')+f' **{abs(hpwl):.2f}%**，**{win}/11** 筆較好。這是固定搜尋預算、seed 1 的比較，不是官方分數或統計顯著性結論。','',
        f'本次 11 筆總位移都是六個版本最低；HPWL 的 3 筆退步是 toy、h03、h05，因此不能宣稱全面優於 v3。新版總時間相對 v3 的幾何平均增加 {100*(geo(ratios(new,"seconds"))-1):.2f}%。','',
        f'![各測資相對 v3 的變化]({LINK}/figures/comparison.png)','',
        '## 共同實驗條件','',
        '- 2026-10-19，本機循序執行；每版相同 `g++ -std=c++17 -O2 -Wall -Wextra`。沒有同時跑多個 placement 程序；時間是單次本機量測，會受系統負載影響。',
        '- 每個 benchmark 的 `K=max(200,ceil(5000000/Nmovable))`，六版均傳入 `--effort K --time 1000 --seed 1`。因此約 500 萬次，ibm05 約 563 萬次。舊版溫度迴圈取整到 100 的倍數，與 GapSA 的次數差小於 100。',
        '- `--time 1000` 只用來避免依機器速度截斷搜尋；不是每筆真的跑 1000 秒。各版額外收尾與每次 proposal 工作不同，所以同 attempts **不是同 CPU 時間**；時間另列。',
        '- 11 筆＝3 筆課程資料＋8 筆合成 holdout；holdout 不是 TA 官方隱藏測資。',
        '- 每筆檢查 7 個輸出檔、4 行 stdout、兩個 placement 的 row/site/subrow/重疊/fixed/orientation、獨立計算指標、最終 HPWL 不高於該版本合法化結果。',
        f'- 原始資料在 `{LINK}/comparison/records.csv`；編譯器與所有 `.cpp/.hpp` SHA-256 在 `settings.json`；每次 stdout/stderr、trace、輸出及 checker 結果均保留。',
        '- 本次重跑全部版本，未混入根目錄 `../COMPARISON.md` 的預設 effort 測試。那份表中 v3 額外給較多 SA 次數，不能與本表直接相減。','',
        '## 四項指標（越低越好）','']
    for key,title in [('disp','總位移'),('max','最大位移'),('hpwl_final','Stage 2 最終 HPWL'),('seconds','總時間（秒，含所有最佳化及 I/O）')]:
        lines += [f'### {title}','','| 測資 | '+' | '.join(SHORT)+' |','|---|'+'---:|'*6]
        for b in BENCHES:
            vals=[value(v,b,key) for v in LABELS];best=min(vals)
            def cell(x):
                text=f'{x:,.2f}'
                return f'**{text}**' if abs(x-best)<1e-7 else text
            lines += ['| '+b+' | '+' | '.join(cell(x) for x in vals)+' |']
        lines += ['| 相對 v3 幾何平均 | '+' | '.join(f'{geo(ratios(v,key)):.4f}' for v in LABELS)+' |','']
    lines += ['## 新版改善來源：同一次執行的逐階段記錄','',
        '此表不另換 seed，也沒有增加 SA 次數；用各階段日誌隔離新增方法的直接效果。位移用精確內部值，HPWL 的 SA 前後值來自 `[polish-total]`。','',
        '| 測資 | v3 總位移 | 新版總位移 | 降幅 | 新版合法化 HPWL | 新版純 SA HPWL | 新版收尾後 HPWL |','|---|---:|---:|---:|---:|---:|---:|']
    for b in BENCHES:
        text=(RES/f'comparison/{new}/{b}_s1/stderr.txt').read_text()
        match=re.search(r'\[polish-total\] ([\d.]+) -> ([\d.]+)',text)
        lines += [f'| {b} | {value(base,b,"disp"):,.2f} | {value(new,b,"disp"):,.2f} | {100*(1-value(new,b,"disp")/value(base,b,"disp")):.2f}% | {value(new,b,"hpwl_legal"):,.2f} | {float(match[1]):,.2f} | {float(match[2]):,.2f} |']
    lines += ['', f'![ibm01 的 SA 軌跡]({LINK}/figures/sa_ibm01.png)','',
        '高溫時 current HPWL 上升是 SA 接受 uphill moves 的結果；best-so-far 保留原本較佳解，之後隨降溫降低。圖上的菱形才是包含確定性收尾的 Stage 2 最終值，沒有把收尾冒充 SA iterations。','',
        '## 多 seed 檢查','']
    multi=RES/'multiseed/records.csv'
    if multi.exists():
        more=list(csv.DictReader(multi.open()));assert len(more)==20 and all(r['verdict']=='PASS' for r in more)
        cases=['toy','h01','h02','h03','ibm01']
        lines += ['seed 1 使用完整比較的結果；另外重跑 seed 2、3。只對 v3 與新版、5 個案例測量，所以不能外推為全測資多 seed 結論。標準差為 sample standard deviation。','',
            '| 測資 | v3 HPWL 平均 ± 標準差 | WindowDP 平均 ± 標準差 | 平均降幅 | 逐 seed 勝場 |','|---|---:|---:|---:|---:|']
        for b in cases:
            samples=[]
            for v in (base,new):
                samples.append([value(v,b,'hpwl_final')]+[float(r['hpwl_final']) for r in more if r['solution']==v and r['bench']==b])
            a,c=samples;assert len(a)==len(c)==3
            lines += [f'| {b} | {statistics.mean(a):,.2f} ± {statistics.stdev(a):,.2f} | {statistics.mean(c):,.2f} ± {statistics.stdev(c):,.2f} | {100*(1-statistics.mean(c)/statistics.mean(a)):.2f}% | {sum(y<x for x,y in zip(a,c))}/3 |']
    lines += ['', '## 已經最佳了嗎？','',
        '**沒有。** 新 DP 只證明選定小視窗內的位移子問題最佳；全域最大位移、完整 row 分配及 HPWL 都未證明最佳。','',
        '對 toy、h01，另以完整列枚舉的整數規劃檢查相同最大位移上限下的最小總位移。數值最佳／下界是雙精度求解器容差內的證書；大型設計未做全域證明。','',
        '| 測資 | 新版總位移 | 最大位移上限 | 完整 ILP 最佳／已知解 | 比該解高出 | 最大位移 LP 下界 |','|---|---:|---:|---:|---:|---:|']
    for b in ('toy','h01'):
        file=RES/f'bounds/{b}.json'
        if not file.exists():continue
        certificate=json.loads(file.read_text())
        status=certificate['globally_optimal_under_cap']
        optimum=certificate['mip_value']
        lines += [f'| {b} | {certificate["incumbent_total"]:,.2f} | {certificate["cap"]:.4f} | {optimum:,.2f}'+('（證明）' if status else '（僅已知解）')+f' | {100*(certificate["incumbent_total"]/optimum-1):.3f}% | {certificate.get("maximum_lp_lower_bound",float("nan")):.4f} |']
    lines += ['',
        'toy 的完整 ILP incumbent 與 dual bound 都是 42,890.2，已在零 MIP gap 下證明。h01 在 120 秒 MIP 上限內找到 2,295.79，dual bound 為 2,281.57，**尚未證明最優**；其最優總位移位於兩者之間，不能再引用原先受限模型的 2,383.6 作全域最佳值。',
        '最大位移 LP 下界是有效的下界，但不是整數最優值：toy 約 1,938.40，h01 約 26.48。因此現有 2,007.9／31.67 並不能據此宣稱只差 2–4%。',
        f'完整位置集合的結果及 dual bound 保留於 `{LINK}/bounds/*.json`。既有 `Grader/tools/exact_bound.py` 的受限列 LP 不可行不能直接當全域不可行、受限列 ILP 的最優也不等於完整 ILP 最優；還有晶片外左側 pad 的負索引遮罩問題，詳見 `READING_NOTES.md`。本版另做完整列枚舉與正確的遮罩，沒有改寫其他人的實驗記錄。','',
        '## 我認為值得繼續改良的地方','',
        '1. **最大位移專用的視窗搜尋。** 目前 DP 固定既有最大位移 cap，主要減少總位移。可對最差 cell 周圍選擇兩到三個 row，在總位移預算內對 cap 二分；需要較大的視窗才可能疏通壅塞，代價是指數成本。',
        '2. **自適應視窗。** 依擁擠度、總位移收益與最近幾輪成功率選視窗／輪數，並快取未改變的區域；可以減少在已穩定區域重算的時間。更大 k 要有明確工作上限。',
        '   同 row 排序 DP 目前固定 gap 序列；可與 L07 的固定順序 linear placement 交替，讓順序與空白一起逐步改善，但仍須比較額外時間的效益。',
        '3. **更好的跨 row HPWL 聯合成本。** 新增的同 row 排序 DP 已精確處理共享 net，但跨 row 的 frozen-pin 成本仍只是 proposal。可為小 net 的邊界狀態設計 DP 或 branch-and-bound；避免把 surrogate 最佳誤稱為真 HPWL 最佳。',
        '4. **SA 多 seed／重啟與接受率調溫。** 改善 Stage 1 不保證 SA 結果更好；有限次數仍有隨機退步。需在相同總時間下比較重啟、預算分配與不同 schedule，不能只展示多跑幾倍後的較佳數字。',
        '5. **安全的並行排程。** 無空間交集還不夠，區域仍可能共享 net。只有互不衝突的 cell、site 與受影響 net 才能同步更新；不能預先假設 4 核就線性得到 4 倍品質／速度。',
        '6. **官方評分與更大測資。** 目前 Grading Criteria 是 Project 2，沒有 Project 3 權重／超時規則。正式規則與真正隱藏測資到位後，才適合選擇品質、時間和記憶體的最終折衷。','',
        '不建議把新版本說成「每項指標都壓過所有版本」：v1 在某些最大位移案例仍可能較好；新版的優勢是可驗證的聯合 row 位移改善，代價是額外計算，HPWL 也有 seed／案例差異。','',
        '## 重現','',
        '```bash','python3 tests/compare.py',
        'python3 tests/compare.py --labels ClaudeOpus55_v3,CodexWindowDP --seeds 2,3 --benches toy,h01,h02,h03,ibm01 --suite multiseed',
        'python3 tests/summarize.py','```','']
    (ROOT/'COMPARISON.md').write_text('\n'.join(lines))
    print(f'66/66 PASS; vs v3: displacement change {-disp:+.3f}%, HPWL change {-hpwl:+.3f}%, HPWL wins {win}/11')


if __name__=='__main__':main()
