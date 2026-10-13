// optimizer.cpp — 模擬退火（Simulated Annealing）
//
// 能量函數 E = buffer cost + w_skew * (skew + beta * σ)
//   skew 只看最大與最小 arrival，大部分修改不會改到它（平原），
//   所以加上 σ（全部 sink arrival 的標準差）當作「方向感」。真正記錄的最佳解仍以 Score 為準。
//
// 每一步：隨機選一種 move → begin() 試做 → 檢查合法 → 依 Metropolis 準則接受或 rollback()
// move 種類：
//   Shift     把 buffer 移動一小段（半徑 1, 2, 4, ... 隨機）
//   Replace   把 buffer 移到「讓它的 children latency 最對齊」的位置
//   Reparent  把一個節點換到附近另一個 buffer 底下（原 parent 變空就刪掉 → 省 cost）
//   Swap      兩個節點交換 parent
//   Dissolve  刪掉一個 buffer，children 分給附近的 buffer
//   Pad       在某條邊中間插一個 buffer 繞路，讓太早到的 sink 變晚
//   Split     把一個 buffer 的部分 children 拉出來給新 buffer
#include "optimizer.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "placement.h"

namespace {

double envOr(const char* name, double def) {
    const char* v = std::getenv(name);
    return v ? std::atof(v) : def;
}

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 12345) {}
    uint64_t next() {   // splitmix64
        uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    long long range(long long lo, long long hi) {   // [lo, hi]
        return lo + (long long)(next() % (uint64_t)(hi - lo + 1));
    }
    double uni() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }
    bool chance(double p) { return uni() < p; }
};

class Optimizer {
public:
    Optimizer(Tree& t, const OptParams& p) : T(t), P(*t.P), TT(*t.TT), prm(p), rng(p.seed) {
        long long m = std::max(P.dimx, P.dimy);
        while ((1LL << (maxShiftLog + 1)) <= m / 2) ++maxShiftLog;
    }
    OptReport run();

private:
    Tree& T;
    const Problem& P;
    const TypeTable& TT;
    OptParams prm;
    Rng rng;
    int maxShiftLog = 0;

    // buffers 的空間格網（定期重建，用來找「附近的 buffer」）
    long long cs = 1;
    int gw = 1, gh = 1;
    std::vector<std::vector<int>> grid;
    std::vector<int> tmp, path, cand;

    double energy() const {
        return (double)T.totalCost + (double)P.wSkew * ((double)T.skew() + prm.beta * T.sigma());
    }
    int cellOf(long long x, long long y) const {
        long long cx = std::min<long long>(gw - 1, std::max<long long>(0, x / cs));
        long long cy = std::min<long long>(gh - 1, std::max<long long>(0, y / cs));
        return (int)(cy * gw + cx);
    }
    void rebuildGrid();
    void nearBuffers(long long x, long long y, size_t want, std::vector<int>& out);
    int randomBuffer();
    int randomNode();
    int criticalNode(bool minPath, bool bufferOnly);
    int cascadeEmpty(int p);
    bool roomFor(int q, int v) const;

    // 每種 move 的統計（-v 時印出）
    static constexpr int NMOVE = 7;
    const char* moveName[NMOVE] = {"Shift", "Replace", "Reparent", "Swap", "Dissolve", "Pad", "Split"};
    long long mvTry[NMOVE] = {}, mvLegal[NMOVE] = {}, mvAcc[NMOVE] = {}, mvBetter[NMOVE] = {};
    double mvTime[NMOVE] = {};
    int lastMove = 0;

    bool doMove();
    bool runMove(int k);
    bool mvShift();
    bool mvReplace();
    bool mvReparent();
    bool mvSwap();
    bool mvDissolve();
    bool mvPad();
    bool mvSplit();
};

void Optimizer::rebuildGrid() {
    double area = (double)P.dimx * (double)P.dimy;
    int nb = std::max(1, T.numBuf);
    cs = std::max<long long>(1, (long long)std::sqrt(area / nb * 2.0));
    gw = (int)(P.dimx / cs) + 1;
    gh = (int)(P.dimy / cs) + 1;
    while ((long long)gw * gh > 4000000) {
        cs *= 2;
        gw = (int)(P.dimx / cs) + 1;
        gh = (int)(P.dimy / cs) + 1;
    }
    grid.assign((size_t)gw * gh, {});
    for (size_t v = T.N + 1; v < T.nd.size(); ++v)
        if (T.nd[v].alive) grid[cellOf(T.nd[v].x, T.nd[v].y)].push_back((int)v);
}

