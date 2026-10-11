// construct.cpp — Stage 1：由下而上（bottom-up）分群建立合法初始解
//
// frontier = 目前還沒有 parent 的子樹根（一開始是全部 sinks）
// 重複：
//   1. 如果 frontier 可以直接接到 SRC（數量 <= SRC fanout 且總長 <= SRC length）→ 接上，結束
//   2. 否則把 frontier 用遞迴二分切成多群（每群 <= Fg 個、且總長不超過某型號的 L），
//      每群放一個 buffer 當 parent，新的 buffers 成為下一層的 frontier
//   3. 如果一群都合不起來（彼此太遠），就幫離 SRC 遠的節點加一個 relay buffer，
//      往 SRC 方向前進一段，下一輪再試
#include "construct.h"

#include <algorithm>
#include <cmath>

#include "placement.h"

namespace {

// 選「每個 fanout 位置最便宜」的型號決定分群大小：
// 一棵樹要容納 N 個 sink，大約需要 N/(F-1) 個 buffer，所以比較 C/(F-1)
int chooseGroupFanout(const Problem& P) {
    int bestF = 2;
    double bestRatio = 1e300;
    for (const BufType& t : P.lib) {
        if (t.F < 2) continue;
        double r = (double)t.C / (double)(t.F - 1);
        if (r < bestRatio) { bestRatio = r; bestF = t.F; }
    }
    return bestF;
}

long long sumTo(const Tree& T, const std::vector<int>& ids, long long x, long long y) {
    long long s = 0;
    for (int v : ids) s += manhattan(x, y, T.nd[v].x, T.nd[v].y);
    return s;
}

// 依 bounding box 較長的那一軸排序
void sortLongAxis(const Tree& T, std::vector<int>& ids) {
    int x0 = T.nd[ids[0]].x, x1 = x0, y0 = T.nd[ids[0]].y, y1 = y0;
    for (int v : ids) {
        x0 = std::min(x0, T.nd[v].x); x1 = std::max(x1, T.nd[v].x);
        y0 = std::min(y0, T.nd[v].y); y1 = std::max(y1, T.nd[v].y);
    }
    bool byX = (x1 - x0) >= (y1 - y0);
    std::sort(ids.begin(), ids.end(), [&](int a, int b) {
        const Node &A = T.nd[a], &B = T.nd[b];
        return byX ? (A.x != B.x ? A.x < B.x : A.y < B.y) : (A.y != B.y ? A.y < B.y : A.x < B.x);
    });
}

// 遞迴二分：切到每群「數量 <= Fg 且在中位數放 buffer 時總長 <= 某型號的 L」
void partition(const Tree& T, std::vector<int> ids, int Fg, std::vector<std::vector<int>>& out) {
    int n = (int)ids.size();
    if (n == 0) return;
    if (n == 1) { out.push_back(ids); return; }
    if (n <= Fg) {
        long long mx, my;
        medianPoint(T, ids, mx, my);
        long long Lmax = T.TT->maxLen(n);
        if (Lmax >= 0 && sumTo(T, ids, mx, my) <= Lmax) { out.push_back(ids); return; }
    }
    sortLongAxis(T, ids);
    int g = (n + Fg - 1) / Fg;   // 需要的群數
    int left = (g >= 2) ? (int)((long long)n * (g / 2) / g) : n / 2;   // 讓兩邊群大小平均
    left = std::max(1, std::min(n - 1, left));
    partition(T, std::vector<int>(ids.begin(), ids.begin() + left), Fg, out);
    partition(T, std::vector<int>(ids.begin() + left, ids.end()), Fg, out);
}

// 幫一群節點放 parent buffer；放不下就再切半
void placeGroup(Tree& T, std::vector<int> g, std::vector<int>& next, bool& merged) {
    if (g.size() == 1) { next.push_back(g[0]); return; }
    int bx, by;
    if (findParentSpot(T, g, 12, bx, by)) {
        int b = T.addBuffer(bx, by);
        for (int c : g) T.link(c, b);
        T.recompute(b);
        next.push_back(b);
        merged = true;
        return;
    }
    sortLongAxis(T, g);
    size_t h = g.size() / 2;
    placeGroup(T, std::vector<int>(g.begin(), g.begin() + h), next, merged);
    placeGroup(T, std::vector<int>(g.begin() + h, g.end()), next, merged);
}

// 在 v 與 SRC 之間插一個 relay buffer，讓子樹往 SRC 前進
// 回傳新的子樹根（失敗則回傳 v 本身）
int relayToward(Tree& T, int v, long long step) {
    const Node& s = T.nd[0];
    long long vx = T.nd[v].x, vy = T.nd[v].y;
    long long d = manhattan(vx, vy, s.x, s.y);
    step = std::min(step, d - 1);
    if (step <= 0) return v;
    long long dx = s.x - vx, dy = s.y - vy;
    long long mx = (long long)std::llround((double)step * (double)std::llabs(dx) / (double)d);
    mx = std::min(mx, std::llabs(dx));
    long long my = std::min(step - mx, std::llabs(dy));
    long long tx = vx + (dx >= 0 ? mx : -mx), ty = vy + (dy >= 0 ? my : -my);
    long long maxL = T.TT->maxLen(1);
    auto ok = [&](long long x, long long y) {
        return manhattan(x, y, vx, vy) <= maxL && manhattan(x, y, s.x, s.y) < d;
    };
    int bx, by;
    if (!nearestUsable(T, tx, ty, 64, ok, bx, by)) return v;
    int b = T.addBuffer(bx, by);
    T.link(v, b);
    T.recompute(b);
    return b;
}

}  // namespace

bool buildInitial(Tree& T, bool singleRoot) {
    const Problem& P = *T.P;
    std::vector<int> frontier;
    for (int i = 1; i <= T.N; ++i) {
        T.recompute(i);
        frontier.push_back(i);
    }
    const int Fg = std::max(2, std::min(chooseGroupFanout(P), T.TT->maxF()));

    // relay 用「每單位長度最便宜」的型號決定一步走多遠
    long long relayStep = 1;
    double bestPerLen = 1e300;
    for (const BufType& t : P.lib) {
        if (t.F < 1 || t.L < 1) continue;
        double r = (double)t.C / (double)t.L;
        if (r < bestPerLen) { bestPerLen = r; relayStep = t.L; }
    }

    for (int iter = 0; iter < 200000; ++iter) {
        long long s = 0;
        for (int v : frontier) s += T.dist(0, v);
        bool fits = (int)frontier.size() <= P.srcF && s <= P.srcL;
        if (fits && (!singleRoot || frontier.size() == 1)) {
            for (int v : frontier) T.link(v, 0);
            T.recomputeAll();
            return true;
        }
        std::vector<int> next;
        bool merged = false;
        if (frontier.size() > 1) {
            std::vector<std::vector<int>> groups;
            if (fits && (int)frontier.size() <= T.TT->maxF()) groups.push_back(frontier);   // 最後一步：全部合成一個根
            else partition(T, frontier, Fg, groups);
            for (auto& g : groups) placeGroup(T, g, next, merged);
        } else {
            next = frontier;
        }
        if (!merged) {
            // 都合不起來 → 把離 SRC 較遠的節點往 SRC 拉近
            long long far = 0;
            for (int v : next) far = std::max(far, T.dist(0, v));
            bool progress = false;
            for (int& v : next) {
                if (T.dist(0, v) * 2 < far && next.size() > 1) continue;
                int nv = relayToward(T, v, relayStep);
                if (nv != v) { v = nv; progress = true; }
            }
            if (!progress) return false;
        }
        frontier.swap(next);
    }
    return false;
}
