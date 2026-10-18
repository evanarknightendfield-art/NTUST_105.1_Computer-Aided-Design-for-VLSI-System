// legalizer.cpp — Stage 1.
//
//   1. Abacus [Spindler et al., ISPD'08]: cells are taken in x order; each one is
//      tried in nearby rows, where it is appended to the row and overlapping
//      "clusters" are merged and moved to their best (least-squares) position.
//      The row with the smallest cost wins.
//   2. Max-displacement refinement: the cell with the largest displacement is
//      moved into free space or swapped with another cell, as long as nobody
//      ends up as far away. Repeats until the worst cell cannot be helped.
//   3. Total-displacement refinement: the same moves, accepted when they cut
//      the total without exceeding the maximum.
#include "legalizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "rowmap.hpp"

namespace {

constexpr double INF = std::numeric_limits<double>::infinity();

std::vector<int> xOrder(const PlaceDB& db) {
    std::vector<int> order = db.movable;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const Node &na = db.nodes[a], &nb = db.nodes[b];
        return na.gx != nb.gx ? na.gx < nb.gx : a < b;
    });
    return order;
}

// Visits rows in order of vertical distance from y; stops when the vertical
// distance alone is no better than best().
template <class Visit, class Best>
void forRowsNear(const PlaceDB& db, double y, Visit visit, Best best) {
    int R = (int)db.rows.size();
    int up = db.nearestRow(y), dn = up - 1;
    while (up < R || dn >= 0) {
        double dyUp = up < R ? std::fabs(db.rows[up].y - y) : INF;
        double dyDn = dn >= 0 ? std::fabs(db.rows[dn].y - y) : INF;
        if (std::min(dyUp, dyDn) >= best()) break;
        int r = dyUp <= dyDn ? up++ : dn--;
        visit(r, std::fabs(db.rows[r].y - y));
    }
}

// Lower bound of |dx| for putting a w-site cell with left edge at gx into segment s.
double dxBound(const Segment& s, int w, double gx) {
    double lo = s.x0, hi = s.x0 + (s.numSites - w) * s.sp;
    return gx < lo ? lo - gx : gx > hi ? gx - hi : 0.0;
}

int clampSite(double x, int w, int n) {
    long v = std::lround(x);
    return (int)std::max(0L, std::min((long)(n - w), v));
}

// ---------------------------------------------------------------- Tetris
// Greedy baseline from the lecture [Hill; Kahng & Wang ISPD'04]: every segment
// keeps its right edge; a cell goes to max(edge, gx) of the cheapest row.
bool tetris(const PlaceDB& db, Placement& P, std::string& err) {
    std::vector<int> edge(db.segs.size(), 0);
    for (int v : xOrder(db)) {
        const Node& n = db.nodes[v];
        double best = INF;
        int bestSeg = -1, bestSite = 0;
        forRowsNear(db, n.gy, [&](int r, double dy) {
            for (int s : db.rows[r].segs) {
                const Segment& sg = db.segs[s];
                int w = db.sitesOf(v, sg);
                if (edge[s] + w > sg.numSites || dy + dxBound(sg, w, n.gx) >= best) continue;
                int site = std::max(edge[s], clampSite((n.gx - sg.x0) / sg.sp, w, sg.numSites));
                double cost = std::fabs(sg.x0 + site * sg.sp - n.gx) + dy;
                if (cost < best) { best = cost; bestSeg = s; bestSite = site; }
            }
        }, [&] { return best; });
        if (bestSeg < 0) { err = "no room for " + n.name; return false; }
        P.seg[v] = bestSeg;
        P.site[v] = bestSite;
        edge[bestSeg] = bestSite + db.sitesOf(v, db.segs[bestSeg]);
    }
    return true;
}

// ---------------------------------------------------------------- Abacus
class Abacus {
public:
    Abacus(const PlaceDB& db, Placement& P, bool totalCost)
        : db_(db), P_(P), totalCost_(totalCost), st_(db.segs.size()) {}

