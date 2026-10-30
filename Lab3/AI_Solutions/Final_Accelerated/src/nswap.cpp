// nswap.cpp — displacement-neutral HPWL swaps (see nswap.hpp).
// Snapshot proposals run in parallel, then are rechecked and committed in cell order.
// Equal physical sizes exchange occupied slots, including on rows with different pitches.
// Shared nets keep exactly the same set of centers. Other nets use exact cached extrema;
// first/second extrema exclude one cell in O(1), and accepted swaps rebuild affected nets.
#include "nswap.hpp"
#include "pool.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>

namespace {
constexpr double INF = std::numeric_limits<double>::infinity();
constexpr double NEUTRAL = 1e-9;   // equal L1 costs can differ by rounding
constexpr double GAIN = 1e-7;
struct NetBox { int net; double lx, hx, ly, hy; };

// The second value includes ties: excluding the first owner then preserves an
// extreme when another pin has the same coordinate. Nets contain unique node IDs.
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
struct CachedNet { Ends x, y; };

struct Swapper {
    const PlaceDB& db;
    Placement& p;
    double cap;
    bool ready = false;
    std::vector<int> cls, idx, bucketOf;
    // Each bucket contains one size class in one segment, sorted by occupied site.
    // Swapping equal-size cells keeps this set of occupied sites fixed.
    std::vector<std::map<int, int>> bucketIds;
    std::vector<std::vector<int>> buckets;
    std::vector<CachedNet> netBoxes;

    Swapper(const PlaceDB& d, Placement& pl, double c, double until) : db(d), p(pl), cap(c) {
        cls.assign(db.nodes.size(), -1);
        idx.assign(db.nodes.size(), -1);
        bucketOf.assign(db.nodes.size(), -1);
        bucketIds.resize(db.segs.size());
        std::map<std::pair<double, double>, int> ids;
        size_t count = 0;
        for (int v : db.movable) {
            if ((count++ % 1024) == 0 && wallSeconds() >= until) return;
            auto it = ids.emplace(std::make_pair(db.nodes[v].w, db.nodes[v].h), int(ids.size())).first;
            cls[v] = it->second;
            auto entry = bucketIds[p.seg[v]].emplace(cls[v], int(buckets.size()));
            if (entry.second) buckets.emplace_back();
            const int b = entry.first->second;
            buckets[b].push_back(v);
            bucketOf[v] = b;
        }
        for (size_t b = 0; b < buckets.size(); ++b) {
            if ((b % 256) == 0 && wallSeconds() >= until) return;
            auto& cells = buckets[b];
            std::sort(cells.begin(), cells.end(), [&](int a, int v) { return p.site[a] < p.site[v]; });
            for (size_t k = 0; k < cells.size(); ++k) idx[cells[k]] = int(k);
        }
        netBoxes.resize(db.nets.size());
        for (int n = 0; n < int(db.nets.size()); ++n) {
            if ((n % 256) == 0 && wallSeconds() >= until) return;
            rebuild(n);
        }
        ready = wallSeconds() < until;
    }

