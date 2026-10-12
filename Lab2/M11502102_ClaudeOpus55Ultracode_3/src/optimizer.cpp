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
//   Reparent  把一個節點換到附近另一個 buffer 底下（原 parent 變空就刪掉 → 省 cost）；
//             35% 的機率換完後把新舊 parent 都重新放置（原本放不下的也可能放得下）
//   Swap      兩個節點交換 parent
//   Dissolve  刪掉一個 buffer，children 分給附近的 buffer
//   Pad       在某條邊中間插一個 buffer 繞路，讓太早到的 sink 變晚
//   Split     把一個 buffer 的部分 children 拉出來給新 buffer
//   Merge     兩個鄰近 buffer 合併成一個（或把 buffer child 收進 parent），省一個 buffer
//   Rebalance 兩個鄰近 buffer 的 children 沿兩者連線重新切成兩群，各自重新放置
//   Retarget  關鍵路徑上的 buffer 以「絕對目標 T_max」重新放置（不只對齊自己的 children）
//   SkewRepar 關鍵路徑上的節點換到「arrival 最合適」的鄰近 buffer（不加 buffer 就能平衡 skew）
//
// 速度：位置搜尋佔大部分時間，所以最佳化時用便宜的 fastSearch、佔用表用 bitmap，
//       每秒約可試 70~90 萬個 move（參考版約 20~25 萬）。
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
        // 各 move 的相對權重（順序同 moveName）；可用環境變數 CBI_W="30,7,18,..." 覆寫做實驗
        // （數值來自 tuning set 上的 ablation 實驗）
        double w[NMOVE] = {30, 7, 18, 8, 5, 7, 4, 8, 8, 5, 6};
        if (const char* e = std::getenv("CBI_W")) {
            for (int k = 0; k < NMOVE && *e; ++k) {
                w[k] = std::atof(e);
                while (*e && *e != ',') ++e;
                if (*e == ',') ++e;
            }
        }
        double acc = 0;
        for (int k = 0; k < NMOVE; ++k) cum[k] = (acc += w[k]);
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
    bool mine(int v) const;
    // attachedToRoot 的快取：同一個 Dissolve 裡的候選常有共同祖先，走過的路徑一起記下來
    std::vector<int> memoStamp;
    std::vector<char> memoVal;
    int memoTag = 1;
    std::vector<int> walk;
    bool attachedCached(int q);
    int randomBuffer();
    int randomNode();
    int criticalNode(bool minPath, bool bufferOnly);
    int cascadeEmpty(int p);
    bool roomFor(int q, int v) const;

    // 每種 move 的統計（-v 時印出）
    static constexpr int NMOVE = 11;
    const char* moveName[NMOVE] = {"Shift", "Replace", "Reparent",  "Swap",     "Dissolve", "Pad",
                                   "Split", "Merge",   "Rebalance", "Retarget", "SkewRepar"};
    long long mvTry[NMOVE] = {}, mvLegal[NMOVE] = {}, mvAcc[NMOVE] = {}, mvBetter[NMOVE] = {};
    double mvTime[NMOVE] = {};
    int lastMove = 0;
    double cum[NMOVE];   // move 機率的累積和

    bool doMove();
    bool runMove(int k);
    bool mvShift();
    bool mvReplace();
    bool mvReparent();
    bool mvSwap();
    bool mvDissolve();
    bool mvPad();
    bool mvSplit();
    bool mvMerge();
    bool mvRebalance();
    bool mvRetarget();
    bool mvSkewReparent();

    long long arrivalOf(int v) const;
    struct SpotMemo {
        uint64_t sig = 0;
        int bx = 0, by = 0;
        bool valid = false;
    };
    std::vector<SpotMemo> memo;
    uint64_t kidsSig(int b) const;
    bool bestSpotFor(int b, int& bx, int& by);

    void relocate(int b);
    int nearbyPartner(int a);
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

bool Optimizer::attachedCached(int q) {
    if (memoStamp.size() < T.nd.size()) {
        memoStamp.resize(T.nd.size(), 0);
        memoVal.resize(T.nd.size(), 0);
    }
    walk.clear();
    int v = q;
    bool res;
    while (true) {
        if (memoStamp[v] == memoTag) { res = memoVal[v]; break; }
        walk.push_back(v);
        int p = T.nd[v].parent;
        if (p < 0) { res = (v == 0); break; }
        v = p;
    }
    for (int u : walk) {
        memoStamp[u] = memoTag;
        memoVal[u] = res;
    }
    return res;
}