    bool run(std::string& err) {
        for (int v : xOrder(db_)) {
            const Node& n = db_.nodes[v];
            double best = INF;
            int bestSeg = -1;
            forRowsNear(db_, n.gy, [&](int r, double dy) {
                for (int s : db_.rows[r].segs) {
                    if (dy + dxBound(db_.segs[s], db_.sitesOf(v, db_.segs[s]), n.gx) >= best) continue;
                    double c = trial(v, s, dy);
                    if (c < best) { best = c; bestSeg = s; }
                }
            }, [&] { return best; });
            if (bestSeg < 0) { err = "no room for " + n.name; return false; }
            commit(v, bestSeg);
        }
        return true;
    }

private:
    struct Cluster { int first; double e, q; int w, x; };   // first = index into SegState::cells
    struct SegState { std::vector<int> cells; std::vector<Cluster> cl; int used = 0; };

    double desired(int v, const Segment& s) const { return (db_.nodes[v].gx - s.x0) / s.sp; }

    // Cost of appending v to segment s, without changing anything.
    double trial(int v, int s, double dy) const {
        const Segment& sg = db_.segs[s];
        const SegState& S = st_[s];
        int w = db_.sitesOf(v, sg);
        if (S.used + w > sg.numSites) return INF;
        double e = 1, q = desired(v, sg);
        int W = w, k = (int)S.cl.size() - 1;
        int x = clampSite(q / e, W, sg.numSites);
        while (k >= 0 && S.cl[k].x + S.cl[k].w > x) {   // overlaps the cluster on its left: merge
            const Cluster& c = S.cl[k];
            q = c.q + q - e * c.w;
            e = c.e + e;
            W = c.w + W;
            x = clampSite(q / e, W, sg.numSites);
            --k;
        }
        double own = std::fabs((x + W - w) - desired(v, sg)) * sg.sp + dy;
        if (!totalCost_ || k + 1 == (int)S.cl.size()) return own;
        // Cells of the merged clusters move too: add their change in displacement.
        double delta = 0;
        int pos = x;
        for (size_t i = S.cl[k + 1].first; i < S.cells.size(); ++i) {
            int c = S.cells[i];
            double u = desired(c, sg);
            delta += std::fabs(pos - u) - std::fabs(P_.site[c] - u);
            pos += db_.sitesOf(c, sg);
        }
        return own + delta * sg.sp;
    }

    void commit(int v, int s) {
        const Segment& sg = db_.segs[s];
        SegState& S = st_[s];
        int w = db_.sitesOf(v, sg);
        S.cells.push_back(v);
        S.used += w;
        double u = desired(v, sg);
        S.cl.push_back({(int)S.cells.size() - 1, 1.0, u, w, clampSite(u, w, sg.numSites)});
        while (S.cl.size() >= 2) {
            Cluster& prev = S.cl[S.cl.size() - 2];
            const Cluster& last = S.cl.back();
            if (prev.x + prev.w <= last.x) break;
            prev.q += last.q - last.e * prev.w;
            prev.e += last.e;
            prev.w += last.w;
            prev.x = clampSite(prev.q / prev.e, prev.w, sg.numSites);
            S.cl.pop_back();
        }
        int pos = S.cl.back().x;
        for (size_t i = S.cl.back().first; i < S.cells.size(); ++i) {
            int c = S.cells[i];
            P_.seg[c] = s;
            P_.site[c] = pos;
            pos += db_.sitesOf(c, sg);
        }
    }

    const PlaceDB& db_;
    Placement& P_;
    bool totalCost_;
    std::vector<SegState> st_;
};

// ---------------------------------------------------------------- refinement
// Two local moves on a legal placement, both checked against a displacement bound:
//   insert: take cell c out and put it into segment s at site x, pushing the
//           neighbours on each side just far enough to make room;
//   swap:   c and b trade places (each must fit in the other's free room).
class Refiner {
public:
    Refiner(const PlaceDB& db, Placement& P, bool evict)
        : db_(db), P_(P), rm_(db, P), d_(db.nodes.size(), 0), evict_(evict) {
        for (int v : db.movable) d_[v] = displacementOf(db, P, v);
    }

    // Repeatedly moves the worst cell so that it and every cell it pushes end
    // up strictly below the current maximum. Stops when the worst cell is stuck.
    void reduceMax(bool verbose) {
        int moves = 0;
        for (;;) {
            int c = worst();
            double D = d_[c];
            Plan best;
            searchFor(c, D - minStep(), D, best);
            if (best.delta < INF) apply(c, best);
            else if (!evict_ || (!evict(c, D - minStep(), true) && !evict(c, D - minStep(), false))) break;
            ++moves;
        }
        if (verbose) {
            DispStats s = displacement(db_, P_);
            std::fprintf(stderr, "[legal] max-refine %d moves  total %.1f  max %.1f\n", moves, s.total, s.max);
        }
    }

