// legalizer.cpp — Stage 1 (v3 = v2 + final rounds below).
//
//   0. (v2) Flow-based spreading [Brenner DATE'12, Karimpour Darav ISPD'17]:
//      overflowed bins pass cells to neighbours along BFS paths, giving
//      Abacus spread-out targets. Run with and without it, keep the better.
//   1. Abacus [Spindler et al., ISPD'08]: cells are taken in x order; each one is
//      tried in nearby rows, where it is appended to the row and overlapping
//      "clusters" are merged and moved to their best (least-squares) position.
//      The row with the smallest cost wins.
//   2. (v2) Exact per-row re-placement with row and order fixed (convex cost,
//      pool-adjacent-violators) [RippleLG DAC'18, "fixed row & fixed order"].
//   3. Max-displacement refinement: the cell with the largest displacement is
//      moved into free space or swapped with another cell, as long as nobody
//      ends up as far away. Repeats until the worst cell cannot be helped.
//      (v2) Then same-width min-cost matching (Hungarian) around the worst
//      cells [RippleLG], alternating with the moves above.
//   4. Total-displacement refinement: (v2) matching over windows tiling the
//      chip, then the same moves, accepted when they cut the total without
//      exceeding the maximum.
//   5. (v3) Final rounds: exact row re-placement + matching + local moves
//      again until the total stops improving (the local moves leave rows a
//      little off their L1 optimum). Optional: swap-with-push (--swappush).
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
// v2: cells go to targets (tx, ty) — the global placement, or the spread
// positions from Spreader — instead of always the global placement.
class Abacus {
public:
    Abacus(const PlaceDB& db, Placement& P, bool totalCost, const std::vector<double>& tx,
           const std::vector<double>& ty)
        : db_(db), P_(P), totalCost_(totalCost), tx_(tx), ty_(ty), st_(db.segs.size()) {}

