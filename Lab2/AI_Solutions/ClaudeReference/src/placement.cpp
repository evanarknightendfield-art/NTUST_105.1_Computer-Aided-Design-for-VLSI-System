// placement.cpp — parent buffer 位置搜尋
#include "placement.h"

#include <algorithm>
#include <climits>

namespace {

struct Cand {
    double val;
    long long x, y;
};

// 評估把 buffer 放在 (x, y) 的好壞；不合法回傳 false
bool evalSpot(const Tree& T, const std::vector<int>& kids, long long x, long long y, double& val) {
    if (!T.usable(x, y)) return false;
    long long lo = LLONG_MAX, hi = LLONG_MIN, sum = 0;
    for (int c : kids) {
        const Node& m = T.nd[c];
        long long e = manhattan(x, y, m.x, m.y);
        sum += e;
        lo = std::min(lo, e + m.lo);
        hi = std::max(hi, e + m.hi);
    }
    int t = T.TT->pick((int)kids.size(), sum);
    if (t < 0) return false;
    val = (double)T.P->wSkew * (double)(hi - lo) + (double)T.TT->cost(t) + 1e-6 * (double)sum;
    return true;
}

}  // namespace

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

bool findParentSpot(const Tree& T, const std::vector<int>& kids, int grid, int& bx, int& by) {
    long long x0 = LLONG_MAX, x1 = LLONG_MIN, y0 = LLONG_MAX, y1 = LLONG_MIN;
    for (int c : kids) {
        x0 = std::min<long long>(x0, T.nd[c].x);
        x1 = std::max<long long>(x1, T.nd[c].x);
        y0 = std::min<long long>(y0, T.nd[c].y);
        y1 = std::max<long long>(y1, T.nd[c].y);
    }
    // 多留 1 格，並夾在晶片內部
    x0 = std::max<long long>(1, x0 - 1);
    y0 = std::max<long long>(1, y0 - 1);
    x1 = std::min<long long>(T.P->dimx - 1, x1 + 1);
    y1 = std::min<long long>(T.P->dimy - 1, y1 + 1);

    std::vector<Cand> top;   // 保留最好的幾個起點再細搜
    const size_t KEEP = 3;
    auto offer = [&](long long x, long long y) {
        double v;
        if (!evalSpot(T, kids, x, y, v)) return;
        Cand c{v, x, y};
        if (top.size() < KEEP) {
            top.push_back(c);
        } else {
            auto worst = std::max_element(top.begin(), top.end(),
                                          [](const Cand& a, const Cand& b) { return a.val < b.val; });
            if (v < worst->val) *worst = c;
        }
    };

    if (x0 <= x1 && y0 <= y1) {
        long long sx = std::max<long long>(1, (x1 - x0) / grid);
        long long sy = std::max<long long>(1, (y1 - y0) / grid);
        for (long long x = x0; x <= x1; x += sx)
            for (long long y = y0; y <= y1; y += sy) offer(x, y);
        long long mx, my;
        medianPoint(T, kids, mx, my);
        offer(mx, my);

        // 由粗到細：步長減半，在 5x5 鄰域找更好的點
        Cand best{1e300, -1, -1};
        for (Cand c : top) {
            long long stx = sx, sty = sy;
            while (true) {
                stx = std::max<long long>(1, stx / 2);
                sty = std::max<long long>(1, sty / 2);
                bool moved = true;
                int guard = 0;
                while (moved && guard++ < 64) {
                    moved = false;
                    Cand cur = c;
                    for (int dx = -2; dx <= 2; ++dx)
                        for (int dy = -2; dy <= 2; ++dy) {
                            if (!dx && !dy) continue;
                            double v;
                            long long x = c.x + dx * stx, y = c.y + dy * sty;
                            if (evalSpot(T, kids, x, y, v) && v < cur.val) cur = Cand{v, x, y};
                        }
                    if (cur.val < c.val) { c = cur; moved = true; }
                }
                if (stx == 1 && sty == 1) break;
            }
            if (c.val < best.val) best = c;
        }
        if (best.x >= 0) {
            bx = (int)best.x;
            by = (int)best.y;
            return true;
        }
    }

    // bounding box 裡都不行（被佔滿或太小）→ 從中位數往外找任何合法位置
    long long mx, my;
    medianPoint(T, kids, mx, my);
    long long maxL = T.TT->maxLen((int)kids.size());
    if (maxL < 0) return false;
    auto ok = [&](long long x, long long y) {
        long long s = 0;
        for (int c : kids) s += manhattan(x, y, T.nd[c].x, T.nd[c].y);
        return s <= maxL;
    };
    return nearestUsable(T, mx, my, 64, ok, bx, by);
}