void Optimizer::nearBuffers(long long x, long long y, size_t want, std::vector<int>& out) {
    out.clear();
    int cx = cellOf(x, y) % gw, cy = cellOf(x, y) / gw;
    auto take = [&](int gx, int gy) {
        if (gx < 0 || gy < 0 || gx >= gw || gy >= gh) return;
        for (int b : grid[(size_t)gy * gw + gx])
            if (T.nd[b].alive && T.nd[b].kind == BUF_NODE) out.push_back(b);
    };
    int maxR = std::max(gw, gh);
    for (int r = 0; r <= maxR; ++r) {
        if (r == 0) {
            take(cx, cy);
        } else {
            for (int d = -r; d <= r; ++d) {
                take(cx + d, cy - r);
                take(cx + d, cy + r);
                if (d != -r && d != r) {
                    take(cx - r, cy + d);
                    take(cx + r, cy + d);
                }
            }
        }
        if (out.size() >= want && r >= 1) break;
    }
}

int Optimizer::randomBuffer() {
    if (T.numBuf == 0) return -1;
    long long hi = (long long)T.nd.size() - 1;
    if (hi < T.N + 1) return -1;
    for (int i = 0; i < 64; ++i) {
        int v = (int)rng.range(T.N + 1, hi);
        if (T.nd[v].alive) return v;
    }
    return -1;
}

int Optimizer::randomNode() {
    long long hi = (long long)T.nd.size() - 1;
    if (hi < 1) return -1;
    for (int i = 0; i < 64; ++i) {
        int v = (int)rng.range(1, hi);
        if (T.nd[v].alive) return v;
    }
    return -1;
}

// 沿著「決定 T_min（或 T_max）」的路徑往下走，隨機挑路徑上一個節點
int Optimizer::criticalNode(bool minPath, bool bufferOnly) {
    path.clear();
    int u = 0;
    while (!T.nd[u].ch.empty()) {
        const Node& a = T.nd[u];
        int next = -1;
        for (int c : a.ch) {
            long long e = T.dist(u, c);
            if (minPath ? (e + T.nd[c].lo == a.lo) : (e + T.nd[c].hi == a.hi)) { next = c; break; }
        }
        if (next < 0) break;
        if (!bufferOnly || T.nd[next].kind == BUF_NODE) path.push_back(next);
        u = next;
    }
    if (path.empty()) return bufferOnly ? randomBuffer() : randomNode();
    return path[rng.range(0, (long long)path.size() - 1)];
}

// p 若是沒有 children 的 buffer 就刪掉，並往上連鎖處理；回傳第一個沒被刪的祖先
int Optimizer::cascadeEmpty(int p) {
    while (p > 0 && T.nd[p].kind == BUF_NODE && T.nd[p].alive && T.nd[p].ch.empty()) {
        int pp = T.nd[p].parent;
        T.unlink(p);
        T.removeBuffer(p);
        p = pp;
    }
    return p;
}

// q 能不能再多帶 v 這個 child（用目前的 sumLen 估計）
bool Optimizer::roomFor(int q, int v) const {
    const Node& a = T.nd[q];
    if (!a.alive || a.kind == SINK_NODE) return false;
    int k = (int)a.ch.size() + 1;
    long long len = a.sumLen + T.dist(q, v);
    if (a.kind == SRC_NODE) return k <= P.srcF && len <= P.srcL;
    return TT.pick(k, len) >= 0;
}

// ---------------- moves ----------------

bool Optimizer::mvShift() {
    int b = rng.chance(0.3) ? criticalNode(rng.chance(0.5), true) : randomBuffer();
    if (b < 0) return false;
    long long r = 1LL << rng.range(0, maxShiftLog);
    long long dx = rng.range(-r, r), dy = rng.range(-r, r);
    if (!dx && !dy) return false;
    long long nx = T.nd[b].x + dx, ny = T.nd[b].y + dy;
    if (!T.usable(nx, ny)) return false;
    int p = T.nd[b].parent;
    T.begin();
    T.moveTo(b, (int)nx, (int)ny);
    T.refreshUp(b);
    T.refreshUp(p);
    return true;
}

bool Optimizer::mvReplace() {
    int b = rng.chance(0.5) ? criticalNode(rng.chance(0.5), true) : randomBuffer();
    if (b < 0) return false;
    tmp = T.nd[b].ch;
    int bx, by;
    if (!findParentSpot(T, tmp, 6, bx, by)) return false;
    int p = T.nd[b].parent;
    T.begin();
    T.moveTo(b, bx, by);
    T.refreshUp(b);
    T.refreshUp(p);
    return true;
}

