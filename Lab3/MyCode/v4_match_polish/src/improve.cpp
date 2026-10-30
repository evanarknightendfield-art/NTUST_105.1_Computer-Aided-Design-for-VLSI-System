// improve.cpp — v4：同大小 cells 的最小成本匹配、位移不變的 HPWL 交換（說明見 improve.hpp）
#include "improve.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <unordered_map>

namespace {

constexpr double INF = std::numeric_limits<double>::infinity();

double dispAt(const PlaceDB& db, int v, int seg, int site) {
    const Segment& s = db.segs[seg];
    const Node& n = db.nodes[v];
    return std::fabs(s.x0 + site * s.sp - n.gx) + std::fabs(s.y - n.gy);
}

// 同樣 (寬, 高) 的 cells 編成同一類：同一類的兩個 cell 互換位置一定還是合法
std::vector<int> sizeClasses(const PlaceDB& db, int& count) {
    std::map<std::pair<double, double>, int> id;
    std::vector<int> cls(db.nodes.size(), -1);
    for (int v : db.movable)
        cls[v] = id.emplace(std::make_pair(db.nodes[v].w, db.nodes[v].h), (int)id.size()).first->second;
    count = (int)id.size();
    return cls;
}

// 最小成本指派（Hungarian，O(n³)）：cost 是 n × n，回傳 col[i] = 第 i 個 cell 分到第幾個位置
std::vector<int> hungarian(const std::vector<double>& cost, int n) {
    // 位勢版本，1-indexed：u = row 位勢、v = column 位勢、p[j] = 佔 column j 的 row
    std::vector<double> u(n + 1, 0), v(n + 1, 0), minv(n + 1);
    std::vector<int> p(n + 1, 0), way(n + 1, 0);
    std::vector<char> used(n + 1);
    for (int i = 1; i <= n; ++i) {
        p[0] = i;
        int j0 = 0;
        std::fill(minv.begin(), minv.end(), INF);
        std::fill(used.begin(), used.end(), 0);
        do {
            used[j0] = 1;
            int i0 = p[j0], j1 = 0;
            double delta = INF;
            for (int j = 1; j <= n; ++j) {
                if (used[j]) continue;
                double cur = cost[(size_t)(i0 - 1) * n + (j - 1)] - u[i0] - v[j];
                if (cur < minv[j]) { minv[j] = cur; way[j] = j0; }
                if (minv[j] < delta) { delta = minv[j]; j1 = j; }
            }
            for (int j = 0; j <= n; ++j) {
                if (used[j]) { u[p[j]] += delta; v[j] -= delta; }
                else minv[j] -= delta;
            }
            j0 = j1;
        } while (p[j0] != 0);
        do {   // 沿著增廣路徑翻轉
            int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0);
    }
    std::vector<int> col(n);
    for (int j = 1; j <= n; ++j) col[p[j] - 1] = j - 1;
    return col;
}

}  // namespace

// ------------------------------------------------------------------ 同大小 cells 的匹配
double matchSameSize(const PlaceDB& db, Placement& P, double cap, Clock::time_point until, bool verbose) {
    constexpr int G = 16;              // 一組幾個 cell（Hungarian 是 O(G³)）
    constexpr double FORBID = 1e18;    // 位移超過 cap 的配對：不准
    int nClass = 0;
    std::vector<int> cls = sizeClasses(db, nClass);
    std::vector<std::vector<int>> byClass(nClass);
    for (int v : db.movable) byClass[cls[v]].push_back(v);

    double gain = 0;
    long groups = 0, changed = 0;
    std::vector<double> cost;
    std::vector<int> segOf(G), siteOf(G);
    // 4 輪：分組的方式輪流用「先 row 再 x」「先 x 再 row」，第 3、4 輪錯開半組，讓組的邊界不一樣
    for (int pass = 0; pass < 4; ++pass) {
        for (auto& list : byClass) {
            if (list.size() < 2) continue;
            auto key = [&](int v) {
                double x = P.x(db, v), y = P.y(db, v);
                return pass % 2 == 0 ? std::make_pair(y, x) : std::make_pair(x, y);
            };
            std::sort(list.begin(), list.end(), [&](int a, int b) { return key(a) < key(b); });
            int start = pass >= 2 ? -G / 2 : 0;
            for (int s = start; s < (int)list.size(); s += G) {
                if ((++groups & 63) == 0 && Clock::now() > until) goto done;
                int lo = std::max(0, s), hi = std::min((int)list.size(), s + G), k = hi - lo;
                if (k < 2) continue;
                for (int i = 0; i < k; ++i) { segOf[i] = P.seg[list[lo + i]]; siteOf[i] = P.site[list[lo + i]]; }
                cost.assign((size_t)k * k, 0);
                double before = 0;
                for (int i = 0; i < k; ++i)
                    for (int j = 0; j < k; ++j) {
                        double d = dispAt(db, list[lo + i], segOf[j], siteOf[j]);
                        cost[(size_t)i * k + j] = d > cap + 1e-9 ? FORBID : d;
                        if (i == j) before += d;
                    }
                std::vector<int> col = hungarian(cost, k);
                double after = 0;
                for (int i = 0; i < k; ++i) after += cost[(size_t)i * k + col[i]];
                if (after < before - 1e-6) {   // 現在的排法一定可行（都 <= cap），所以 after 不會是 FORBID
                    for (int i = 0; i < k; ++i) {
                        P.seg[list[lo + i]] = segOf[col[i]];
                        P.site[list[lo + i]] = siteOf[col[i]];
                    }
                    gain += before - after;
                    ++changed;
                }
            }
        }
    }
done:
    if (verbose) std::fprintf(stderr, "[legal] match  %ld groups changed  total -%.1f\n", changed, gain);
    return gain;
}

