# Ejection beam selection: same-round Stage 1 pairs

Ratio = candidate / v6. No SA; HPWL is not a quality gate in this report.

| Case | Total ratio | Max ratio | Chain seconds candidate / v6 | Both vs historical ChainDP |
|---|---:|---:|---:|:---:|
| ibm01 | 0.99994857 | 1.00000000 | 1.450060 / 1.451364 | True |
| ibm05 | 0.99999645 | 1.00000000 | 1.369419 / 1.369574 | True |
| toy | 1.00000000 | 1.00000000 | 0.013404 / 0.013958 | True |
| h01 | 1.00000000 | 1.00000000 | 0.056883 / 0.056107 | True |
| h02 | 1.00000000 | 1.00000000 | 1.861640 / 1.862599 | True |
| h03 | 1.00000000 | 1.00000000 | 2.627778 / 2.626243 | True |
| h04 | 1.00000554 | 1.00000000 | 1.448978 / 1.450646 | False |
| h05 | 1.00000000 | 1.00000000 | 1.451876 / 1.450464 | True |
| h06 | 0.99994590 | 1.00000000 | 1.455686 / 1.455356 | False |
| h07 | 0.99998160 | 1.00000000 | 1.452919 / 1.451376 | False |
| h08 | 0.99937074 | 1.00000000 | 1.359083 / 1.360135 | True |

Both: 8/11; total GM 0.99993169; max GM 1.00000000.
Chain seconds: {'v6': 14.547820981999996, 'candidate': 14.547725445}.

Candidate 1.5 s vs v6 3 s chain gain:
- h05: candidate 0.261639%, v6 0.288901%, historical requirement 0.29%; pass False; actual chain seconds 1.451768 / 2.872168.
- h06: candidate 0.367677%, v6 0.434905%, historical requirement 0.44%; pass False; actual chain seconds 1.455389 / 2.889821.

Gates:
- both_at_least_8: True
- total_improved_003pct: False
- max_gm_not_worse_01pct: True
- every_max_not_worse_02pct: True
- chain_seconds_not_more: True
- h05_h06_gain: False
