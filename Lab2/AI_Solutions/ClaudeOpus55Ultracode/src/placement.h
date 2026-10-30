// placement.h — buffer 位置搜尋
#pragma once
#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include "tree.h"

// 搜尋參數：粗搜格數、保留幾個起點做細搜、細搜鄰域半徑（2 = 5x5，1 = 3x3）
struct SearchOpt {
    int grid = 12;
    int keep = 3;
    int nbr = 2;
};
inline SearchOpt fullSearch(int grid) { return SearchOpt{grid, 3, 2}; }
inline SearchOpt fastSearch(int grid) { return SearchOpt{grid, 1, 1}; }   // 最佳化時用：便宜很多

// 在矩形 [x0,x1]x[y0,y1]（會夾在晶片內部）找 eval 最小的可用點。
// eval(x, y, val)：(x, y) 合法時回傳 true 並寫入分數（越小越好）。
// 先 grid x grid 粗搜（外加 seeds），保留最好的 keep 個起點，再把步長減半細搜。
template <class Eval>
bool searchSpot(const Tree& T, long long x0, long long y0, long long x1, long long y1, const SearchOpt& o,
                Eval&& eval, const std::vector<std::pair<long long, long long>>& seeds, int& bx, int& by) {
    struct Cand {
        double val;
        long long x, y;
    };
    x0 = std::max<long long>(1, x0);
    y0 = std::max<long long>(1, y0);
    x1 = std::min<long long>(T.P->dimx - 1, x1);
    y1 = std::min<long long>(T.P->dimy - 1, y1);
    if (x0 > x1 || y0 > y1) return false;

    auto evalAt = [&](long long x, long long y, double& v) { return T.usable(x, y) && eval(x, y, v); };
    Cand top[3];
    int ntop = 0;
    const int keep = std::max(1, std::min(3, o.keep));
    auto offer = [&](long long x, long long y) {
        double v;
        if (!evalAt(x, y, v)) return;
        if (ntop < keep) {
            top[ntop++] = Cand{v, x, y};
            return;
        }
        int w = 0;
        for (int i = 1; i < ntop; ++i)
            if (top[i].val > top[w].val) w = i;
        if (v < top[w].val) top[w] = Cand{v, x, y};
    };
    long long sx = std::max<long long>(1, (x1 - x0) / o.grid);
    long long sy = std::max<long long>(1, (y1 - y0) / o.grid);
    for (long long x = x0; x <= x1; x += sx)
        for (long long y = y0; y <= y1; y += sy) offer(x, y);
    for (const auto& s : seeds) offer(s.first, s.second);

    // 由粗到細：步長減半，在鄰域裡往更好的點移動
    Cand best{1e300, -1, -1};
    for (int t = 0; t < ntop; ++t) {
        Cand c = top[t];
        long long stx = sx, sty = sy;
        while (true) {
            stx = std::max<long long>(1, stx / 2);
            sty = std::max<long long>(1, sty / 2);
            bool moved = true;
            int guard = 0;
            while (moved && guard++ < 64) {
                moved = false;
                Cand cur = c;
                for (int dx = -o.nbr; dx <= o.nbr; ++dx)
                    for (int dy = -o.nbr; dy <= o.nbr; ++dy) {
                        if (!dx && !dy) continue;
                        double v;
                        long long x = c.x + dx * stx, y = c.y + dy * sty;
                        if (evalAt(x, y, v) && v < cur.val) cur = Cand{v, x, y};
                    }
                if (cur.val < c.val) { c = cur; moved = true; }
            }
            if (stx == 1 && sty == 1) break;
        }
        if (c.val < best.val) best = c;
    }
    if (best.x < 0) return false;
    bx = (int)best.x;
    by = (int)best.y;
    return true;
}

// 對 kids（已算好 lo/hi 的節點）找 parent buffer 位置 (bx, by)。
// 目標：w_skew * (children 的 latency range) + 型號 cost，平手時選總線長較短的。
// 只在 children 的 bounding box 內搜尋（在 box 外 Manhattan 距離只會整體平移，range 不會變好）。
// fallback = true：box 內找不到時從中位數往外找（慢，只在建構時用）；fast = true：用便宜的搜尋。
bool findParentSpot(const Tree& T, const std::vector<int>& kids, int grid, int& bx, int& by, bool fallback = true,
                    bool fast = false);

// 中位數放 buffer 時的總線長（= 所有位置中最小的總線長），用來快速判斷「一定放不下」
long long minSumLen(const Tree& T, const std::vector<int>& kids);

// 從 (cx, cy) 一圈一圈往外找第一個「可用且滿足 ok(x,y)」的點，最多找到半徑 maxR。
bool nearestUsable(const Tree& T, long long cx, long long cy, int maxR,
                   const std::function<bool(long long, long long)>& ok, int& rx, int& ry);

// kids 的 Manhattan 1-median（x、y 各自取中位數，使總距離最小）
void medianPoint(const Tree& T, const std::vector<int>& kids, long long& mx, long long& my);