// ------------------------------------------------------------------ 位移不變的 HPWL 交換
namespace {

// 一條 net 某個方向的最小、次小、最大、次大（記住是誰），去掉一個 pin 之後的邊界 O(1) 就知道
struct Ends {
    double lo = INF, lo2 = INF, hi = -INF, hi2 = -INF;
    int ilo = -1, ihi = -1;
    void add(double x, int v) {
        if (x < lo) { lo2 = lo; lo = x; ilo = v; }
        else if (x < lo2) lo2 = x;
        if (x > hi) { hi2 = hi; hi = x; ihi = v; }
        else if (x > hi2) hi2 = x;
    }
    double lowWithout(int v) const { return ilo == v ? lo2 : lo; }
    double highWithout(int v) const { return ihi == v ? hi2 : hi; }
};
struct NetEnds { Ends x, y; };
struct Box { int net; double xl, xh, yl, yh; };   // 一條 net 去掉某個 cell 之後的 bounding box

class Swapper {
public:
    Swapper(const PlaceDB& db, Placement& P, double cap) : db_(db), P_(P), cap_(cap) {
        int nClass = 0;
        cls_ = sizeClasses(db, nClass);
        nClass_ = nClass;
        bucketOf_.assign(db.nodes.size(), -1);
        idx_.assign(db.nodes.size(), -1);
        for (int v : db.movable) {
            long key = (long)P.seg[v] * nClass_ + cls_[v];
            auto it = bucketId_.emplace(key, (int)buckets_.size()).first;
            if (it->second == (int)buckets_.size()) buckets_.emplace_back();
            buckets_[it->second].push_back(v);
            bucketOf_[v] = it->second;
        }
        for (auto& b : buckets_) {   // 每個 bucket = 一個 segment 裡同一類的 cells，依 site 排序
            std::sort(b.begin(), b.end(), [&](int a, int c) { return P_.site[a] < P_.site[c]; });
            for (size_t k = 0; k < b.size(); ++k) idx_[b[k]] = (int)k;
        }
        ends_.resize(db.nets.size());
        for (int n = 0; n < (int)db.nets.size(); ++n) rebuild(n);
    }

    // 一輪：每個 cell（依位置）找最好的交換對象；回傳 HPWL 少了多少
    double pass(int parity, Clock::time_point until, bool& timeUp) {
        std::vector<int> order = db_.movable;
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            return P_.seg[a] != P_.seg[b] ? P_.seg[a] < P_.seg[b] : P_.site[a] < P_.site[b];
        });
        if (parity) std::reverse(order.begin(), order.end());
        double gain = 0;
        long seen = 0;
        for (int a : order) {
            if ((++seen & 255) == 0 && Clock::now() > until) { timeUp = true; break; }
            double d;
            int b = propose(a, d);
            if (b >= 0) { swap(a, b); gain -= d; ++swaps_; }
        }
        return gain;
    }
    long swaps() const { return swaps_; }