bool Optimizer::mvReparent() {
    int v = rng.chance(0.3) ? criticalNode(rng.chance(0.5), false) : randomNode();
    if (v < 0) return false;
    int p = T.nd[v].parent;
    nearBuffers(T.nd[v].x, T.nd[v].y, 8, cand);
    if (cand.empty() || rng.chance(0.05)) cand.push_back(0);
    int q = cand[rng.range(0, (long long)cand.size() - 1)];
    if (q == p || q == v) return false;
    if (T.nd[v].kind == BUF_NODE && T.isAncestorOrSelf(v, q)) return false;
    if (!roomFor(q, v)) return false;
    T.begin();
    T.unlink(v);
    T.link(v, q);
    int r = cascadeEmpty(p);
    T.refreshUp(q);
    T.refreshUp(r);
    return true;
}

bool Optimizer::mvSwap() {
    int v1 = rng.chance(0.3) ? criticalNode(rng.chance(0.5), false) : randomNode();
    if (v1 < 0) return false;
    int p1 = T.nd[v1].parent;
    nearBuffers(T.nd[v1].x, T.nd[v1].y, 6, cand);
    if (cand.empty()) return false;
    int p2 = cand[rng.range(0, (long long)cand.size() - 1)];
    if (p2 == p1 || T.nd[p2].ch.empty()) return false;
    int v2 = T.nd[p2].ch[rng.range(0, (long long)T.nd[p2].ch.size() - 1)];
    if (v2 == v1) return false;
    if (T.isAncestorOrSelf(v1, p2) || T.isAncestorOrSelf(v2, p1)) return false;
    T.begin();
    T.unlink(v1);
    T.unlink(v2);
    T.link(v1, p2);
    T.link(v2, p1);
    T.refreshUp(p1);
    T.refreshUp(p2);
    return true;
}

bool Optimizer::mvDissolve() {
    int b = randomBuffer();
    if (b < 0) return false;
    int p = T.nd[b].parent;
    std::vector<int> C = T.nd[b].ch;
    T.begin();
    T.unlink(b);
    for (int c : C) T.unlink(c);
    T.removeBuffer(b);
    if (!T.nd[p].ch.empty()) T.refreshUp(p);
    for (size_t i = C.size(); i > 1; --i) std::swap(C[i - 1], C[rng.range(0, (long long)i - 1)]);
    for (int c : C) {
        nearBuffers(T.nd[c].x, T.nd[c].y, 8, cand);
        cand.push_back(p);
        int best = -1;
        long long bestD = LLONG_MAX;
        for (int q : cand) {
            if (q == c || !roomFor(q, c) || !T.attachedToRoot(q)) continue;
            long long d = T.dist(q, c);
            if (d < bestD) { bestD = d; best = q; }
        }
        if (best < 0) {
            T.rollback();
            return false;
        }
        T.link(c, best);
        T.refreshUp(best);
    }
    int r = cascadeEmpty(p);
    T.refreshUp(r);
    return true;
}

bool Optimizer::mvPad() {
    int v = rng.chance(0.7) ? criticalNode(true, false) : randomNode();
    if (v < 0) return false;
    int p = T.nd[v].parent;
    long long nx, ny;
    const Node &a = T.nd[v], &b = T.nd[p];
    if (rng.chance(0.5)) {   // 在 v 周圍繞路：多走約 2r
        long long sk = std::max<long long>(1, T.skew());
        long long r = rng.range(1, std::max<long long>(1, sk / 2 + 1));
        long long dx = rng.range(-r, r);
        long long dy = (r - std::llabs(dx)) * (rng.chance(0.5) ? 1 : -1);
        nx = a.x + dx;
        ny = a.y + dy;
    } else {                 // 放在 p 與 v 之間（純 relay，可分擔 length）
        double t = rng.uni();
        long long j = std::max<long long>(1, T.dist(v, p) / 4);
        nx = b.x + (long long)std::llround(t * (a.x - b.x)) + rng.range(-j, j);
        ny = b.y + (long long)std::llround(t * (a.y - b.y)) + rng.range(-j, j);
    }
    if (!T.usable(nx, ny)) return false;
    T.begin();
    int nb = T.addBuffer((int)nx, (int)ny);
    T.unlink(v);
    T.link(v, nb);
    T.link(nb, p);
    T.refreshUp(nb);
    T.refreshUp(p);
    return true;
}

bool Optimizer::mvSplit() {
    int b = rng.chance(0.1) ? 0 : randomBuffer();
    if (b < 0) return false;
    int k = (int)T.nd[b].ch.size();
    if (k < 2) return false;
    int m = (k == 2) ? 2 : (int)rng.range(2, k - 1);
    int c0 = T.nd[b].ch[rng.range(0, k - 1)];
    tmp = T.nd[b].ch;
    std::partial_sort(tmp.begin(), tmp.begin() + m, tmp.end(),
                      [&](int x, int y) { return T.dist(c0, x) < T.dist(c0, y); });
    tmp.resize(m);
    int bx, by;
    if (!findParentSpot(T, tmp, 6, bx, by)) return false;
    T.begin();
    int nb = T.addBuffer(bx, by);
    for (int s : tmp) {
        T.unlink(s);
        T.link(s, nb);
    }
    T.link(nb, b);
    T.refreshUp(nb);
    T.refreshUp(b);
    return true;
}