    bool run(std::string& err) {
        std::vector<int> order = db_.movable;
        std::sort(order.begin(), order.end(), [&](int a, int b) { return tx_[a] != tx_[b] ? tx_[a] < tx_[b] : a < b; });
        for (int v : order) {
            const Node& n = db_.nodes[v];
            double best = INF;
            int bestSeg = -1;
            forRowsNear(db_, ty_[v], [&](int r, double dy) {
                for (int s : db_.rows[r].segs) {
                    if (dy + dxBound(db_.segs[s], db_.sitesOf(v, db_.segs[s]), tx_[v]) >= best) continue;
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

    double desired(int v, const Segment& s) const { return (tx_[v] - s.x0) / s.sp; }

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
    const std::vector<double>& tx_;
    const std::vector<double>& ty_;
    std::vector<SegState> st_;
};

// ---------------------------------------------------------------- v2: flow-based spreading
// [Brenner, DATE'12; Karimpour Darav et al., ISPD'17; lecture L07 "network flow
// for legalization among regions"]. Cells are put into bins (2 rows x a few
// cells) by their global position. While a bin holds more cell width than it
// has free sites, a BFS over neighbouring bins finds the nearest bin with room;
// along that path every bin passes one cell to the next one, so each cell
// moves about one bin instead of the overflow travelling the whole distance.
// No cell may end up farther than a bound B from its global position; B grows
// only when some overflow cannot be resolved. Output: a target per cell.
class Spreader {
public:
    Spreader(const PlaceDB& db, int binRows, double binCells, int rounds) : db_(db), rounds_(rounds) {
        R_ = std::max(1, binRows);
        double avg = 0;
        for (int v : db.movable) avg += db.nodes[v].w;
        avg /= std::max<size_t>(1, db.movable.size());
        X0_ = 1e300;
        double X1 = -1e300;
        for (const Segment& s : db.segs) {
            X0_ = std::min(X0_, s.x0);
            X1 = std::max(X1, s.x0 + s.numSites * s.sp);
        }
        double sp = db.segs[0].sp;
        BW_ = std::max(sp, std::round(binCells * avg / sp) * sp);
        nx_ = std::max(1, (int)std::ceil((X1 - X0_) / BW_));
        ny_ = ((int)db.rows.size() + R_ - 1) / R_;
        bins_.resize((size_t)nx_ * ny_);
        for (int by = 0; by < ny_; ++by)
            for (int bx = 0; bx < nx_; ++bx) {
                Bin& b = bins_[id(bx, by)];
                b.x0 = X0_ + bx * BW_;
                b.x1 = b.x0 + BW_;
                int r0 = by * R_, r1 = std::min((int)db.rows.size(), r0 + R_);
                b.y0 = db.rows[r0].y;
                b.y1 = db.rows[r1 - 1].y + db.rows[r1 - 1].height;
                for (int r = r0; r < r1; ++r)
                    for (int s : db.rows[r].segs) {
                        const Segment& g = db.segs[s];
                        double lo = std::max(b.x0, g.x0), hi = std::min(b.x1, g.x0 + g.numSites * g.sp);
                        if (hi > lo) b.cap += (long)std::floor((hi - lo) / g.sp + 1e-9);
                    }
            }
        bin_.assign(db.nodes.size(), -1);
        for (int v : db.movable) {
            const Node& n = db.nodes[v];
            int by = std::min(ny_ - 1, db.nearestRow(n.gy) / R_);
            int bx = std::min(nx_ - 1, std::max(0, (int)std::floor((n.gx + n.w / 2 - X0_) / BW_)));
            bin_[v] = id(bx, by);
            bins_[bin_[v]].cells.push_back(v);
            bins_[bin_[v]].dem += width(v);
        }
    }

    void run(std::vector<double>& tx, std::vector<double>& ty, bool verbose) {
        double B = 2 * (BW_ + R_ * db_.rows[0].height);
        long moves = 0;
        for (int round = 0; round < rounds_; ++round) {
            std::vector<int> over;
            for (int b = 0; b < (int)bins_.size(); ++b)
                if (bins_[b].dem > bins_[b].cap) over.push_back(b);
            if (over.empty()) break;
            std::sort(over.begin(), over.end(), [&](int a, int b) {
                return bins_[a].dem - bins_[a].cap > bins_[b].dem - bins_[b].cap;
            });
            for (int b : over)
                while (bins_[b].dem > bins_[b].cap && moves < 50L * (long)db_.movable.size()) {
                    if (!augment(b, B)) break;
                    ++moves;
                }
            bool left = false;
            long nOver = 0, sumOver = 0;
            for (const Bin& b : bins_)
                if (b.dem > b.cap) { left = true; ++nOver; sumOver += b.dem - b.cap; }
            if (verbose) std::fprintf(stderr, "[spread] round %d B %.0f: %ld bins over by %ld sites, moves %ld\n", round, B, nOver, sumOver, moves);
            if (!left) break;
            B *= 1.5;
        }
        int moved = 0;
        double worst = 0;
        for (int v : db_.movable) {
            const Node& n = db_.nodes[v];
            const Bin& b = bins_[bin_[v]];
            tx[v] = std::min(std::max(n.gx, b.x0), std::max(b.x0, b.x1 - n.w));
            ty[v] = std::min(std::max(n.gy, b.y0), std::max(b.y0, b.y1 - n.h));
            double d = std::fabs(tx[v] - n.gx) + std::fabs(ty[v] - n.gy);
            moved += d > 0;
            worst = std::max(worst, d);
        }
        if (verbose)
            std::fprintf(stderr, "[legal] spread: %d x %d bins, %ld path moves, %d cells retargeted, max shift %.1f, B %.0f\n",
                         nx_, ny_, moves, moved, worst, B);
    }

private:
    struct Bin { double x0, x1, y0, y1; long cap = 0, dem = 0; std::vector<int> cells; };

    int id(int bx, int by) const { return by * nx_ + bx; }
    int width(int v) const { return db_.sitesOf(v, db_.segs[0]); }

    // Distance from v's global position to the nearest spot inside bin b.
    double dispTo(int v, int b) const {
        const Node& n = db_.nodes[v];
        const Bin& g = bins_[b];
        double hiX = std::max(g.x0, g.x1 - n.w), hiY = std::max(g.y0, g.y1 - n.h);
        double dx = n.gx < g.x0 ? g.x0 - n.gx : n.gx > hiX ? n.gx - hiX : 0;
        double dy = n.gy < g.y0 ? g.y0 - n.gy : n.gy > hiY ? n.gy - hiY : 0;
        return dx + dy;
    }

    // BFS from overflowed bin src to the nearest bin with room; moves one cell per hop.
    bool augment(int src, double B) {
        std::vector<int>& prev = prev_;
        std::vector<int>& via = via_;
        prev.assign(bins_.size(), -1);
        via.assign(bins_.size(), -1);
        std::vector<int> queue{src};
        prev[src] = src;
        for (size_t qi = 0; qi < queue.size(); ++qi) {
            int u = queue[qi];
            int ux = u % nx_, uy = u / nx_;
            const int nb[4][2] = {{ux - 1, uy}, {ux + 1, uy}, {ux, uy - 1}, {ux, uy + 1}};
            for (auto& q : nb) {
                if (q[0] < 0 || q[0] >= nx_ || q[1] < 0 || q[1] >= ny_) continue;
                int v = id(q[0], q[1]);
                if (prev[v] != -1 || bins_[v].cap == 0) continue;
                int best = -1;
                double bd = INF;
                for (int c : bins_[u].cells) {   // cheapest cell of u to hand over to v
                    double d = dispTo(c, v);
                    if (d <= B && d < bd) { bd = d; best = c; }
                }
                if (best < 0) continue;
                prev[v] = u;
                via[v] = best;
                if (bins_[v].cap - bins_[v].dem >= width(best)) {
                    for (int t = v; t != src; t = prev[t]) moveCell(via[t], prev[t], t);
                    return true;
                }
                queue.push_back(v);
            }
        }
        return false;
    }

    void moveCell(int c, int from, int to) {
        auto& cl = bins_[from].cells;
        cl.erase(std::find(cl.begin(), cl.end(), c));
        bins_[from].dem -= width(c);
        bins_[to].cells.push_back(c);
        bins_[to].dem += width(c);
        bin_[c] = to;
    }

    const PlaceDB& db_;
    int rounds_;   // B grows 1.5x per round; leftover overflow is small and Abacus absorbs it
    int R_, nx_, ny_;
    double X0_, BW_;
    std::vector<Bin> bins_;
    std::vector<int> bin_, prev_, via_;
};

// Min-cost perfect assignment (Hungarian algorithm with potentials, O(n^3)).
// cost is n x n, row-major; returns to[i] = column given to row i.
std::vector<int> hungarian(const std::vector<double>& cost, int n) {
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
        do { int j1 = way[j0]; p[j0] = p[j1]; j0 = j1; } while (j0);
    }
    std::vector<int> to(n);
    for (int j = 1; j <= n; ++j) to[p[j] - 1] = j - 1;
    return to;
}

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
        tried_.assign(db.nodes.size(), 0);
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
            if (gain < totalTol_ * displacement(db_, P_).total) break;
        }
    }

    double maxDisp() const { return d_[worst()]; }

    // ---- v2: exact re-placement of each row with its cell order fixed.
    // Minimises sum phi(d_i) per segment, where d_i = |x_i - u_i| + dy_i and
    // phi is convex (see phi()). With row and order fixed this is a chain-
    // constrained convex problem: pool-adjacent-violators gives the exact
    // optimum ("fixed row & fixed order" step of RippleLG, solved by clumping
    // instead of a dual min-cost flow). Cells may not go beyond `bound`.
    void optimizeRows(double delta0, double bound, bool verbose) {
        int changed = 0;
        for (size_t s = 0; s < db_.segs.size(); ++s) changed += optimizeSegment((int)s, delta0, bound);
        for (int v : db_.movable) d_[v] = displacementOf(db_, P_, v);
        if (verbose) {
            DispStats st = displacement(db_, P_);
            std::fprintf(stderr, "[legal] row-opt (d0 %.0f): %d segments  total %.1f  max %.1f\n", delta0, changed,
                         st.total, st.max);
        }
    }

    // ---- v2: same-width matching [RippleLG, DAC'18].
    // Cells of equal width can trade slots without any overlap. Around each
    // cell displaced more than delta0, every width class in a window is
    // re-assigned to its own slots by a min-cost assignment (Hungarian) with
    // cost phi(d): linear below delta0, steep above it. One assignment can move
    // a far cell inward and push a chain of nearby cells outward a little each,
    // which the one- and two-cell moves above cannot do.
    int matchWindows(double delta0, int maxWindows) {
        if (!db_.uniformSites) return 0;
        double cap = maxDisp();
        int windows = 0;
        for (int iter = 0; iter < maxWindows && work_ < kMatchWork; ++iter) {
            int c = -1;
            for (int v : db_.movable)
                if (d_[v] > delta0 && !tried_[v] && (c < 0 || d_[v] > d_[c])) c = v;
            if (c < 0) break;
            tried_[c] = 1;
            matchAround(c, delta0, cap);
            ++windows;
        }
        std::fill(tried_.begin(), tried_.end(), 0);
        return windows;
    }

    // Lowers the maximum: matching moves most of the cells above a slightly
    // lower threshold, then insert/swap/evict moves the rest, and repeat.
    void reduceMaxByMatching(bool verbose) {
        for (int round = 0; round < 12; ++round) {
            double D = maxDisp(), d0 = 0.95 * D;
            int above = 0;
            for (int v : db_.movable) above += d_[v] > d0;
            int w = matchWindows(d0, 400);
            int after = 0;
            for (int v : db_.movable) after += d_[v] > d0;
            reduceMax(false);
            if (verbose) {
                DispStats s = displacement(db_, P_);
                std::fprintf(stderr, "[legal] matching d0 %.0f: %d windows, cells above d0 %d -> %d  total %.1f  max %.1f\n",
                             d0, w, above, after, s.total, s.max);
            }
            if (maxDisp() > D - minStep()) break;
        }
    }

    // Matching for total displacement: windows of a few rows tile the chip
    // (twice, the second time shifted by half a window); every width class is
    // re-assigned with cost = displacement, never above the current maximum.
    void matchSweep(bool verbose) {
        if (!db_.uniformSites) return;
        double cap = maxDisp();
        double rowH = db_.rows[0].height, sp = db_.segs[0].sp;
        double wy = sweepRows_ * rowH, wx = sweepSites_ * sp;
        double x0 = 1e300, x1 = -1e300;
        for (const Segment& s : db_.segs) {
            x0 = std::min(x0, s.x0);
            x1 = std::max(x1, s.x0 + s.numSites * s.sp);
        }
        double y0 = db_.rows.front().y, y1 = db_.rows.back().y + rowH;
        for (int pass = 0; pass < 2; ++pass) {
            double off = pass ? 0.5 : 0.0;
            for (double y = y0 - off * wy; y < y1; y += wy)
                for (double x = x0 - off * wx; x < x1; x += wx)
                    matchBox(x, x + wx - 1e-9, y, y + wy - 1e-9, cap, cap, false, x + wx / 2, y + wy / 2);
        }
        if (verbose) {
            DispStats s = displacement(db_, P_);
            std::fprintf(stderr, "[legal] matching sweep: total %.1f  max %.1f\n", s.total, s.max);
        }
    }

private:
    double phi(double d, double delta0) const {
        if (!(delta0 > 0) || d <= delta0) return d;
        double r = d / delta0;
        return d * r * r * r * r;
    }

    // Returns 1 if the segment was re-placed.
    int optimizeSegment(int s, double delta0, double bound) {
        const Segment& sg = db_.segs[s];
        const std::vector<int>& cells = rm_.cells(s);
        int m = (int)cells.size(), n = sg.numSites;
        if (m == 0) return 0;
        std::vector<double> u(m), dy(m);
        std::vector<int> w(m), lo(m), hi(m);
        for (int j = 0; j < m; ++j) {
            int v = cells[j];
            const Node& nd = db_.nodes[v];
            u[j] = (nd.gx - sg.x0) / sg.sp;
            dy[j] = std::fabs(sg.y - nd.gy);
            w[j] = db_.sitesOf(v, sg);
            lo[j] = 0;
            hi[j] = n - w[j];
            if (bound < INF) {
                double r = (bound - dy[j]) / sg.sp;
                lo[j] = std::max(lo[j], (int)std::ceil(u[j] - r - 1e-9));
                hi[j] = std::min(hi[j], (int)std::floor(u[j] + r + 1e-9));
                lo[j] = std::min(lo[j], P_.site[v]);   // the current spot is always allowed
                hi[j] = std::max(hi[j], P_.site[v]);
            }
        }
        auto cellCost = [&](int j, int x) { return phi(std::fabs(x - u[j]) * sg.sp + dy[j], delta0); };
        struct Cl { int first, last, W, lo, hi, x; };
        // cost of a cluster whose first cell starts at site x
        auto F = [&](const Cl& c, int x) {
            double f = 0;
            for (int j = c.first; j <= c.last; ++j) {
                f += cellCost(j, x);
                x += w[j];
            }
            return f;
        };
        auto argmin = [&](Cl& c) {   // convex: first x where F(x+1) - F(x) >= 0
            int a = c.lo, b = c.hi;
            while (a < b) {
                int mid = a + (b - a) / 2;
                if (F(c, mid + 1) - F(c, mid) >= 0) b = mid;
                else a = mid + 1;
            }
            c.x = a;
        };
        std::vector<Cl> st;
        for (int j = 0; j < m; ++j) {
            Cl c{j, j, w[j], lo[j], hi[j], 0};
            argmin(c);
            while (!st.empty() && st.back().x + st.back().W > c.x) {
                const Cl& p = st.back();
                Cl mg{p.first, c.last, p.W + c.W, std::max(p.lo, c.lo - p.W), std::min(p.hi, c.hi - p.W), 0};
                if (mg.lo > mg.hi) return 0;   // cannot happen for a feasible start; keep the segment
                st.pop_back();
                c = mg;
                argmin(c);
            }
            st.push_back(c);
        }
        bool moved = false;
        for (const Cl& c : st) {
            int pos = c.x;
            for (int j = c.first; j <= c.last; ++j) {
                if (P_.site[cells[j]] != pos) moved = true;
                rm_.slide(cells[j], pos);
                pos += w[j];
            }
        }
        return moved ? 1 : 0;
    }

    // Window around a far cell c: its global-placement spot, its slot, and a margin.
    void matchAround(int c, double delta0, double cap) {
        const Node& nc = db_.nodes[c];
        double cx = P_.x(db_, c), cy = P_.y(db_, c), margin = 1.5 * d_[c];
        matchBox(std::min(nc.gx, cx) - margin, std::max(nc.gx, cx) + nc.w + margin, std::min(nc.gy, cy) - margin,
                 std::max(nc.gy, cy) + nc.h + margin, delta0, cap, true, nc.gx, nc.gy);
    }

    // Matching for all cells whose slot lies in [xl, xh] x [yl, yh]. cap: no cell
    // may end up farther than this (its own slot is always allowed). onlyFar:
    // skip width classes without a cell above delta0. Oversized classes keep
    // the cells nearest to (fx, fy).
    void matchBox(double xl, double xh, double yl, double yh, double delta0, double cap, bool onlyFar, double fx,
                  double fy) {
        // width class -> cells whose slot lies in the window
        std::vector<std::pair<int, int>> inWin;   // (width in sites, cell)
        for (size_t r = 0; r < db_.rows.size(); ++r) {
            if (db_.rows[r].y < yl || db_.rows[r].y > yh) continue;
            for (int s : db_.rows[r].segs) {
                const Segment& sg = db_.segs[s];
                for (int v : rm_.cells(s)) {
                    double x = sg.x0 + P_.site[v] * sg.sp;
                    if (x >= xl && x <= xh) inWin.push_back({db_.sitesOf(v, sg), v});
                }
            }
        }
        std::sort(inWin.begin(), inWin.end());
        bool changed = false;
        for (size_t a = 0; a < inWin.size();) {
            size_t b = a;
            while (b < inWin.size() && inWin[b].first == inWin[a].first) ++b;
            std::vector<int> grp;
            for (size_t k = a; k < b; ++k) grp.push_back(inWin[k].second);
            a = b;
            if (grp.size() < 2) continue;
            bool hasFar = false;   // only classes with a cell above delta0 can lower the maximum
            for (int v : grp) hasFar = hasFar || d_[v] > delta0;
            if (onlyFar && !hasFar) continue;
            if (grp.size() > kMaxGroup) {
                std::nth_element(grp.begin(), grp.begin() + kMaxGroup, grp.end(), [&](int p, int q) {
                    return std::fabs(P_.x(db_, p) - fx) + std::fabs(P_.y(db_, p) - fy) <
                           std::fabs(P_.x(db_, q) - fx) + std::fabs(P_.y(db_, q) - fy);
                });
                grp.resize(kMaxGroup);
            }
            int g = (int)grp.size();
            std::vector<int> slotSeg(g), slotSite(g);
            for (int k = 0; k < g; ++k) {
                slotSeg[k] = P_.seg[grp[k]];
                slotSite[k] = P_.site[grp[k]];
            }
            std::vector<double> cost((size_t)g * g);
            for (int i = 0; i < g; ++i) {
                const Node& ni = db_.nodes[grp[i]];
                for (int k = 0; k < g; ++k) {
                    const Segment& sg = db_.segs[slotSeg[k]];
                    double d = std::fabs(sg.x0 + slotSite[k] * sg.sp - ni.gx) + std::fabs(sg.y - ni.gy);
                    cost[(size_t)i * g + k] = (d > cap && k != i) ? 1e30 : phi(d, delta0);
                }
            }
            if (onlyFar) work_ += (double)g * g * g;
            std::vector<int> to = hungarian(cost, g);
            for (int i = 0; i < g; ++i) {
                if (to[i] == i) continue;
                changed = true;
                P_.seg[grp[i]] = slotSeg[to[i]];
                P_.site[grp[i]] = slotSite[to[i]];
            }
        }
        if (changed) {
            std::vector<int> segs;
            for (auto& [wsites, v] : inWin) segs.push_back(P_.seg[v]);
            rm_.rebuild(segs);
            for (auto& [wsites, v] : inWin) d_[v] = displacementOf(db_, P_, v);
        }
    }

    static constexpr size_t kMaxGroup = 300;

public:
    // v3 knobs: matching-sweep window (rows x sites) and the total pass's stop threshold
    int sweepRows_ = 6, sweepSites_ = 120;
    bool swapPush_ = false;
    double totalTol_ = 5e-4;

private:
    // Work budget for the max-displacement matching, in Hungarian steps (sum of g^3),
    // a few seconds; a count instead of a clock gives the same result on every machine.
    static constexpr double kMatchWork = 5e9;
    double work_ = 0;
    std::vector<char> tried_;   // cells whose window was already matched in this round

    struct Plan {
        double delta = INF;
        int seg = -1, site = 0;                    // new place of c
        int swapWith = -1, swapSeg = -1, swapSite = 0;
        std::vector<std::pair<int, int>> pushes;   // (cell, new site) in seg
        // v3 swap-with-push: partner b goes to (seg2, site2) pushing pushes2
        int partner = -1, seg2 = -1, site2 = 0;
        std::vector<std::pair<int, int>> pushes2;
    };

    // v3: c and b (different rows) trade rows; each is inserted into the other's
    // row with the new neighbours pushed just far enough (the plain swap needs
    // both to fit in the other's free room). Moves that the total-optimal
    // placements use but single-cell moves cannot reach.
    void trySwapPush(int c, int b, double bound, Plan& best) {
        if (P_.seg[c] < 0 || P_.seg[b] < 0 || P_.seg[c] == P_.seg[b]) return;
        Plan p1, p2;
        tryInsert(c, P_.seg[b], bound, p1, b);
        if (p1.delta == INF) return;
        tryInsert(b, P_.seg[c], bound, p2, c);
        if (p2.delta == INF || p1.delta + p2.delta >= best.delta) return;
        best = p1;
        best.delta = p1.delta + p2.delta;
        best.swapWith = -1;
        best.partner = b;
        best.seg2 = p2.seg;
        best.site2 = p2.site;
        best.pushes2 = p2.pushes;
    }

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
                if (b >= 0 && swapPush_) trySwapPush(c, b, bound, best);
            }
        }, [&] { return reach; });
    }

    // skip: another cell to treat as removed from s (swap-with-push), or -1.
    void tryInsert(int c, int s, double bound, Plan& best, int skip = -1) {
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

        // the cells of s without c (and skip), as a view: L(j)
        const std::vector<int>& all = rm_.cells(s);
        int i1 = P_.seg[c] == s ? rm_.indexOf(c) : -1;
        int i2 = skip >= 0 && P_.seg[skip] == s ? rm_.indexOf(skip) : -1;
        if (i1 > i2) std::swap(i1, i2);   // i1 < i2, -1 = none
        int m = (int)all.size() - (i1 >= 0) - (i2 >= 0);
        auto L = [&](int j) {
            if (i1 >= 0 && j >= i1) ++j;
            if (i2 >= 0 && j >= i2) ++j;
            return all[j];
        };
        // insertion index by center
        double cc = uc + wc / 2.0;
        int k0 = (int)(std::lower_bound(all.begin(), all.end(), cc, [&](int v, double x) {
                           return P_.site[v] + db_.sitesOf(v, sg) / 2.0 < x;
                       }) - all.begin());
        k0 -= (i1 >= 0 && i1 < k0) + (i2 >= 0 && i2 < k0);
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
                    best.partner = -1;
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
            best.partner = -1;
            best.swapSeg = sc;
            best.swapSite = xb;
            best.pushes.clear();
        }
    }

    void apply(int c, const Plan& p) {
        if (p.partner >= 0) {
            int b = p.partner;
            rm_.remove(c);
            rm_.remove(b);
            for (auto [v, x] : p.pushes) rm_.slide(v, x);
            rm_.insert(c, p.seg, p.site);
            for (auto [v, x] : p.pushes2) rm_.slide(v, x);
            rm_.insert(b, p.seg2, p.site2);
            for (auto [v, x] : p.pushes) d_[v] = displacementOf(db_, P_, v);
            for (auto [v, x] : p.pushes2) d_[v] = displacementOf(db_, P_, v);
            d_[b] = displacementOf(db_, P_, b);
        } else if (p.swapWith >= 0) {
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

namespace {

// One Stage-1 pipeline: (spreading) -> Abacus -> refinement.
bool pipeline(const PlaceDB& db, Placement& P, const LegalizeOptions& opt, bool spread, std::string& err) {
    P.seg.assign(db.nodes.size(), -1);
    P.site.assign(db.nodes.size(), 0);
    std::vector<double> tx(db.nodes.size()), ty(db.nodes.size());
    for (int v : db.movable) {
        tx[v] = db.nodes[v].gx;
        ty[v] = db.nodes[v].gy;
    }
    if (spread) Spreader(db, opt.binRows, opt.binCells, opt.spreadRounds).run(tx, ty, opt.verbose);
    bool ok = opt.method == "tetris" ? tetris(db, P, err) : Abacus(db, P, opt.totalCost, tx, ty).run(err);
    if (!ok) return false;
    if (opt.verbose) {
        DispStats d = displacement(db, P);
        std::fprintf(stderr, "[legal] %-7s total %.1f  max %.1f\n", opt.method.c_str(), d.total, d.max);
    }
    if (opt.refineMax || opt.refineTotal) {
        Refiner ref(db, P, opt.evict);
        ref.sweepRows_ = opt.sweepRows;
        ref.sweepSites_ = opt.sweepSites;
        ref.totalTol_ = opt.totalTol;
        ref.swapPush_ = opt.swapPush;
        if (opt.rowOpt) ref.optimizeRows(0, INF, opt.verbose);   // exact L1 per row
        // The total pass frees room that the max pass can use again, so alternate.
        double prevMax = INF;
        for (int round = 0; round < 4; ++round) {
            if (opt.refineMax) ref.reduceMax(opt.verbose);
            if (opt.refineMax && opt.matching) ref.reduceMaxByMatching(opt.verbose);
            if (opt.rowOpt) ref.optimizeRows(ref.maxDisp(), ref.maxDisp(), opt.verbose);
            if (opt.matching) ref.matchSweep(opt.verbose);
            if (opt.refineTotal) ref.reduceTotal(opt.verbose);
            double mx = displacement(db, P).max;
            if (!opt.refineMax || mx > prevMax - 1e-9) break;
            prevMax = mx;
        }
        // v3: finish with exact row re-placement and local moves until neither
        // helps (the local moves leave rows slightly off their L1 optimum).
        for (int k = 0; opt.rowOpt && opt.refineTotal && k < opt.finishRounds; ++k) {
            double before = displacement(db, P).total;
            ref.optimizeRows(ref.maxDisp(), ref.maxDisp(), opt.verbose);
            if (opt.matching) ref.matchSweep(opt.verbose);
            ref.reduceTotal(opt.verbose);
            if (displacement(db, P).total > before * (1 - 1e-4)) break;
        }
    }
    return true;
}

}  // namespace

bool legalize(const PlaceDB& db, Placement& P, const LegalizeOptions& opt, std::string& err) {
    bool canSpread = opt.method != "tetris" && db.uniformSites && opt.spread != "off";
    if (!canSpread || opt.spread == "on") return pipeline(db, P, opt, canSpread, err);
    if (db.movable.size() > 60000) return pipeline(db, P, opt, false, err);   // auto on huge designs: one run only
    // auto: run both pipelines and keep the better one, total and maximum
    // displacement weighted equally (each relative to the run without spreading).
    if (!pipeline(db, P, opt, false, err)) return false;
    Placement Q;
    std::string err2;
    if (!pipeline(db, Q, opt, true, err2)) return true;
    DispStats a = displacement(db, P), b = displacement(db, Q);
    double score = b.total / std::max(1e-9, a.total) + b.max / std::max(1e-9, a.max);
    if (opt.verbose)
        std::fprintf(stderr, "[legal] plain: total %.1f max %.1f | spread: total %.1f max %.1f -> %s\n", a.total, a.max,
                     b.total, b.max, score < 2 ? "spread" : "plain");
    if (score < 2) P = Q;
    return true;
}
