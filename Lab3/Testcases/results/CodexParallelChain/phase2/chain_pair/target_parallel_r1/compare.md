# Chain target windows: same-round Stage 1 pairs

Ratio = candidate / v6. No SA; HPWL is not a quality gate in this report.

| Case | Total ratio | Max ratio | Chain seconds candidate / v6 | Both vs historical ChainDP |
|---|---:|---:|---:|:---:|
| ibm01 | 1.00015385 | 1.00000000 | 1.384007 / 1.450737 | True |
| ibm05 | 1.00000799 | 1.00000000 | 1.336789 / 1.369443 | True |
| toy | 1.00000000 | 1.00000000 | 0.015304 / 0.013843 | True |
| h01 | 1.00000000 | 1.00000000 | 0.059558 / 0.056060 | True |
| h02 | 1.00000000 | 1.00000000 | 1.826305 / 1.870798 | True |
| h03 | 1.00000000 | 1.00000000 | 2.580758 / 2.627096 | True |
| h04 | 1.00053649 | 1.00260668 | 1.446571 / 1.449012 | False |
| h05 | 1.00000000 | 1.00000000 | 1.450185 / 1.450986 | True |
| h06 | 1.00357714 | 0.98244445 | 1.446123 / 1.454442 | False |
| h07 | 0.99998274 | 1.00000000 | 1.372210 / 1.457566 | False |
| h08 | 1.00029528 | 1.00000000 | 1.322027 / 1.358798 | True |

Both: 8/11; total GM 1.00041344; max GM 0.99862747.
Chain seconds: {'v6': 14.558779537000001, 'candidate': 14.239835988999996}.

Candidate 1.5 s vs v6 3 s chain gain:
- h05: candidate 0.261639%, v6 0.288901%, historical requirement 0.29%; pass False; actual chain seconds 1.447799 / 2.848087.
- h06: candidate 0.364283%, v6 0.434934%, historical requirement 0.44%; pass False; actual chain seconds 1.444832 / 2.890684.

Gates:
- both_at_least_8: True
- total_improved_003pct: False
- max_gm_not_worse_01pct: True
- every_max_not_worse_02pct: False
- chain_seconds_not_more: True
- h05_h06_gain: False