bool Optimizer::runMove(int k) {
    switch (k) {
        case 0: return mvShift();
        case 1: return mvReplace();
        case 2: return mvReparent();
        case 3: return mvSwap();
        case 4: return mvDissolve();
        case 5: return mvPad();
        default: return mvSplit();
    }
}

bool Optimizer::doMove() {
    // 機率依序：Shift 40%, Replace 10%, Reparent 20%, Swap 10%, Dissolve 7%, Pad 9%, Split 4%
    static const double cum[NMOVE] = {0.40, 0.50, 0.70, 0.80, 0.87, 0.96, 1.0};
    double r = rng.uni();
    int k = 0;
    while (k < NMOVE - 1 && r >= cum[k]) ++k;
    lastMove = k;
    ++mvTry[k];
    if (!prm.verbose) return runMove(k);
    auto t0 = std::chrono::steady_clock::now();
    bool ok = runMove(k);
    mvTime[k] += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return ok;
}

OptReport Optimizer::run() {
    using clk = std::chrono::steady_clock;
    auto t0 = clk::now();
    auto elapsed = [&]() { return std::chrono::duration<double>(clk::now() - t0).count(); };
    OptReport rep;

    rebuildGrid();
    Snapshot best;
    T.save(best);
    long long bestScore = T.score();
    double E = energy();

    // 溫度：以「最便宜 buffer 的 cost」為尺度。T0 時偶爾願意多花一個 buffer 去探索，
    // 降到 T1 時幾乎只接受變好的 move。（可用環境變數 CBI_T0F / CBI_T1F 調整，方便實驗）
    double c = (double)std::max<long long>(1, TT.minCost());
    // 樹越大，同時被打亂的區域越多，所以起始溫度隨 sink 數下降
    double t0f = std::min(0.3, std::max(0.03, 0.3 * std::sqrt(50.0 / std::max(1, T.N))));
    double T0 = envOr("CBI_T0F", t0f) * c;
    double T1 = envOr("CBI_T1F", 0.01) * std::min(c, (double)P.wSkew);
    if (T1 > T0) T1 = T0;
    double temp = T0;

    const long long saveGap = std::max<long long>(256, (long long)T.nd.size() / 50);
    long long it = 0, lastSave = 0;
    while (true) {
        if ((it & 255) == 0) {
            double t = elapsed();
            if (t >= prm.timeLimit) break;
            temp = T0 * std::pow(T1 / T0, t / prm.timeLimit);
            if ((it & 16383) == 0) rebuildGrid();
            if (T.score() < bestScore && it - lastSave >= saveGap) {
                T.save(best);
                bestScore = T.score();
                lastSave = it;
            }
            if (prm.verbose && (it & ((1 << 18) - 1)) == 0)
                std::fprintf(stderr, "  [SA] t=%.1fs T=%.3g score=%lld (cost=%lld skew=%lld bufs=%d) best=%lld\n", t,
                             temp, T.score(), T.totalCost, T.skew(), T.numBuf, bestScore);
        }
        ++it;
        if (!doMove()) continue;
        if (!T.touchedLegal()) {
            T.rollback();
            continue;
        }
        ++mvLegal[lastMove];
        double E2 = energy();
        double d = E2 - E;
        if (d <= 0 || rng.uni() < std::exp(-d / temp)) {
            T.commit();
            E = E2;
            ++rep.accepted;
            ++mvAcc[lastMove];
            if (d < 0) ++mvBetter[lastMove];
            if (T.score() < bestScore && it - lastSave >= saveGap) {
                T.save(best);
                bestScore = T.score();
                lastSave = it;
            }
        } else {
            T.rollback();
        }
    }
    if (T.score() < bestScore) {
        T.save(best);
        bestScore = T.score();
    }
    T.load(best);
    rep.iters = it;
    rep.seconds = elapsed();
    if (prm.verbose) {
        std::fprintf(stderr, "  [SA] T0=%.3g T1=%.3g\n  move        tries    legal   accept  improve   us/try\n", T0, T1);
        for (int k = 0; k < NMOVE; ++k)
            std::fprintf(stderr, "  %-9s %8lld %8lld %8lld %8lld %8.2f\n", moveName[k], mvTry[k], mvLegal[k], mvAcc[k],
                         mvBetter[k], mvTry[k] ? 1e6 * mvTime[k] / mvTry[k] : 0.0);
    }
    return rep;
}

}  // namespace

OptReport optimize(Tree& T, const OptParams& prm) {
    Optimizer opt(T, prm);
    return opt.run();
}
