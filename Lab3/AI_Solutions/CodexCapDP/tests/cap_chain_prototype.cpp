#include "cap_chain_prototype.hpp"
#include "rowmap.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <queue>
#include <stdexcept>
#include <tuple>

namespace {
constexpr double EPS = 1e-7;
struct Candidate {
    int seg = 0, site = 0, count = 0;
    std::array<int, 2> blocker{{-1, -1}};
    double distance = 0, score = 0;
};

class Search {
    const PlaceDB& db;
    Placement& p;
    const ChainOptions& opt;
    RowMap rows;
    std::vector<char> locked;
    std::vector<double> oldDistance;
    std::priority_queue<std::pair<double, int>> maximum;
    std::vector<int> path;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    int holeSeg = 0, holeSite = 0, remaining = 0;
    double cap = 0, rootDistance = 0;
    double targetCap = -1, deltaLimit = -EPS;
    ChainResult stats;

    bool expired() {
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= opt.seconds)
            stats.timedOut = true;
        return stats.timedOut;
    }
    double distance(int v, int s, int x) const {
        const auto& sg = db.segs[s];
        const auto& n = db.nodes[v];
        return std::abs(sg.x0 + x * sg.sp - n.gx) + std::abs(sg.y - n.gy);
    }

    // A position is usable iff it touches at most maxBlockers unlocked cells.
    // Pending cells are absent from RowMap; committed ancestors are locked.
    bool blockers(int s, int x, int width, Candidate& c) const {
        const auto& list = rows.cells(s);
        auto it = std::lower_bound(list.begin(), list.end(), x,
                                   [&](int v, int a) { return p.site[v] < a; });
        if (it != list.begin()) --it;
        for (; it != list.end() && p.site[*it] < x + width; ++it) {
            int v = *it;
            if (p.site[v] + db.sitesOf(v, db.segs[s]) <= x) continue;
            if (locked[v] || c.count == opt.maxBlockers) return false;
            c.blocker[c.count++] = v;
        }
        return true;
    }

    std::vector<Candidate> candidates(int v, bool root, int placed, int pending) {
        std::vector<int> rr;
        int target = db.nearestRow(db.nodes[v].gy), current = db.segs[p.seg[v]].row;
        for (int r = target - 4; r <= target + 4; ++r)
            if (r >= 0 && r < int(db.rows.size())) rr.push_back(r);
        for (int r = current - 2; r <= current + 2; ++r)
            if (r >= 0 && r < int(db.rows.size())) rr.push_back(r);
        rr.push_back(db.segs[holeSeg].row);
        std::sort(rr.begin(), rr.end());
        rr.erase(std::unique(rr.begin(), rr.end()), rr.end());
        std::vector<Candidate> result;
        for (int r : rr) for (int s : db.rows[r].segs) {
            if (expired()) return {};
            const auto& sg = db.segs[s];
            const auto& n = db.nodes[v];
            int w = db.sitesOf(v, sg);
            if (std::abs(n.h - sg.height) > 1e-6 || w < 1 || w > sg.numSites) continue;
            double reach = cap - std::abs(sg.y - n.gy);
            if (reach < -EPS) continue;
            // Clamp in floating point before converting: distant global
            // placements must not overflow a site index.
            double low = std::max(0.0, (n.gx - reach - sg.x0) / sg.sp);
            double high = std::min(double(sg.numSites - w), (n.gx + reach - sg.x0) / sg.sp);
            if (low > high + 1e-9) continue;
            int lo = int(std::ceil(low - 1e-9)), hi = int(std::floor(high + 1e-9));
            if (lo > hi) continue;
            double ideal = std::clamp((n.gx - sg.x0) / sg.sp, double(lo), double(hi));
            std::vector<int> xx{int(std::floor(ideal)), int(std::ceil(ideal)), lo, hi};
            if (s == holeSeg) xx.push_back(std::clamp(holeSite, lo, hi));
            const auto& list = rows.cells(s);
            int at = int(std::lower_bound(list.begin(), list.end(), ideal,
                         [&](int u, double x) { return p.site[u] < x; }) - list.begin());
            // The ideal site and nearby obstacle edges expose both gaps and
            // useful unequal-width exchanges without scanning every site.
            for (int k = std::max(0, at - 4); k < std::min(int(list.size()), at + 4); ++k) {
                int u = list[k], a = p.site[u], b = a + db.sitesOf(u, sg);
                for (int x : {a, b, a - w, b - w}) xx.push_back(std::clamp(x, lo, hi));
            }
            std::sort(xx.begin(), xx.end());
            xx.erase(std::unique(xx.begin(), xx.end()), xx.end());
            for (int x : xx) {
                if (s == p.seg[v] && x == p.site[v]) continue;
                double d = distance(v, s, x);
                if (d > cap + EPS || (root && d >= rootDistance - EPS)) continue;
                Candidate c;
                c.seg = s; c.site = x; c.distance = d;
                if (!blockers(s, x, w, c) || placed + 1 + pending + c.count > opt.depth) continue;
                // This optimistic score only orders candidates; it never
                // certifies infeasibility or substitutes for the final delta.
                c.score = d - oldDistance[v];
                for (int j = 0; j < c.count; ++j) c.score -= 0.5 * oldDistance[c.blocker[j]];
                result.push_back(c);
            }
        }
        stats.candidates += long(result.size());
        auto less = [](const Candidate& a, const Candidate& b) {
            return std::tie(a.score, a.distance, a.count, a.seg, a.site) <
                   std::tie(b.score, b.distance, b.count, b.seg, b.site);
        };
        std::sort(result.begin(), result.end(), less);
        // Retain the best empty positions as well as the occupied beam. This
        // prevents an attractive but infeasible chain from hiding every exit.
        int occupied = 0, empty = 0;
        result.erase(std::remove_if(result.begin(), result.end(), [&](const Candidate& c) {
            return c.count ? ++occupied > opt.branches : ++empty > std::max(4, opt.branches / 2);
        }), result.end());
        return result;
    }

