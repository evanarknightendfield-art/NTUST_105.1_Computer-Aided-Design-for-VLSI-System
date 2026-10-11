// balance.cpp — Stage 2：delay padding
//
// 想法：T_max 由最慢的 sink 決定，太早到的 sink 可以「繞遠路」變慢。
// 對節點 v（arrival = A_v，子樹 latency 區間 [lo_v, hi_v]）：
//   slack(v) = T_max - (A_v + hi_v)   整棵子樹最多可以延後多少而不讓 T_max 變大
//   need(v)  = low   - (A_v + lo_v)   想讓子樹裡最早的 sink 也 >= low，至少要延後多少
// 從上往下走，第一個 need <= slack 的節點只要插「一個」delay buffer 就能整棵解決。
// 繞路長度一定是偶數：d(p,n) + d(n,v) - d(p,v) 的奇偶性固定，
// 所以 sink 的 arrival 奇偶性 = (x+y) 的奇偶性，無法改變。
//
// low 的選擇：掃描很多個候選值，估計 (插入數 × buffer 成本) + w_skew × (T_max - low)，取最小。
#include "balance.h"

#include <algorithm>
#include <climits>

namespace {

struct Pad {
    int v;
    long long delta;
};

// 每個節點的 arrival time（從 SRC 走到它的路徑長）
std::vector<long long> arrivals(const Tree& T) {
    std::vector<long long> arr(T.nd.size(), 0);
    std::vector<int> order{0};
    for (size_t i = 0; i < order.size(); ++i)
        for (int c : T.nd[order[i]].ch) {
            arr[c] = arr[order[i]] + T.dist(order[i], c);
            order.push_back(c);
        }
    return arr;
}

// 給定目標 low，規劃要在哪些節點上方插多少 delay
void planPads(const Tree& T, const std::vector<long long>& arr, long long low, std::vector<Pad>& plan) {
    plan.clear();
    const long long Tmax = T.nd[0].hi;
    std::vector<int> st(T.nd[0].ch.begin(), T.nd[0].ch.end());
    while (!st.empty()) {
        int v = st.back();
        st.pop_back();
        const Node& n = T.nd[v];
        long long A = arr[v];
        if (A + n.lo >= low) continue;               // 子樹裡沒有太早到的 sink
        long long slack = Tmax - (A + n.hi);
        long long need = low - (A + n.lo);
        need += need & 1;                            // 繞路只能是偶數
        if (need > slack && n.kind == SINK_NODE) need -= 2;   // 奇偶性限制，差 1 就算了
        if (need > 0 && need <= slack) {
            plan.push_back({v, need});
            continue;
        }
        for (int c : n.ch) st.push_back(c);
    }
}

// 在以 v 為中心、半徑 a 的菱形上找一個點 n，使 d(p, n) == b 且可用
bool pointOnDiamond(const Tree& T, int v, int p, long long a, long long b, int& nx, int& ny) {
    long long vx = T.nd[v].x, vy = T.nd[v].y, px = T.nd[p].x, py = T.nd[p].y;
    for (long long dx = -a; dx <= a; ++dx) {
        long long dy = a - std::llabs(dx);
        for (int s = 0; s < (dy ? 2 : 1); ++s) {
            long long x = vx + dx, y = vy + (s ? -dy : dy);
            if (manhattan(x, y, px, py) == b && T.usable(x, y)) {
                nx = (int)x;
                ny = (int)y;
                return true;
            }
        }
    }
    return false;
}

}  // namespace

