// balance.h — Stage 2：插入 delay（繞路）buffer 來拉高 T_min
#pragma once
#include "tree.h"

// 在 v 與它的 parent 之間插入 delay buffer（必要時插一串），讓 v 的整棵子樹延後 delta（偶數）。
// 完全成功回傳 true；added = 實際插入的 buffer 數（失敗時可能只延後了一部分，但樹一定合法）。
bool insertDelay(Tree& T, int v, long long delta, int& added);

// 掃描目標 skew，找出「buffer cost + w_skew * skew」最小的插入方案並套用。
// 回傳插入的 buffer 數。
int balanceSkew(Tree& T);