    // Every cell in pending is absent from RowMap, but retains its original
    // coordinates in p until placed. On failure this entire state is restored.
    bool dfs(std::vector<int> pending, double delta) {
        if (remaining == 0 || expired()) return false;
        --remaining; ++stats.nodes;
        // Wider pending cells have fewer exits; place them before narrow ones.
        auto next = std::max_element(pending.begin(), pending.end(), [&](int a, int b) {
            return std::tie(db.nodes[a].w, oldDistance[a], a) < std::tie(db.nodes[b].w, oldDistance[b], b);
        });
        int v = *next;
        pending.erase(next);
        int oldS = p.seg[v], oldX = p.site[v];
        auto choices = candidates(v, path.empty(), int(path.size()), int(pending.size()));
        for (const auto& c : choices) {
            if (expired()) break;
            double value = delta + c.distance - oldDistance[v];
            if (pending.empty() && c.count == 0 && value >= deltaLimit) continue;
            if ((!pending.empty() || c.count) && remaining == 0) continue;
            std::array<int, 2> oldBS{}, oldBX{};
            std::vector<int> rest = pending;
            for (int j = 0; j < c.count; ++j) {
                int b = c.blocker[j];
                oldBS[j] = p.seg[b]; oldBX[j] = p.site[b];
                rows.remove(b); rest.push_back(b);
            }
            rows.insert(v, c.seg, c.site);
            locked[v] = 1; path.push_back(v);
            bool found = rest.empty() ? value < deltaLimit : dfs(std::move(rest), value);
            locked[v] = 0;
            if (found) {
                if (c.count > 1) ++stats.multiBlocker;
                return true;
            }
            path.pop_back();
            rows.remove(v); p.seg[v] = oldS; p.site[v] = oldX;
            for (int j = 0; j < c.count; ++j) rows.insert(c.blocker[j], oldBS[j], oldBX[j]);
        }
        return false;
    }

public:
    Search(const PlaceDB& d, Placement& q, const ChainOptions& o)
        : db(d), p(q), opt(o), rows(d, q), locked(d.nodes.size()), oldDistance(d.nodes.size()) {}