namespace {

// 只用「一個」buffer 讓 v 的子樹延後 delta（偶數）。成功回傳 true，失敗樹不變。
bool insertOne(Tree& T, int v, long long delta) {
    if (delta <= 0 || (delta & 1)) return false;
    int p = T.nd[v].parent;
    if (p < 0) return false;
    const Node& par = T.nd[p];   // 注意：addBuffer 之後這個參考可能失效，只在這裡使用
    long long D = T.dist(p, v);
    long long rest = par.sumLen - D;   // p 的其他邊已經用掉的長度
    // p 這條邊最多能用多長：先試「不升級型號」，再試「升級到最大型號」
    long long budgets[2];
    if (par.kind == SRC_NODE) {
        budgets[0] = budgets[1] = T.P->srcL - rest;
    } else {
        budgets[0] = (par.type >= 0 ? T.P->lib[par.type].L : 0) - rest;
        budgets[1] = T.TT->maxLen((int)par.ch.size()) - rest;
    }
    // 新 buffer（只有 1 個 child）的長度上限：先試最便宜型號，再試最大型號
    int cheap = T.TT->pick(1, 0);
    long long nBudgets[2] = {cheap >= 0 ? T.P->lib[cheap].L : 0, T.TT->maxLen(1)};
    long long total = D + delta;   // p -> n -> v 的總長
    for (long long bp : budgets) {
        for (long long bn : nBudgets) {
            // a = d(n, v), b = d(p, n)；兩個菱形要相交必須 |a-b| <= D，即 a, b >= delta/2
            long long aLo = std::max(delta / 2, total - bp);
            long long aHi = std::min(bn, D + delta / 2);
            for (long long a = aLo, tries = 0; a <= aHi && tries < 8; ++a, ++tries) {
                int nx, ny;
                if (!pointOnDiamond(T, v, p, a, total - a, nx, ny)) continue;
                T.begin();
                int n = T.addBuffer(nx, ny);
                T.unlink(v);
                T.link(v, n);
                T.link(n, p);
                T.refreshUp(n);
                T.refreshUp(p);
                if (T.touchedLegal()) {
                    T.commit();
                    return true;
                }
                T.rollback();
            }
        }
    }
    return false;
}

// 一個 buffer 在 v 上方最多能提供多少延遲：delta <= min(2*bn, 2*bp, bn + bp - D)
long long maxOneDelay(const Tree& T, int v) {
    int p = T.nd[v].parent;
    const Node& par = T.nd[p];
    long long D = T.dist(p, v);
    long long rest = par.sumLen - D;
    long long bp = (par.kind == SRC_NODE) ? T.P->srcL - rest : T.TT->maxLen((int)par.ch.size()) - rest;
    long long bn = T.TT->maxLen(1);
    long long m = std::min({2 * bn, 2 * bp, bn + bp - D});
    return m & ~1LL;
}

}  // namespace

bool insertDelay(Tree& T, int v, long long delta, int& added) {
    added = 0;
    while (delta > 0) {
        if (insertOne(T, v, delta)) {
            ++added;
            return true;
        }
        // 一個不夠 → 先插一個「盡量長」的，剩下的在新 buffer 與 v 之間繼續插（形成一串）
        long long m = std::min(maxOneDelay(T, v), delta - 2);
        bool done = false;
        for (int k = 0; k < 6 && m > 0; ++k) {
            if (insertOne(T, v, m)) { done = true; break; }
            m = (m * 3 / 4) & ~1LL;
        }
        if (!done) return false;
        ++added;
        delta -= m;
    }
    return true;
}

int balanceSkew(Tree& T) {
    if (T.N == 0) return 0;
    const long long w = T.P->wSkew;
    // 延遲的單位成本：用「C/L 最小」的型號估計一串 delay buffer 的成本
    long long Lbest = 1, Cbest = T.TT->minCost();
    double bestRatio = 1e300;
    for (const BufType& t : T.P->lib) {
        if (t.F < 1 || t.L < 1) continue;
        double r = (double)t.C / (double)t.L;
        if (r < bestRatio) { bestRatio = r; Lbest = t.L; Cbest = t.C; }
    }
    auto padCost = [&](long long delta) {
        long long k = (delta + Lbest - 1) / Lbest;
        return std::max(T.TT->minCost(), k * Cbest);
    };

    int total = 0;
    for (int round = 0; round < 4; ++round) {
        std::vector<long long> arr = arrivals(T);
        const long long Tmax = T.nd[0].hi, Tmin = T.nd[0].lo;
        if (Tmax - Tmin <= 1) break;

        // 候選 low：在 [Tmin, Tmax] 均勻取 128 個 + 抽樣的 sink arrival
        std::vector<long long> cands;
        const int K = 128;
        for (int k = 0; k <= K; ++k) cands.push_back(Tmin + (Tmax - Tmin) * k / K);
        std::vector<long long> sinkArr;
        for (int i = 1; i <= T.N; ++i) sinkArr.push_back(arr[i]);
        std::sort(sinkArr.begin(), sinkArr.end());
        size_t step = std::max<size_t>(1, sinkArr.size() / 256);
        for (size_t i = 0; i < sinkArr.size(); i += step) cands.push_back(sinkArr[i] + 1);
        std::sort(cands.begin(), cands.end());
        cands.erase(std::unique(cands.begin(), cands.end()), cands.end());

        std::vector<Pad> plan, bestPlan;
        long long bestEst = w * (Tmax - Tmin);   // 不插任何 buffer 的估計
        for (long long low : cands) {
            planPads(T, arr, low, plan);
            long long est = w * (Tmax - std::max(low, Tmin));
            for (const Pad& pd : plan) est += padCost(pd.delta);
            if (est < bestEst) {
                bestEst = est;
                bestPlan = plan;
            }
        }
        if (bestPlan.empty()) break;

        Snapshot before;
        T.save(before);
        long long scoreBefore = T.score();
        int added = 0;
        for (const Pad& pd : bestPlan) {
            int a = 0;
            insertDelay(T, pd.v, pd.delta, a);
            added += a;
        }
        if (T.score() >= scoreBefore) {   // 沒有變好（例如有些插不進去）→ 這一輪作廢
            T.load(before);
            break;
        }
        total += added;
    }
    return total;
}