private:
    double cx(int v) const { return P_.x(db_, v) + db_.nodes[v].w / 2; }
    double cy(int v) const { return P_.y(db_, v) + db_.nodes[v].h / 2; }

    void rebuild(int n) {
        NetEnds e;
        for (int v : db_.nets[n]) { e.x.add(cx(v), v); e.y.add(cy(v), v); }
        ends_[n] = e;
    }
    void boxesOf(int v, std::vector<Box>& out) const {
        out.clear();
        for (int n : db_.nodeNets[v]) {
            if (db_.nets[n].size() < 2) continue;
            const NetEnds& e = ends_[n];
            out.push_back({n, e.x.lowWithout(v), e.x.highWithout(v), e.y.lowWithout(v), e.y.highWithout(v)});
        }
    }
    static double cost(const Box& b, double x, double y) {
        return std::max(b.xh, x) - std::min(b.xl, x) + std::max(b.yh, y) - std::min(b.yl, y);
    }
    bool onNet(int v, int n) const {
        for (int m : db_.nodeNets[v]) if (m == n) return true;
        return false;
    }

    // a、b 交換後的 HPWL 變化（兩個都在的 net 不變）
    double hpwlChange(int a, int b) {
        double ax = cx(a), ay = cy(a), bx = cx(b), by = cy(b), d = 0;
        for (const Box& x : boxA_) if (!onNet(b, x.net)) d += cost(x, bx, by) - cost(x, ax, ay);
        boxesOf(b, boxB_);
        for (const Box& x : boxB_) if (!onNet(a, x.net)) d += cost(x, ax, ay) - cost(x, bx, by);
        return d;
    }

    // 交換後總位移沒變多、兩個都 <= cap 才行
    bool neutral(int a, int b) const {
        double xa = P_.x(db_, a), ya = P_.y(db_, a), xb = P_.x(db_, b), yb = P_.y(db_, b);
        const Node &A = db_.nodes[a], &B = db_.nodes[b];
        double na = std::fabs(xb - A.gx) + std::fabs(yb - A.gy), nb = std::fabs(xa - B.gx) + std::fabs(ya - B.gy);
        if (na > cap_ + 1e-9 || nb > cap_ + 1e-9) return false;
        double oa = std::fabs(xa - A.gx) + std::fabs(ya - A.gy), ob = std::fabs(xb - B.gx) + std::fabs(yb - B.gy);
        return na + nb <= oa + ob + 1e-9;
    }

    // a 的交換對象：a 的 optimal region 中點、a 和它的中間、a 自己附近，上下各 1 個 row，
    // 每個 segment 裡同一類的 cells 左右各 3 個
    int propose(int a, double& bestD) {
        boxesOf(a, boxA_);
        if (boxA_.empty()) return -1;
        ex_.clear(); ey_.clear();
        for (const Box& b : boxA_) { ex_.push_back(b.xl); ex_.push_back(b.xh); ey_.push_back(b.yl); ey_.push_back(b.yh); }
        std::sort(ex_.begin(), ex_.end());
        std::sort(ey_.begin(), ey_.end());
        size_t k = ex_.size() / 2;
        double ax = cx(a), ay = cy(a);
        double tx = std::clamp(ax, ex_[k - 1], ex_[k]), ty = std::clamp(ay, ey_[k - 1], ey_[k]);
        const Node& A = db_.nodes[a];
        double probes[3][2] = {{tx, ty}, {(ax + tx) / 2, (ay + ty) / 2}, {ax, ay}};
        cand_.clear();
        for (auto& pr : probes) {
            int r0 = db_.nearestRow(pr[1] - A.h / 2);
            for (int r = std::max(0, r0 - 1); r <= std::min((int)db_.rows.size() - 1, r0 + 1); ++r)
                for (int s : db_.rows[r].segs) {
                    auto it = bucketId_.find((long)s * nClass_ + cls_[a]);
                    if (it == bucketId_.end()) continue;
                    const auto& list = buckets_[it->second];
                    const Segment& sg = db_.segs[s];
                    double site = (pr[0] - A.w / 2 - sg.x0) / sg.sp;
                    int at = (int)(std::lower_bound(list.begin(), list.end(), site,
                                                    [&](int v, double x) { return P_.site[v] < x; }) - list.begin());
                    for (int j = std::max(0, at - 3); j < std::min((int)list.size(), at + 3); ++j)
                        if (list[j] != a && std::find(cand_.begin(), cand_.end(), list[j]) == cand_.end())
                            cand_.push_back(list[j]);
                }
        }
        int best = -1;
        bestD = -1e-7;
        for (int b : cand_) {
            if (!neutral(a, b)) continue;
            double d = hpwlChange(a, b);
            if (d < bestD) { bestD = d; best = b; }
        }
        return best;
    }

    void swap(int a, int b) {
        std::swap(P_.seg[a], P_.seg[b]);
        std::swap(P_.site[a], P_.site[b]);
        buckets_[bucketOf_[a]][idx_[a]] = b;
        buckets_[bucketOf_[b]][idx_[b]] = a;
        std::swap(bucketOf_[a], bucketOf_[b]);
        std::swap(idx_[a], idx_[b]);
        for (int n : db_.nodeNets[a]) rebuild(n);
        for (int n : db_.nodeNets[b]) rebuild(n);
    }

    const PlaceDB& db_;
    Placement& P_;
    double cap_;
    std::vector<int> cls_;
    int nClass_ = 0;
    std::unordered_map<long, int> bucketId_;   // segment × 類別 → bucket
    std::vector<std::vector<int>> buckets_;
    std::vector<int> bucketOf_, idx_;
    std::vector<NetEnds> ends_;
    std::vector<Box> boxA_, boxB_;
    std::vector<double> ex_, ey_;
    std::vector<int> cand_;
    long swaps_ = 0;
};

}  // namespace

double neutralSwap(const PlaceDB& db, Placement& P, double cap, Clock::time_point until, bool verbose) {
    if (db.movable.empty() || Clock::now() > until) return 0;
    Swapper sw(db, P, cap);
    double total = 0, first = 0;
    bool timeUp = false;
    int p = 0;
    for (; p < 20 && !timeUp; ++p) {
        double g = sw.pass(p & 1, until, timeUp);
        total += g;
        if (p == 0) first = g;
        if (g <= 0.02 * first) break;   // 這一輪的收穫不到第一輪的 2% 就停
    }
    if (verbose)
        std::fprintf(stderr, "[legal] neutral-swap %d passes  %ld swaps  HPWL -%.0f%s\n", p, sw.swaps(), total,
                     timeUp ? "  (time budget)" : "");
    return total;
}