    ChainResult run(double D = -1) {
        targetCap = D;
        if (D >= 0) deltaLimit = std::max(1e-6, .02 * D);
        for (int v : db.movable) {
            oldDistance[v] = displacementOf(db, p, v);
            maximum.push({oldDistance[v], v});
        }
        for (int pass = 0; pass < opt.passes && !expired(); ++pass) {
            const auto before = displacement(db, p);
            auto order = db.movable;
            int count = std::min(int(order.size()), opt.roots);
            std::partial_sort(order.begin(), order.begin() + count, order.end(), [&](int a, int b) {
                return oldDistance[a] != oldDistance[b] ? oldDistance[a] > oldDistance[b] : a < b;
            });
            order.resize(count);
            long accepted = stats.accepted;
            for (int root : order) {
                if (expired()) break;
                ++stats.roots;
                rootDistance = oldDistance[root];
                if (targetCap >= 0 && rootDistance <= targetCap + EPS) continue;
                if (rootDistance < EPS) continue;
                while (!maximum.empty() && maximum.top().first != oldDistance[maximum.top().second]) maximum.pop();
                cap = targetCap >= 0 ? targetCap : maximum.top().first;
                holeSeg = p.seg[root]; holeSite = p.site[root]; remaining = opt.expansions;
                path.clear(); rows.remove(root);
                if (dfs({root}, 0)) {
                    ++stats.accepted;
                    stats.longest = std::max(stats.longest, int(path.size()));
                    // Essential: every successful chain refreshes its touched
                    // cells BEFORE another root computes a delta. A pass-old
                    // cache can otherwise accept a worsening subsequent move.
                    for (int v : path) {
                        oldDistance[v] = displacementOf(db, p, v);
                        maximum.push({oldDistance[v], v});
                        ++stats.refreshed;
                    }
                } else rows.insert(root, holeSeg, holeSite);
            }
            const auto after = displacement(db, p);
            if (opt.verbose)
                std::fprintf(stderr, "[chain] pass %d: %ld chains, total %.6f -> %.6f, max %.6f -> %.6f\n",
                             pass, stats.accepted - accepted, before.total, after.total, before.max, after.max);
            if (stats.accepted == accepted) break;
        }
        return stats;
    }
};
}  // namespace

bool repairCapChainsPrototype(const PlaceDB& db, Placement& p, double D, const ChainOptions& opt) {
    if (D < 0 || opt.seconds <= 0) return false;
    Placement q = p;
    Search(db, q, opt).run(D);
    if (!checkLegal(db, q).empty() || displacement(db, q).max > D + 1e-6) return false;
    p = std::move(q);
    return true;
}

ChainResult chainDisplacementPrototype(const PlaceDB& db, Placement& p, const ChainOptions& opt) {
    if (opt.passes < 0 || opt.depth < 1 || opt.depth > 32 || opt.branches < 1 || opt.expansions < 1 ||
        opt.roots < 1 || opt.maxBlockers < 1 || opt.maxBlockers > 2 || !std::isfinite(opt.seconds) || opt.seconds < 0)
        throw std::runtime_error("invalid chain options");
    if (db.movable.empty() || opt.passes == 0 || opt.seconds == 0) return {};
    if (!checkLegal(db, p).empty()) throw std::runtime_error("chain search requires a legal input");
    const Placement safe = p;
    const auto before = displacement(db, p);
    try {
        auto result = Search(db, p, opt).run();
        const auto after = displacement(db, p);
        if (!checkLegal(db, p).empty() || after.total > before.total + 1e-5 || after.max > before.max + 1e-6)
            throw std::runtime_error("chain invariant failed");
        if (opt.verbose)
            std::fprintf(stderr, "[chain] %ld roots, %ld nodes, %ld candidates, %ld accepted, longest %d, "
                         "%ld multi-blocker placements%s\n", result.roots, result.nodes, result.candidates,
                         result.accepted, result.longest, result.multiBlocker, result.timedOut ? " (time limit)" : "");
        return result;
    } catch (...) {
        p = safe;
        throw;
    }
}
