// placement.h — 幫一群 children 找一個好的 parent buffer 位置
#pragma once
#include <functional>
#include <vector>

#include "tree.h"

// 對 kids（已算好 lo/hi 的節點）找 buffer 位置 (bx, by)。
// 目標：w_skew * (children 的 latency range) + 型號 cost，平手時選總線長較短的。
// 只在 children 的 bounding box 內搜尋（在 box 外 Manhattan 距離只會整體平移，range 不會變好）。
// grid = 粗搜的格數。找不到合法位置回傳 false。
bool findParentSpot(const Tree& T, const std::vector<int>& kids, int grid, int& bx, int& by);

// 從 (cx, cy) 一圈一圈往外找第一個「可用且滿足 ok(x,y)」的點，最多找到半徑 maxR。
bool nearestUsable(const Tree& T, long long cx, long long cy, int maxR,
                   const std::function<bool(long long, long long)>& ok, int& rx, int& ry);

// kids 的 Manhattan 1-median（x、y 各自取中位數，使總距離最小）
void medianPoint(const Tree& T, const std::vector<int>& kids, long long& mx, long long& my);