    // Greedy passes over cells (worst first): any insert/swap that lowers the
    // total without pushing anybody above the current maximum is applied.
    void reduceTotal(bool verbose) {
        double bound = d_[worst()];
        for (int pass = 0; pass < 20; ++pass) {
            std::vector<int> order = db_.movable;
            std::sort(order.begin(), order.end(), [&](int a, int b) { return d_[a] > d_[b]; });
            double gain = 0;
            int moves = 0;
            for (int c : order) {
                if (d_[c] <= 0) break;
                Plan best;
                searchFor(c, bound, d_[c], best);
                if (best.delta < -1e-6) {
                    gain -= best.delta;
                    apply(c, best);
                    ++moves;
                }
            }
            if (verbose) {
                DispStats s = displacement(db_, P_);
                std::fprintf(stderr, "[legal] total-refine pass %d: %d moves  total %.1f  max %.1f\n", pass, moves,
                             s.total, s.max);
            }
            if (gain < 5e-4 * displacement(db_, P_).total) break;
        }
    }

private:
    struct Plan {
        double delta = INF;
        int seg = -1, site = 0;                    // new place of c
        int swapWith = -1, swapSeg = -1, swapSite = 0;
        std::vector<std::pair<int, int>> pushes;   // (cell, new site) in seg
    };

    double minStep() const { return db_.segs[0].sp; }

    int worst() const {
        int w = db_.movable[0];
        for (int v : db_.movable)
            if (d_[v] > d_[w]) w = v;
        return w;
    }

    double dispAt(int v, const Segment& s, int site) const {
        const Node& n = db_.nodes[v];
        return std::fabs(s.x0 + site * s.sp - n.gx) + std::fabs(s.y - n.gy);
    }

    // Two-step move for a worst cell c that cannot move by itself: a cell b
    // near c's target (one with plenty of slack) is lifted out, c takes its
    // room, and b is re-inserted somewhere else. Everything stays within bound.
    // roomFirst: try cells whose room already fits c before the least displaced ones.
    bool evict(int c, double bound, bool roomFirst) {
        const Node& n = db_.nodes[c];
        std::vector<std::pair<double, int>> cand;
        forRowsNear(db_, n.gy, [&](int r, double dy) {
            for (int s : db_.rows[r].segs) {
                const Segment& sg = db_.segs[s];
                double rr = (bound - dy) / sg.sp, uc = (n.gx - sg.x0) / sg.sp;
                const auto& cs = rm_.cells(s);
                auto it = std::lower_bound(cs.begin(), cs.end(), uc - rr - db_.sitesOf(c, sg),
                                           [&](int v, double x) { return P_.site[v] < x; });
                int wc = db_.sitesOf(c, sg);
                for (; it != cs.end() && P_.site[*it] <= uc + rr; ++it)
                    if (*it != c)
                        cand.push_back({d_[*it] + (roomFirst && rm_.freeAround(*it).width() < wc ? 1e18 : 0.0), *it});
            }
        }, [&] { return bound; });
        size_t K = std::min<size_t>(cand.size(), 16);
        std::partial_sort(cand.begin(), cand.begin() + K, cand.end());

        for (size_t i = 0; i < K; ++i) {
            int b = cand[i].second, sb = P_.seg[b], xb = P_.site[b];
            rm_.remove(b);
            P_.seg[b] = -1;
            Plan pc;
            tryInsert(c, sb, bound, pc);
            if (pc.delta < INF) {
                int oc = P_.seg[c], ox = P_.site[c];
                std::vector<std::pair<int, int>> undo;
                for (auto [v, x] : pc.pushes) undo.push_back({v, P_.site[v]});
                apply(c, pc);
                Plan pb;
                searchFor(b, bound, bound, pb);
                if (pb.delta < INF) {
                    apply(b, pb);
                    return true;
                }
                rm_.remove(c);
                for (auto [v, x] : undo) {
                    rm_.slide(v, x);
                    d_[v] = displacementOf(db_, P_, v);
                }
                if (oc >= 0) rm_.insert(c, oc, ox);
                else P_.seg[c] = -1;
                d_[c] = displacementOf(db_, P_, c);
            }
            rm_.insert(b, sb, xb);
        }
        return false;
    }