// 平行分工時只挑自己區域、而且不是區域根的節點當 move 的主角
bool Optimizer::mine(int v) const {
    if (!prm.region) return true;
    return (size_t)v < prm.region->size() && (*prm.region)[v] == prm.regionId &&
           !((size_t)v < prm.isRoot->size() && (*prm.isRoot)[v]);
}

int Optimizer::randomBuffer() {
    if (T.numBuf == 0) return -1;
    long long hi = (long long)T.nd.size() - 1;
    if (hi < T.N + 1) return -1;
    for (int i = 0; i < 64; ++i) {
        int v = (int)rng.range(T.N + 1, hi);
        if (T.nd[v].alive && mine(v)) return v;
    }
    return -1;
}

int Optimizer::randomNode() {
    long long hi = (long long)T.nd.size() - 1;
    if (hi < 1) return -1;
    for (int i = 0; i < 64; ++i) {
        int v = (int)rng.range(1, hi);
        if (T.nd[v].alive && mine(v)) return v;
    }
    return -1;
}

// 沿著「決定 T_min（或 T_max）」的路徑往下走，隨機挑路徑上一個節點
int Optimizer::criticalNode(bool minPath, bool bufferOnly) {
    path.clear();
    int u = 0;
    if (prm.roots && !prm.roots->empty()) {
        // 平行分工時：從自己區域裡「最早（或最晚）到的 sink 所在的區域根」往下走
        int bestR = -1;
        long long bestV = 0;
        for (int r : *prm.roots) {
            const Node& a = T.nd[r];
            if (a.cnt == 0) continue;
            long long A = arrivalOf(r);
            long long v = minPath ? A + a.lo : A + a.hi;
            if (bestR < 0 || (minPath ? v < bestV : v > bestV)) { bestR = r; bestV = v; }
        }
        if (bestR >= 0) u = bestR;
    }
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

// children 的指紋（與順序無關）：編號、座標、latency 都相同就代表最佳位置不會變
uint64_t Optimizer::kidsSig(int b) const {
    uint64_t h = 0x9E3779B97F4A7C15ULL * (uint64_t)(T.nd[b].ch.size() + 1);
    for (int c : T.nd[b].ch) {
        const Node& m = T.nd[c];
        uint64_t z = (uint64_t)c * 0xBF58476D1CE4E5B9ULL ^ (uint64_t)m.x * 0x94D049BB133111EBULL ^
                     (uint64_t)m.y * 0x2545F4914F6CDD1DULL ^ (uint64_t)m.lo * 0x9E3779B97F4A7C15ULL ^
                     (uint64_t)m.hi * 0xD6E8FEB86659FD93ULL;
        z ^= z >> 31;
        h += z * 0x94D049BB133111EBULL;
    }
    return h;
}

// b 的最佳位置。比「留在原地」好才回傳 true。
// children 沒變時直接用上次算好的結果（之前 Replace 會反覆對沒變的 buffer 做同樣的搜尋）。
bool Optimizer::bestSpotFor(int b, int& bx, int& by) {
    const std::vector<int>& K = T.nd[b].ch;
    if (memo.size() < T.nd.size()) memo.resize(T.nd.size());
    uint64_t sig = kidsSig(b);
    SpotMemo& m = memo[b];
    if (m.sig == sig && m.valid) {
        if (T.nd[b].x == m.bx && T.nd[b].y == m.by) return false;   // 已經在最佳位置
        if (T.usable(m.bx, m.by) && spotValue(T, K, m.bx, m.by) < spotValue(T, K, T.nd[b].x, T.nd[b].y)) {
            bx = m.bx;
            by = m.by;
            return true;
        }
    }
    double here = spotValue(T, K, T.nd[b].x, T.nd[b].y);
    int fx, fy;
    bool found = findParentSpot(T, K, 6, fx, fy, false, true);
    double there = found ? spotValue(T, K, fx, fy) : 1e300;
    m.sig = sig;
    m.valid = true;
    if (there < here) {
        m.bx = fx;
        m.by = fy;
        bx = fx;
        by = fy;
        return true;
    }
    m.bx = T.nd[b].x;   // 原地最好
    m.by = T.nd[b].y;
    return false;
}

// SRC 到 v 的路徑長
long long Optimizer::arrivalOf(int v) const {
    long long a = 0;
    while (T.nd[v].parent >= 0) {
        int p = T.nd[v].parent;
        a += T.dist(p, v);
        v = p;
    }
    return a;
}

// 把 buffer b 移到「讓它的 children latency 最對齊」的位置（transaction 中使用）
void Optimizer::relocate(int b) {
    const std::vector<int>& K = T.nd[b].ch;   // 搜尋過程不會改動樹，直接讀不用複製
    int bx, by;
    if (K.empty() || !bestSpotFor(b, bx, by)) return;   // 原地已經最好就不動
    int pp = T.nd[b].parent;
    T.moveTo(b, bx, by);
    T.refreshUp(b);
    T.refreshUp(pp);
}

// 找 a 附近的另一個 buffer（不在 a 的祖先 / 子孫關係上），找不到回傳 -1
int Optimizer::nearbyPartner(int a) {
    nearBuffers(T.nd[a].x, T.nd[a].y, 6, cand);
    for (int tries = 0; tries < 4 && !cand.empty(); ++tries) {
        int b = cand[rng.range(0, (long long)cand.size() - 1)];
        if (b == a || T.isAncestorOrSelf(a, b) || T.isAncestorOrSelf(b, a)) continue;
        return b;
    }
    return -1;
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
    if (b < 0 || T.nd[b].ch.empty()) return false;
    int bx, by;
    if (!bestSpotFor(b, bx, by)) return false;
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
    // relocate 版：換完 parent 後，新舊 parent 都重新放到最佳位置（原本放不下的也可能放得下）
    bool reloc = q != 0 && rng.chance(0.35);
    if (reloc ? (int)T.nd[q].ch.size() + 1 > TT.maxF() : !roomFor(q, v)) return false;
    T.begin();
    T.unlink(v);
    T.link(v, q);
    int r = cascadeEmpty(p);
    T.refreshUp(q);
    T.refreshUp(r);
    if (reloc) {
        relocate(q);
        if (r == p && p != 0) relocate(p);
    }
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
    ++memoTag;
    if (!T.nd[p].ch.empty()) T.refreshUp(p);
    for (size_t i = C.size(); i > 1; --i) std::swap(C[i - 1], C[rng.range(0, (long long)i - 1)]);
    for (int c : C) {
        nearBuffers(T.nd[c].x, T.nd[c].y, 8, cand);
        cand.push_back(p);
        int best = -1;
        long long bestD = LLONG_MAX;
        for (int q : cand) {
            if (q == c || !roomFor(q, c) || !attachedCached(q)) continue;
            long long d = T.dist(q, c);
            if (d < bestD) { bestD = d; best = q; }
        }
        if (best < 0) {
            T.rollback();
            return false;
        }
        T.link(c, best);
        T.refreshUp(best);
        ++memoTag;   // 接上一棵子樹後，「是否接在 SRC 底下」的快取失效
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
    if (!findParentSpot(T, tmp, 6, bx, by, false, true)) return false;
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

// 兩個鄰近 buffer 合併成一個（省一個 buffer）；b 是 a 的 child 時就是把 b 收進 a
bool Optimizer::mvMerge() {
    int a = randomBuffer();
    if (a < 0) return false;
    int b;
    bool collapse = rng.chance(0.25);
    if (collapse) {   // 挑 a 的一個 buffer child
        tmp.clear();
        for (int c : T.nd[a].ch)
            if (T.nd[c].kind == BUF_NODE) tmp.push_back(c);
        if (tmp.empty()) return false;
        b = tmp[rng.range(0, (long long)tmp.size() - 1)];
    } else {
        b = nearbyPartner(a);
        if (b < 0) return false;
        if (rng.chance(0.5)) std::swap(a, b);   // a 留下、b 被刪掉
    }
    int k = (int)(T.nd[a].ch.size() + T.nd[b].ch.size()) - (collapse ? 1 : 0);
    if (k > TT.maxF()) return false;
    // 快速檢查：中位數是總線長最小的位置，連它都超過最大 L 就不可能合併
    tmp.clear();
    for (int c : T.nd[a].ch) if (c != b) tmp.push_back(c);
    for (int c : T.nd[b].ch) tmp.push_back(c);
    if (minSumLen(T, tmp) > TT.maxLen(k)) return false;
    T.begin();
    int pb = T.nd[b].parent;
    std::vector<int> C = T.nd[b].ch;
    T.unlink(b);
    for (int c : C) {
        T.unlink(c);
        T.link(c, a);
    }
    T.removeBuffer(b);
    std::vector<int> K = T.nd[a].ch;
    int bx, by;
    if (!findParentSpot(T, K, 6, bx, by, false, true)) {
        T.rollback();
        return false;
    }
    int pa = T.nd[a].parent;
    T.moveTo(a, bx, by);
    T.refreshUp(a);
    T.refreshUp(pa);
    int r = cascadeEmpty(pb);
    T.refreshUp(r);
    return true;
}

// 兩個鄰近 buffer 的 children 合起來，沿兩者連線方向重新切成兩群，再各自重新放置
bool Optimizer::mvRebalance() {
    int a = randomBuffer();
    if (a < 0) return false;
    int b = nearbyPartner(a);
    if (b < 0) return false;
    std::vector<int> C = T.nd[a].ch;
    C.insert(C.end(), T.nd[b].ch.begin(), T.nd[b].ch.end());
    int n = (int)C.size(), Fm = TT.maxF();
    if (n < 2) return false;
    long long dx = (long long)T.nd[b].x - T.nd[a].x, dy = (long long)T.nd[b].y - T.nd[a].y;
    if (!dx && !dy) dx = 1;
    std::sort(C.begin(), C.end(), [&](int u, int v) {
        long long ku = T.nd[u].x * dx + T.nd[u].y * dy, kv = T.nd[v].x * dx + T.nd[v].y * dy;
        return ku != kv ? ku < kv : u < v;
    });
    // 用「中位數放 buffer 時最便宜型號的 cost」快速評估每個切點
    thread_local std::vector<int> g;
    auto estCost = [&](int from, int to) -> double {
        g.assign(C.begin() + from, C.begin() + to);
        long long mx, my, sum = 0;
        medianPoint(T, g, mx, my);
        for (int c : g) sum += manhattan(mx, my, T.nd[c].x, T.nd[c].y);
        int t = TT.pick(to - from, sum);
        return t < 0 ? 1e18 : (double)TT.cost(t) + 1e-6 * (double)sum;
    };
    int bestK = -1;
    double bestV = 1e17;
    for (int k = std::max(1, n - Fm); k <= std::min(n - 1, Fm); ++k) {
        double v = estCost(0, k) + estCost(k, n) + 0.5 * rng.uni();
        if (v < bestV) { bestV = v; bestK = k; }
    }
    if (bestK < 0) return false;
    T.begin();
    for (int c : C) T.unlink(c);
    for (int i = 0; i < n; ++i) T.link(C[i], i < bestK ? a : b);
    T.refreshUp(a);
    T.refreshUp(b);
    relocate(a);
    relocate(b);
    return true;
}

// 把關鍵路徑上的 buffer 放到「讓它底下的 sink 盡量接近 T_max」的位置（拉高 T_min / 壓低 T_max）
bool Optimizer::mvRetarget() {
    bool minSide = rng.chance(0.7);
    int b = criticalNode(minSide, true);
    if (b < 0) return false;
    int p = T.nd[b].parent;
    const Node& pn = T.nd[p];
    long long arrP = arrivalOf(p);
    long long target = T.tmax() - (minSide ? 0 : std::max<long long>(1, T.skew() / 4));
    long long restP = pn.sumLen - T.dist(p, b);
    long long capP = (p == 0) ? P.srcL : TT.maxLen((int)pn.ch.size());
    const std::vector<int> K = T.nd[b].ch;
    long long px = pn.x, py = pn.y;
    double w = (double)P.wSkew;
    auto eval = [&](long long x, long long y, double& val) {
        long long ep = manhattan(x, y, px, py);
        if (restP + ep > capP) return false;
        long long lo = LLONG_MAX, hi = LLONG_MIN, sum = 0;
        for (int c : K) {
            const Node& m = T.nd[c];
            long long e = manhattan(x, y, m.x, m.y);
            sum += e;
            lo = std::min(lo, e + m.lo);
            hi = std::max(hi, e + m.hi);
        }
        int t = TT.pick((int)K.size(), sum);
        if (t < 0) return false;
        long long A = arrP + ep;
        double over = (double)std::max<long long>(0, A + hi - target);
        double under = (double)std::max<long long>(0, target - (A + lo));
        val = w * (2.0 * over + under) + (double)TT.cost(t) + 1e-6 * (double)sum;
        return true;
    };
    // 搜尋範圍：children 與 parent 的 bounding box，再往外擴「需要多繞的距離」的一半
    long long x0 = px, x1 = px, y0 = py, y1 = py;
    for (int c : K) {
        x0 = std::min<long long>(x0, T.nd[c].x); x1 = std::max<long long>(x1, T.nd[c].x);
        y0 = std::min<long long>(y0, T.nd[c].y); y1 = std::max<long long>(y1, T.nd[c].y);
    }
    long long need = std::max<long long>(0, target - (arrP + T.dist(p, b) + T.nd[b].lo));
    long long m = need / 2 + 1;
    int bx, by;
    if (!searchSpot(T, x0 - m, y0 - m, x1 + m, y1 + m, fastSearch(8), eval, {{T.nd[b].x, T.nd[b].y}}, bx, by))
        return false;
    if (bx == T.nd[b].x && by == T.nd[b].y) return false;
    T.begin();
    T.moveTo(b, bx, by);
    T.refreshUp(b);
    T.refreshUp(p);
    return true;
}

// 關鍵路徑上的節點換到「到達時間最合適」的鄰近 buffer 底下：不加 buffer 就能平衡 skew
bool Optimizer::mvSkewReparent() {
    bool minSide = rng.chance(0.6);
    int v = criticalNode(minSide, false);
    if (v < 0) return false;
    int p = T.nd[v].parent;
    nearBuffers(T.nd[v].x, T.nd[v].y, 10, cand);
    cand.push_back(0);
    const long long Tmax = T.tmax(), Tmin = T.tmin();
    const long long lo = T.nd[v].lo, hi = T.nd[v].hi;
    int best = -1;
    double bestV = 1e300;
    for (int q : cand) {
        if (q == p || q == v || !roomFor(q, v)) continue;
        if (T.nd[v].kind == BUF_NODE && T.isAncestorOrSelf(v, q)) continue;
        long long A = arrivalOf(q) + T.dist(q, v);
        // 早到的要盡量晚（但不超過 T_max）；晚到的要盡量早（但不低於 T_min）
        double val = minSide ? (double)(Tmax - (A + hi)) : (double)((A + lo) - Tmin);
        if (val < 0) val = -4.0 * val;   // 超出範圍的懲罰較重
        if (val < bestV) { bestV = val; best = q; }
    }
    if (best < 0) return false;
    T.begin();
    T.unlink(v);
    T.link(v, best);
    int r = cascadeEmpty(p);
    T.refreshUp(best);
    T.refreshUp(r);
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
        case 6: return mvSplit();
        case 7: return mvMerge();
        case 8: return mvRebalance();
        case 9: return mvRetarget();
        default: return mvSkewReparent();
    }
}

bool Optimizer::doMove() {
    double r = rng.uni() * cum[NMOVE - 1];
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
    auto tempAt = [&](double frac) {
        double f = prm.schedFrom + (prm.schedTo - prm.schedFrom) * frac;
        return T0 * std::pow(T1 / T0, f);
    };
    double temp = tempAt(0.0);

    const long long saveGap = std::max<long long>(256, (long long)T.nd.size() / 50);
    long long it = 0, lastSave = 0;
    while (true) {
        if ((it & 255) == 0) {
            double t = elapsed();
            if (t >= prm.timeLimit) break;
            temp = tempAt(t / prm.timeLimit);
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
        if (!T.touchedLegal() ||
            (prm.region && !T.touchedOnlyRegion(*prm.region, *prm.isRoot, prm.regionId))) {
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