    double cx(int v) const { return p.x(db, v) + db.nodes[v].w / 2; }
    double cy(int v) const { return p.y(db, v) + db.nodes[v].h / 2; }
    void rebuild(int n) {
        CachedNet box;
        for (int v : db.nets[n]) { box.x.add(cx(v), v); box.y.add(cy(v), v); }
        netBoxes[n] = box;
    }
    void boxesOf(int v, std::vector<NetBox>& out) const {
        out.clear();
        for (int n : db.nodeNets[v]) {
            if (db.nets[n].size() < 2) continue;
            const CachedNet& b = netBoxes[n];
            out.push_back({n, b.x.lowWithout(v), b.x.highWithout(v), b.y.lowWithout(v), b.y.highWithout(v)});
        }
    }
    static double cost(const NetBox& b, double x, double y) {
        return std::max(b.hx, x) - std::min(b.lx, x) + std::max(b.hy, y) - std::min(b.ly, y);
    }
    bool onNet(int v, int n) const {
        for (int m : db.nodeNets[v]) if (m == n) return true;
        return false;
    }
    double dispChange(int a, int b) const {
        const Node& A = db.nodes[a];
        const Node& B = db.nodes[b];
        const double xa = p.x(db, a), ya = p.y(db, a), xb = p.x(db, b), yb = p.y(db, b);
        const double da = std::fabs(xa - A.gx) + std::fabs(ya - A.gy);
        const double dbb = std::fabs(xb - B.gx) + std::fabs(yb - B.gy);
        const double dan = std::fabs(xb - A.gx) + std::fabs(yb - A.gy);
        const double dbn = std::fabs(xa - B.gx) + std::fabs(ya - B.gy);
        if (dan > cap || dbn > cap) return INF;
        return (dan + dbn) - (da + dbb);
    }
    // Exact pair delta: one member may individually lose HPWL; only the sum matters.
    double hpwlChange(int a, int b, const std::vector<NetBox>& boxesA, std::vector<NetBox>& scratch) const {
        const double ax = cx(a), ay = cy(a), bx = cx(b), by = cy(b);
        double delta = 0;
        for (const NetBox& nb : boxesA)
            if (!onNet(b, nb.net)) delta += cost(nb, bx, by) - cost(nb, ax, ay);
        boxesOf(b, scratch);
        for (const NetBox& nb : scratch)
            if (!onNet(a, nb.net)) delta += cost(nb, ax, ay) - cost(nb, bx, by);
        return delta;
    }
    static double median(std::vector<double>& ends, double current) {
        if (ends.empty()) return current;
        std::sort(ends.begin(), ends.end());
        const size_t k = ends.size() / 2;
        return std::clamp(current, ends[k - 1], ends[k]);
    }
    struct Scratch { std::vector<NetBox> a, b; std::vector<double> ex, ey; std::vector<int> cand; };
    std::pair<int, double> propose(int a, const NeutralSwapOptions& opt, Scratch& s, long& cands, long& neutral) const {
        boxesOf(a, s.a);
        if (s.a.empty()) return {-1, 0};
        const Node& A = db.nodes[a];
        const double ax = cx(a), ay = cy(a);
        s.ex.clear(); s.ey.clear();
        for (const NetBox& b : s.a) { s.ex.push_back(b.lx); s.ex.push_back(b.hx); s.ey.push_back(b.ly); s.ey.push_back(b.hy); }
        const double tx = median(s.ex, ax), ty = median(s.ey, ay);
        if (!std::isfinite(tx) || !std::isfinite(ty)) return {-1, 0};
        s.cand.clear();
        // A shared net can pin the single-cell optimum at its current location,
        // while a nearby exchange still gains because that shared net stays fixed.
        const double probes[3][2] = {{tx, ty}, {0.5 * ax + 0.5 * tx, 0.5 * ay + 0.5 * ty}, {ax, ay}};
        for (const auto& pr : probes) {
            const int r0 = db.nearestRow(pr[1] - A.h / 2);
            for (int r = r0 - opt.rowsAround; r <= r0 + opt.rowsAround; ++r) {
                if (r < 0 || r >= int(db.rows.size())) continue;
                for (int sg : db.rows[r].segs) {
                    const Segment& S = db.segs[sg];
                    if (std::fabs(S.height - A.h) > 1e-6) continue;
                    const auto found = bucketIds[sg].find(cls[a]);
                    if (found == bucketIds[sg].end()) continue;
                    const auto& list = buckets[found->second];
                    const double raw = (pr[0] - A.w / 2 - S.x0) / S.sp;
                    const int site = int(std::floor(std::clamp(raw, 0.0, double(S.numSites)) + 0.5));
                    const int at = int(std::lower_bound(list.begin(), list.end(), site,
                                                        [&](int v, int x) { return p.site[v] < x; }) - list.begin());
                    for (int j = std::max(0, at - opt.cellsAround); j < std::min(int(list.size()), at + opt.cellsAround); ++j) {
                        const int b = list[j];
                        if (b == a || std::find(s.cand.begin(), s.cand.end(), b) != s.cand.end()) continue;
                        s.cand.push_back(b);
                    }
                }
            }
        }
        int best = -1;
        double bestGain = -GAIN;
        for (int b : s.cand) {
            ++cands;
            if (dispChange(a, b) > NEUTRAL) continue;
            ++neutral;
            const double g = hpwlChange(a, b, s.a, s.b);
            if (g < bestGain) { bestGain = g; best = b; }
        }
        return {best, bestGain};
    }
    void swap(int a, int b) {
        const int sa = p.seg[a], xa = p.site[a], sb = p.seg[b], xb = p.site[b];
        p.seg[a] = sb; p.site[a] = xb; p.seg[b] = sa; p.site[b] = xa;
        buckets[bucketOf[a]][idx[a]] = b;
        buckets[bucketOf[b]][idx[b]] = a;
        std::swap(idx[a], idx[b]);
        std::swap(bucketOf[a], bucketOf[b]);
        // Shared values stay fixed, but a boundary owned by a is now owned by b.
        // Repair owners too; otherwise a later exclusion could use the wrong second value.
        for (int n : db.nodeNets[a]) {
            if (!onNet(b, n)) { rebuild(n); continue; }
            auto exchangeOwner = [&](int& owner) {
                if (owner == a) owner = b;
                else if (owner == b) owner = a;
            };
            CachedNet& box = netBoxes[n];
            exchangeOwner(box.x.ilo); exchangeOwner(box.x.ihi);
            exchangeOwner(box.y.ilo); exchangeOwner(box.y.ihi);
        }
        for (int n : db.nodeNets[b]) if (!onNet(a, n)) rebuild(n);
    }
};
}  // namespace