    // Best move for c with every moved cell ending at displacement <= bound.
    // Only rows closer than `reach` (vertically) are searched. A cell that is
    // currently lifted out (seg -1) can only be inserted.
    void searchFor(int c, double bound, double reach, Plan& best) {
        const Node& n = db_.nodes[c];
        forRowsNear(db_, n.gy, [&](int r, double dy) {
            if (dy > bound) return;
            for (int s : db_.rows[r].segs) tryInsert(c, s, bound, best);
            if (P_.seg[c] < 0) return;
            // swap partner: whoever sits at c's global-placement x in this row
            for (int s : db_.rows[r].segs) {
                const Segment& sg = db_.segs[s];
                int t = (int)std::floor((n.gx + n.w / 2 - sg.x0) / sg.sp);
                if (t < 0 || t >= sg.numSites) continue;
                Gap g;
                int b = rm_.cellAt(s, t, g, c);
                if (b >= 0) trySwap(c, b, bound, best);
            }
        }, [&] { return reach; });
    }

    void tryInsert(int c, int s, double bound, Plan& best) {
        const Segment& sg = db_.segs[s];
        const Node& nc = db_.nodes[c];
        int wc = db_.sitesOf(c, sg);
        double dy = std::fabs(sg.y - nc.gy);
        double r = (bound - dy) / sg.sp;
        if (r < 0) return;
        double uc = (nc.gx - sg.x0) / sg.sp;
        int cLo = std::max(0, (int)std::ceil(uc - r - 1e-9));
        int cHi = std::min(sg.numSites - wc, (int)std::floor(uc + r + 1e-9));
        if (cLo > cHi) return;

        // the cells of s without c, as a view: L(j)
        const std::vector<int>& all = rm_.cells(s);
        int ic = P_.seg[c] == s ? rm_.indexOf(c) : -1;
        int m = (int)all.size() - (ic >= 0 ? 1 : 0);
        auto L = [&](int j) { return all[ic >= 0 && j >= ic ? j + 1 : j]; };
        // insertion index by center
        double cc = uc + wc / 2.0;
        int k0 = (int)(std::lower_bound(all.begin(), all.end(), cc, [&](int v, double x) {
                           return P_.site[v] + db_.sitesOf(v, sg) / 2.0 < x;
                       }) - all.begin());
        if (ic >= 0 && ic < k0) --k0;
        double oldC = d_[c];
        for (int k = std::max(0, k0 - 1); k <= std::min(m, k0 + 1); ++k) {
            int gLo = k > 0 ? P_.site[L(k - 1)] + db_.sitesOf(L(k - 1), sg) : 0;
            int gHi = k < m ? P_.site[L(k)] : sg.numSites;
            // candidate sites: c's own target, the gap's ends, the target clamped into the gap
            long xs[4] = {std::lround(uc), gLo, gHi - wc,
                          std::max<long>(gLo, std::min<long>(gHi - wc, std::lround(uc)))};
            for (long& x : xs) x = std::max<long>(cLo, std::min<long>(cHi, x));
            std::sort(xs, xs + 4);
            for (int i = 0; i < 4; ++i) {
                if (i > 0 && xs[i] == xs[i - 1]) continue;
                int x = (int)xs[i];
                double delta = dispAt(c, sg, x) - oldC;
                pushes_.clear();
                // left side: push left just enough
                int b = x;
                bool ok = true;
                for (int j = k - 1; j >= 0 && ok; --j) {
                    int v = L(j), wv = db_.sitesOf(v, sg);
                    if (P_.site[v] + wv <= b) break;
                    int nx = b - wv;
                    if (nx < 0 || !allowed(v, sg, nx, bound)) { ok = false; break; }
                    delta += dispAt(v, sg, nx) - d_[v];
                    pushes_.push_back({v, nx});
                    b = nx;
                }
                b = x + wc;
                for (int j = k; j < m && ok; ++j) {
                    int v = L(j), wv = db_.sitesOf(v, sg);
                    if (P_.site[v] >= b) break;
                    int nx = b;
                    if (nx + wv > sg.numSites || !allowed(v, sg, nx, bound)) { ok = false; break; }
                    delta += dispAt(v, sg, nx) - d_[v];
                    pushes_.push_back({v, nx});
                    b = nx + wv;
                }
                if (ok && delta < best.delta) {
                    best.delta = delta;
                    best.seg = s;
                    best.site = x;
                    best.swapWith = -1;
                    best.pushes = pushes_;
                }
            }
        }
    }

