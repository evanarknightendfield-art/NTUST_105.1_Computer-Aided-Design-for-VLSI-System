// placement.cpp — buffer 位置搜尋
#include "placement.h"

#include <algorithm>
#include <climits>

void medianPoint(const Tree& T, const std::vector<int>& kids, long long& mx, long long& my) {
    std::vector<long long> xs, ys;
    for (int c : kids) {
        xs.push_back(T.nd[c].x);
        ys.push_back(T.nd[c].y);
    }
    size_t h = xs.size() / 2;
    std::nth_element(xs.begin(), xs.begin() + h, xs.end());
    std::nth_element(ys.begin(), ys.begin() + h, ys.end());
    mx = xs[h];
    my = ys[h];
}

bool nearestUsable(const Tree& T, long long cx, long long cy, int maxR,
                   const std::function<bool(long long, long long)>& ok, int& rx, int& ry) {
    auto test = [&](long long x, long long y) {
        if (!T.usable(x, y) || !ok(x, y)) return false;
        rx = (int)x;
        ry = (int)y;
        return true;
    };
    if (test(cx, cy)) return true;
    for (long long r = 1; r <= maxR; ++r) {
        // 以 (cx, cy) 為中心、邊長 2r+1 的正方形外圈
        for (long long d = -r; d <= r; ++d) {
            if (test(cx + d, cy - r) || test(cx + d, cy + r)) return true;   // 上下兩條邊
            if (d != -r && d != r && (test(cx - r, cy + d) || test(cx + r, cy + d))) return true;   // 左右兩條邊
        }
    }
    return false;
}

long long minSumLen(const Tree& T, const std::vector<int>& kids) {
    long long mx, my, s = 0;
    medianPoint(T, kids, mx, my);
    for (int c : kids) s += manhattan(mx, my, T.nd[c].x, T.nd[c].y);
    return s;
}

bool findParentSpot(const Tree& T, const std::vector<int>& kids, int grid, int& bx, int& by, bool fallback,
                    bool fast) {
    long long x0 = LLONG_MAX, x1 = LLONG_MIN, y0 = LLONG_MAX, y1 = LLONG_MIN;
    for (int c : kids) {
        x0 = std::min<long long>(x0, T.nd[c].x);
        x1 = std::max<long long>(x1, T.nd[c].x);
        y0 = std::min<long long>(y0, T.nd[c].y);
        y1 = std::max<long long>(y1, T.nd[c].y);
    }
    // 目標：w_skew * (children 的 latency range) + 型號 cost，平手選總線長短的
    const int k = (int)kids.size();
    const double w = (double)T.P->wSkew;
    auto eval = [&](long long x, long long y, double& val) {
        long long lo = LLONG_MAX, hi = LLONG_MIN, sum = 0;
        for (int c : kids) {
            const Node& m = T.nd[c];
            long long e = manhattan(x, y, m.x, m.y);
            sum += e;
            lo = std::min(lo, e + m.lo);
            hi = std::max(hi, e + m.hi);
        }
        int t = T.TT->pick(k, sum);
        if (t < 0) return false;
        val = w * (double)(hi - lo) + (double)T.TT->cost(t) + 1e-6 * (double)sum;
        return true;
    };
    long long mx, my;
    medianPoint(T, kids, mx, my);
    SearchOpt o = fast ? fastSearch(grid) : fullSearch(grid);
    if (searchSpot(T, x0 - 1, y0 - 1, x1 + 1, y1 + 1, o, eval, {{mx, my}}, bx, by)) return true;
    if (!fallback) return false;

    // bounding box 裡都不行（被佔滿或太小）→ 從中位數往外找任何合法位置
    long long maxL = T.TT->maxLen((int)kids.size());
    if (maxL < 0) return false;
    auto ok = [&](long long x, long long y) {
        long long s = 0;
        for (int c : kids) s += manhattan(x, y, T.nd[c].x, T.nd[c].y);
        return s <= maxL;
    };
    return nearestUsable(T, mx, my, 64, ok, bx, by);
}