NeutralSwapResult neutralSwap(const PlaceDB& db, Placement& p, const NeutralSwapOptions& opt) {
    NeutralSwapResult res;
    if (db.movable.empty() || opt.passes <= 0) return res;
    const double setupStart = wallSeconds();
    if (setupStart >= opt.until) { res.cut = true; res.stop = "deadline"; return res; }
    const double cap = displacement(db, p).max;
    Swapper sw(db, p, cap, opt.until);
    if (!sw.ready) {
        res.setupSeconds = wallSeconds() - setupStart;
        res.cut = true; res.stop = "deadline"; return res;
    }
    const int T = std::max(1, opt.threads);
    std::unique_ptr<Pool> pool;
    if (T > 1) pool.reset(new Pool(T));
    std::vector<Swapper::Scratch> scratch(T);
    const int chunk = std::max(1, opt.chunk);
    std::vector<int> propB(chunk);
    std::vector<long> cands(T), neutral(T);
    double firstGain = 0;
    const bool hpwlPolicy = opt.minRelativeRate > 0 || opt.minFractionalGain > 0;
    const bool hpwlKnown = opt.referenceHPWL > 0 || hpwlPolicy;
    // Capturing diagnostics alone never triggers another full-net calculation.
    // main already has the starting exact HPWL and can provide it for every policy.
    double currentHPWL = opt.referenceHPWL > 0 ? opt.referenceHPWL
        : hpwlPolicy ? totalHPWL(db, p) : 0;
    res.setupSeconds = wallSeconds() - setupStart;
    for (int pass = 0; pass < opt.passes && !res.cut; ++pass) {
        if (wallSeconds() >= opt.until) { res.cut = true; break; }
        const double t0 = wallSeconds();
        std::vector<int> order = db.movable;
        if (opt.spatialOrder) {
            std::vector<std::pair<long long, int>> key(order.size());
            for (size_t i = 0; i < order.size(); ++i) {
                const int v = order[i];
                key[i] = {(long long)db.segs[p.seg[v]].row * (1LL << 32) + (long long)std::llround(p.x(db, v) / db.segs[p.seg[v]].sp), v};
            }
            std::sort(key.begin(), key.end());
            for (size_t i = 0; i < order.size(); ++i) order[i] = key[i].second;
        }
        if (pass % 2) std::reverse(order.begin(), order.end());
        if (pass == 0) res.firstOrderSeconds = wallSeconds() - t0;
        long swaps = 0;
        long visited = 0;
        double gain = 0;
        for (size_t c0 = 0; c0 < order.size(); c0 += size_t(chunk)) {
            if (wallSeconds() >= opt.until) { res.cut = true; break; }
            const size_t c1 = std::min(order.size(), c0 + size_t(chunk));
            auto work = [&](int t) {
                for (size_t i = c0 + size_t(t); i < c1; i += size_t(T))
                    propB[i - c0] = sw.propose(order[i], opt, scratch[t], cands[t], neutral[t]).first;
            };
            if (pool) pool->run(T, work); else work(0);
            Swapper::Scratch& s = scratch[0];
            for (size_t i = c0; i < c1; ++i) {
                const int a = order[i], b = propB[i - c0];
                if (b < 0) continue;
                ++res.proposals;
                const double dd = sw.dispChange(a, b);
                if (dd > NEUTRAL) continue;
                sw.boxesOf(a, s.a);
                const double g = sw.hpwlChange(a, b, s.a, s.b);
                if (!(g < -GAIN)) continue;
                sw.swap(a, b);
                ++swaps;
                gain += g;
                res.dispChange += dd;
            }
            if (opt.capturePassStats) visited += long(c1 - c0);
        }
        if (pass == 0) {
            res.firstPassSeconds = wallSeconds() - t0;
            res.firstPassComplete = !res.cut;
        }
        res.swaps += swaps;
        res.hpwlGain += gain;
        res.passes = pass + 1;
        if (opt.verbose)
            std::fprintf(stderr, "[nswap] pass %d: %ld swaps, HPWL %+.6e  %.2fs%s\n", pass + 1, swaps, gain,
                         wallSeconds() - t0, res.cut ? " (deadline)" : "");
        if (pass == 0) firstGain = gain;
        const double hpwlBefore = currentHPWL;
        double passSeconds = 0;
        auto capture = [&] {
            if (!opt.capturePassStats) return;
            NeutralSwapPassStats stat;
            stat.pass = pass + 1;
            stat.visited = visited;
            stat.swaps = swaps;
            stat.complete = !res.cut;
            stat.hpwlKnown = hpwlKnown;
            // Reuse the legacy rate's clock sample if that rule was active.
            stat.seconds = opt.minRelativeRate > 0 ? passSeconds : wallSeconds() - t0;
            stat.hpwlBefore = hpwlKnown ? hpwlBefore : 0;
            stat.hpwlAfter = hpwlKnown ? hpwlBefore + gain : 0;
            stat.hpwlGain = gain;
            stat.fractionalGain = hpwlKnown ? -gain / std::max(1.0, hpwlBefore) : 0;
            stat.firstGainRatio = firstGain < 0 ? gain / firstGain : 0;
            stat.relativeRate = stat.fractionalGain / std::max(1e-6, stat.seconds);
            res.passStats.push_back(stat);
        };
        if (opt.minRelativeRate > 0) {
            passSeconds = wallSeconds() - t0;
            const double rate = -gain / std::max(1.0, currentHPWL) / std::max(1e-6, passSeconds);
            currentHPWL += gain;
            if (opt.verbose) std::fprintf(stderr, "[nswap-rate] pass %d relative_gain_per_second %.6f threshold %.6f\n",
                                          pass+1, rate, opt.minRelativeRate);
            if (rate < opt.minRelativeRate) { capture(); res.stop = "rate"; break; }
        } else if (hpwlKnown) currentHPWL += gain;
        // A partial pass is governed only by the hard deadline. This rule uses
        // the same accepted gains and HPWL denominator as the rate rule, but no clock.
        if (!res.cut && opt.minFractionalGain > 0 &&
            -gain / std::max(1.0, hpwlBefore) < opt.minFractionalGain) {
            capture(); res.stop = "fraction"; break;
        }
        if (swaps == 0 || gain > opt.quiet * firstGain) { capture(); res.stop = "quiet"; break; }
        capture();
        if (pass + 1 >= opt.passes) res.stop = "passes";
    }
    if (res.cut) res.stop = "deadline";
    for (int t = 0; t < T; ++t) { res.candidates += cands[t]; res.neutral += neutral[t]; }
    return res;
}