    // A pushed cell may move toward its target freely; away from it only up to the bound.
    bool allowed(int v, const Segment& s, int nx, double bound) const {
        double nd = dispAt(v, s, nx);
        return nd <= bound + 1e-9 || nd <= d_[v] + 1e-9;
    }

    void trySwap(int c, int b, double bound, Plan& best) {
        int sc = P_.seg[c], sb = P_.seg[b];
        const Segment &gc = db_.segs[sc], &gb = db_.segs[sb];
        int wc = db_.sitesOf(c, gb), wb = db_.sitesOf(b, gc);
        Gap roomC = rm_.freeAround(c, b), roomB = rm_.freeAround(b, c);
        if (sc == sb && std::abs(rm_.indexOf(c) - rm_.indexOf(b)) == 1) return;   // insert covers neighbours
        if (roomB.width() < wc || roomC.width() < wb) return;
        const Node &nc = db_.nodes[c], &nb = db_.nodes[b];
        int xc = (int)std::max<long>(roomB.lo, std::min<long>(roomB.hi - wc, std::lround((nc.gx - gb.x0) / gb.sp)));
        int xb = (int)std::max<long>(roomC.lo, std::min<long>(roomC.hi - wb, std::lround((nb.gx - gc.x0) / gc.sp)));
        double dc = dispAt(c, gb, xc), db = dispAt(b, gc, xb);
        if (dc > bound + 1e-9 || db > bound + 1e-9) return;
        double delta = dc + db - d_[c] - d_[b];
        if (delta < best.delta) {
            best.delta = delta;
            best.seg = sb;
            best.site = xc;
            best.swapWith = b;
            best.swapSeg = sc;
            best.swapSite = xb;
            best.pushes.clear();
        }
    }

    void apply(int c, const Plan& p) {
        if (p.swapWith >= 0) {
            rm_.exchange(c, p.swapWith, p.seg, p.site, p.swapSeg, p.swapSite);
            d_[p.swapWith] = displacementOf(db_, P_, p.swapWith);
        } else {
            if (P_.seg[c] >= 0) rm_.remove(c);
            for (auto [v, x] : p.pushes) {
                rm_.slide(v, x);
                d_[v] = displacementOf(db_, P_, v);
            }
            rm_.insert(c, p.seg, p.site);
        }
        d_[c] = displacementOf(db_, P_, c);
    }

    const PlaceDB& db_;
    Placement& P_;
    RowMap rm_;
    std::vector<double> d_;
    std::vector<std::pair<int, int>> pushes_;
    bool evict_;
};

}  // namespace

bool legalize(const PlaceDB& db, Placement& P, const LegalizeOptions& opt, std::string& err) {
    P.seg.assign(db.nodes.size(), -1);
    P.site.assign(db.nodes.size(), 0);
    bool ok = opt.method == "tetris" ? tetris(db, P, err) : Abacus(db, P, opt.totalCost).run(err);
    if (!ok) return false;
    if (opt.verbose) {
        DispStats d = displacement(db, P);
        std::fprintf(stderr, "[legal] %-7s total %.1f  max %.1f\n", opt.method.c_str(), d.total, d.max);
    }
    if (opt.refineMax || opt.refineTotal) {
        Refiner ref(db, P, opt.evict);
        // The total pass frees room that the max pass can use again, so alternate.
        double prevMax = INF;
        for (int round = 0; round < 4; ++round) {
            if (opt.refineMax) ref.reduceMax(opt.verbose);
            if (opt.refineTotal) ref.reduceTotal(opt.verbose);
            double mx = displacement(db, P).max;
            if (!opt.refineMax || mx > prevMax - 1e-9) break;
            prevMax = mx;
        }
    }
    return true;
}
