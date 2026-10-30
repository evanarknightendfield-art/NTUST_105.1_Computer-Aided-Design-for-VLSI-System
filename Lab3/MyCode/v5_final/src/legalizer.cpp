// legalizer.cpp — Stage 1.
//
//   0. Flow-based spreading [Brenner DATE'12, Karimpour Darav ISPD'17]:
//      overflowed bins pass cells to neighbours along BFS paths, giving
//      Abacus spread-out targets. Run with and without it, keep the better.
//   1. Abacus [Spindler et al., ISPD'08]: cells are taken in x order; each one is
//      tried in nearby rows, where it is appended to the row and overlapping
//      "clusters" are merged and moved to their best (least-squares) position.
//      The row with the smallest cost wins.
//   2. Exact per-row re-placement with row and order fixed (convex cost,
//      pool-adjacent-violators) [RippleLG DAC'18, "fixed row & fixed order"].
//   3. Max-displacement refinement: the cell with the largest displacement is
//      moved into free space or swapped with another cell, as long as nobody
//      ends up as far away. Repeats until the worst cell cannot be helped.
// Then same-width min-cost matching (Hungarian) around the worst
//      cells [RippleLG], alternating with the moves above.
//   4. Total-displacement refinement: matching over windows tiling the
//      chip, then the same moves, accepted when they cut the total without
//      exceeding the maximum.
//   5. Final rounds: exact row re-placement + matching + local moves
//      again until the total stops improving (the local moves leave rows a
//      little off their L1 optimum). Optional: swap-with-push (--swappush).
//   6. In the max pass: window DP around the worst cell with a bisected
//      cap (reduceMaxWindows). In the final rounds: two-row
//      and three-row window DP swept to convergence (windowSweeps), run once,
//      on the better of the two pipelines. Every pass checks the Stage 1
//      time budget (opt.until), so a 200k-cell design ends in minutes.
#include "legalizer.hpp"

#include <algorithm>
#include <cassert>
#include <queue>
#include <cmath>
#include <cstdio>
#include <array>
#include <atomic>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <functional>
#include <stdexcept>

#include "rowmap.hpp"
#include "window.hpp"
#include "pool.hpp"
#include "diagnostics.hpp"

namespace {

constexpr double INF = std::numeric_limits<double>::infinity();

// The candidate scan already knows this position. At a shared site indexOf
// returns the first cell, so use the original lookup for later zero-width cells.
inline int evictionRoomWidth(const PlaceDB& db, const Placement& p, const RowMap& rm, int s,
                             std::vector<int>::const_iterator it, bool allowDirect, bool& direct) {
    const auto& cells = rm.cells(s);
    direct = allowDirect && (it == cells.begin() || p.site[*(it - 1)] != p.site[*it]);
    if (!direct) return rm.freeAround(*it).width();
    const int lo = it == cells.begin() ? 0 : p.site[*(it - 1)] + db.sitesOf(*(it - 1), db.segs[s]);
    const int hi = it + 1 == cells.end() ? db.segs[s].numSites : p.site[*(it + 1)];
    return hi - lo;
}

std::vector<int> xOrder(const PlaceDB& db) {
    std::vector<int> order = db.movable;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const Node &na = db.nodes[a], &nb = db.nodes[b];
        return na.gx != nb.gx ? na.gx < nb.gx : a < b;
    });
    return order;
}

// Visits rows in order of vertical distance from y; stops when the vertical
// distance alone is no better than best.
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
// cells go to targets (tx, ty) — the global placement, or the spread
// positions from Spreader — instead of always the global placement.
// the same algorithm, scored faster (LegalizeOptions::abacusFast):
//   1 = the exact per-cell loop reads each committed cell's target and width from
//       arrays kept per segment (same operations in the same order as mode 0, so
//       the same doubles), instead of tx_[cell] / the node table (random access);
//   2 = 1 + a lower-bound filter: a trial whose cost provably cannot beat the best
//       row found so far is rejected from block sums (sorted keys, 32 cells per
//       block) without the per-cell loop. Winners and near-ties still run the exact
//       loop, so the chosen rows, tie-breaking and work_ (the exact/bounded switch)
//       are unchanged; tests/abacus_equiv_test.cpp and --abacus-check verify it.
class Abacus {
public:
    Abacus(const PlaceDB& db, Placement& P, bool totalCost, const std::vector<double>& tx,
           const std::vector<double>& ty, bool verbose = false, int fast = 2, double exactWork = 5e9)
        : db_(db), P_(P), totalCost_(totalCost), tx_(tx), ty_(ty), st_(db.segs.size()), verbose_(verbose),
          fast_(fast), exactWork_(exactWork) {}

    bool run(std::string& err) {
        const double t0 = wallSeconds();
        std::vector<int> order = db_.movable;
        std::sort(order.begin(), order.end(), [&](int a, int b) { return tx_[a] != tx_[b] ? tx_[a] < tx_[b] : a < b; });
        for (int v : order) {
            const Node& n = db_.nodes[v];
            double best = INF;
            int bestSeg = -1;
            forRowsNear(db_, ty_[v], [&](int r, double dy) {
                for (int s : db_.rows[r].segs) {
                    if (dy + dxBound(db_.segs[s], db_.sitesOf(v, db_.segs[s]), tx_[v]) >= best) continue;
                    if (fast_ >= 2 && usedBound(v, s, dy) >= best) { ++pruned_; continue; }
                    double c = trial(v, s, dy, best);
                    if (c < best) { best = c; bestSeg = s; }
                }
            }, [&] { return best; });
            if (bestSeg < 0) { err = "no room for " + n.name; return false; }
            commit(v, bestSeg);
        }
        if (verbose_)
            std::fprintf(stderr, "[legal] abacus: %.3g exact cost steps%s, mode %d: %ld filtered / %ld exact trials "
                         "(%.3g cells looped) / %ld bounded, %ld cluster merges, %ld rows pruned  %.2fs\n",
                         work_, work_ >= exactWork_ ? " (budget used up, big merges bounded)" : "", fast_, filtered_,
                         exactTrials_, exactVisits_, boundTrials_, mergeIters_, pruned_, wallSeconds() - t0);
        // positions are written once at the end (commit used to rewrite the
        // whole last cluster every time: quadratic in big hotspot clusters)
        for (size_t s = 0; s < st_.size(); ++s)
            for (const Cluster& c : st_[s].cl) {
                int pos = c.x;
                for (int i = c.first; i < c.first + c.n; ++i) {
                    int v = st_[s].cells[i];
                    P_.seg[v] = (int)s;
                    P_.site[v] = pos;
                    pos += db_.sitesOf(v, db_.segs[s]);
                }
            }
        return true;
    }
    double work() const { return work_; }

private:
    // first = index into SegState::cells, n = number of cells
    // cost = sum of |x - u| over its cells at its current x (mode 2 only, filter estimate)
    struct Cluster { int first; double e, q; int w, x; int n; double cost = 0; };
    // the exact per-cell total cost walks every cell of the merged
    // clusters, quadratic in big hotspot clusters (1M-cell test design: 900 s).
    // It stays exact until kExactWork cell visits (a count, not a clock, so
    // the result does not depend on the machine), then merges over 64 cells
    // use a bound. The 200k-cell design stays exact (the bound cost it 3 % in
    // max displacement); only the 1M-cell one switches.
    static constexpr double kExactWork = 5e9;   // 200k-cell design: 3.0e9 (exact); ibm01: 3.4e6
    mutable double work_ = 0;
    int exactCells() const { return work_ < exactWork_ ? 1 << 30 : 64; }
    // (mode >= 1): u[i], w[i] = target site and width of cells[i]; pre[i] = sum of w before i.
    // Mode 2: blocks of kBlock committed cells hold key = u - pre sorted, with prefix sums:
    // a cluster's cells sit at x + pre[i] - pre[first], so their cost at x is sum |base - key|
    // with base = x - pre[first], one binary search per block.
    static constexpr int kBlock = 32;
    struct Block { std::array<double, kBlock> key; std::array<double, kBlock + 1> sum; };
    struct SegState {
        std::vector<int> cells; std::vector<Cluster> cl; int used = 0;
        std::vector<double> u, key; std::vector<int> w; std::vector<long> pre; std::vector<Block> blocks;
    };

    double desired(int v, const Segment& s) const { return (tx_[v] - s.x0) / s.sp; }

    // (mode 2): v appended to s ends at a site >= S.used (every committed cell of s lies left of
    // it, packed from site 0), so its own cost is >= dy + sp * (used - u). That bounds the whole trial
    // when the merged cells' change cannot be negative: no clusters, own-cost mode, or the bounded
    // regime with a last cluster over 64 cells (any merge then takes it, and the bound adds >= 0).
    double usedBound(int v, int s, double dy) const {
        const SegState& S = st_[s];
        if (totalCost_ && !S.cl.empty() && !(work_ >= exactWork_ && S.cl.back().n > 64)) return -INF;
        const Segment& sg = db_.segs[s];
        const double u = desired(v, sg);
        double lower = dy + std::max(0.0, double(S.used) - u) * sg.sp;
        if (fast_ >= 3 && totalCost_ && !S.cl.empty() && work_ >= exactWork_ && S.cl.back().n > 64) {
            const Cluster& last = S.cl.back();
            const int w = db_.sitesOf(v, sg);
            if (S.used + w > sg.numSites) return INF;
            if (last.x + last.w > clampSite(u, w, sg.numSites)) {
                // B2: the last cluster must be merged and therefore the bounded scorer is used.
                // Let a be its eventual start, after ALL earlier merges. The new cell starts at
                // a + last.w, so this cluster and the new cell alone cost at least
                // |a + last.w - u| + last.n * |a - last.x|. All omitted bounded terms are >= 0.
                // Packing implies used-last.w <= a <= numSites-last.w-w. Since last.n > 1,
                // this convex expression is minimized at clamp(last.x, lo, hi). This does NOT
                // assume the first tentative merge's shift survives later cluster merges.
                const double a = std::clamp(double(last.x), double(S.used - last.w),
                                            double(sg.numSites - last.w - w));
                const double own = std::fabs(a + last.w - u), shift = std::fabs(a - last.x) * last.n;
                const double bound = dy + (own + shift) * sg.sp;
                const double margin = 1e-9 * (1.0 + std::fabs(dy) + (own + shift + std::fabs(u)) * sg.sp);
                lower = std::max(lower, bound - margin);
            }
        }
        return lower;
    }

    // sum over cells [a, b) of |base - key| (estimate; mode 2)
    double rangeCost(const SegState& S, int a, int b, double base) const {
        double c = 0;
        for (; a < b && a % kBlock; ++a) c += std::fabs(base - S.key[a]);
        while (a + kBlock <= b && a / kBlock < (int)S.blocks.size()) {
            const Block& k = S.blocks[a / kBlock];
            int lo = int(std::lower_bound(k.key.begin(), k.key.end(), base) - k.key.begin());
            c += base * lo - k.sum[lo] + (k.sum[kBlock] - k.sum[lo]) - base * (kBlock - lo);
            a += kBlock;
        }
        for (; a < b; ++a) c += std::fabs(base - S.key[a]);
        return c;
    }

    // Cost of appending v to segment s, without changing anything.
    // best = the best cost so far; a result >= best is only compared, never used.
    double trial(int v, int s, double dy, double best) const {
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
        mergeIters_ += (long)S.cl.size() - 1 - k;
        double own = std::fabs((x + W - w) - desired(v, sg)) * sg.sp + dy;
        if (!totalCost_ || k + 1 == (int)S.cl.size()) return own;
        // Cells of the merged clusters move too: add their change in displacement.
        const int first = S.cl[k + 1].first;
        int cells = (int)S.cells.size() - first;
        double delta = 0;
        int pos = x;
        if (cells <= exactCells()) {
            work_ += cells;
            if (fast_ >= 2 && cells >= 2 * kBlock && best < INF) {
                double oldCost = 0;
                for (size_t j = k + 1; j < S.cl.size(); ++j) oldCost += S.cl[j].cost;
                const double newCost = rangeCost(S, first, (int)S.cells.size(), double(x) - double(S.pre[first]));
                const double est = own + (newCost - oldCost) * sg.sp;
                // rounding of both sums (and of the exact loop) is far below this margin
                const double margin = (1e-8 * (newCost + oldCost) + 1e-6) * sg.sp + 1e-12 * std::fabs(best);
                if (est - margin >= best) { ++filtered_; return INF; }
            }
            ++exactTrials_;
            exactVisits_ += cells;
            if (fast_ >= 1) {
                for (size_t j = k + 1; j < S.cl.size(); ++j) {
                    int old = S.cl[j].x;
                    const int i1 = S.cl[j].first + S.cl[j].n;
                    for (int i = S.cl[j].first; i < i1; ++i) {
                        const double u = S.u[i];
                        const int wc = S.w[i];
                        delta += std::fabs(pos - u) - std::fabs(old - u);
                        pos += wc;
                        old += wc;
                    }
                }
            } else {
                for (size_t j = k + 1; j < S.cl.size(); ++j) {
                    int old = S.cl[j].x;   // old positions from the clusters (not written yet)
                    for (int i = S.cl[j].first; i < S.cl[j].first + S.cl[j].n; ++i) {
                        int c = S.cells[i], wc = db_.sitesOf(c, sg);
                        double u = desired(c, sg);
                        delta += std::fabs(pos - u) - std::fabs(old - u);
                        pos += wc;
                        old += wc;
                    }
                }
            }
        } else {
            // a big merge (hotspot): bound each cluster's change by |shift| x cells
            ++boundTrials_;
            for (size_t j = k + 1; j < S.cl.size(); ++j) {
                delta += std::fabs(pos - S.cl[j].x) * S.cl[j].n;
                pos += S.cl[j].w;
            }
        }
        return own + delta * sg.sp;
    }

    void commit(int v, int s) {
        const Segment& sg = db_.segs[s];
        SegState& S = st_[s];
        int w = db_.sitesOf(v, sg);
        double u = desired(v, sg);
        if (fast_ >= 1) {
            const long pre = S.cells.empty() ? 0 : S.pre.back() + S.w.back();
            S.u.push_back(u);
            S.w.push_back(w);
            S.pre.push_back(pre);
            if (fast_ >= 2) {
                S.key.push_back(u - double(pre));
                if (S.key.size() % kBlock == 0) {
                    Block b;
                    std::copy(S.key.end() - kBlock, S.key.end(), b.key.begin());
                    std::sort(b.key.begin(), b.key.end());
                    b.sum[0] = 0;
                    for (int i = 0; i < kBlock; ++i) b.sum[i + 1] = b.sum[i] + b.key[i];
                    S.blocks.push_back(b);
                }
            }
        }
        S.cells.push_back(v);
        S.used += w;
        S.cl.push_back({(int)S.cells.size() - 1, 1.0, u, w, clampSite(u, w, sg.numSites), 1});
        while (S.cl.size() >= 2) {
            Cluster& prev = S.cl[S.cl.size() - 2];
            const Cluster& last = S.cl.back();
            if (prev.x + prev.w <= last.x) break;
            prev.q += last.q - last.e * prev.w;
            prev.e += last.e;
            prev.w += last.w;
            prev.n += last.n;
            prev.x = clampSite(prev.q / prev.e, prev.w, sg.numSites);
            S.cl.pop_back();
        }
        if (fast_ >= 2 && totalCost_) {
            Cluster& c = S.cl.back();
            c.cost = rangeCost(S, c.first, c.first + c.n, double(c.x) - double(S.pre[c.first]));
        }
    }

    const PlaceDB& db_;
    Placement& P_;
    bool totalCost_;
    const std::vector<double>& tx_;
    const std::vector<double>& ty_;
    std::vector<SegState> st_;
    bool verbose_;
    int fast_;
    double exactWork_;
    mutable long filtered_ = 0, exactTrials_ = 0, boundTrials_ = 0, mergeIters_ = 0, pruned_ = 0;
    mutable double exactVisits_ = 0;
};

// ---------------------------------------------------------------- flow-based spreading
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
    Refiner(const PlaceDB& db, Placement& P, bool evict, double until)
        : db_(db), P_(P), rm_(db, P), d_(db.nodes.size(), 0), evict_(evict), until_(until) {
        for (int v : db.movable) d_[v] = displacementOf(db, P, v);
        tried_.assign(db.nodes.size(), 0);
    }

    // P was changed from outside (window DP): rebuild the lists and distances.
    void resync() {
        rm_.build();
        for (int v : db_.movable) d_[v] = displacementOf(db_, P_, v);
    }
    bool timeUp() const { return wallSeconds() > until_; }
    void setUntil(double until) { until_ = until; }
    double until() const { return until_; }

    // Repeatedly moves the worst cell so that it and every cell it pushes end
    // up strictly below the current maximum. Stops when the worst cell is stuck.
    // rateStop > 0 = also stop when, over the last window (>= 0.25 s and >= a quarter of the time
    // spent so far), the maximum fell by less than rateStop of itself per second (the greedy slows down
    // once the tail of far cells is thick; the time then buys more elsewhere). Returns why it stopped.
    const char* reduceMax(bool verbose, double rateStop = 0, double floor = 0) {
        assert(maxReleaseCount_ == 0);   // closeMaxPhase is terminal for this Refiner's max work
        inMax_ = true;
        rowCache_.resize(db_.segs.size());
        // one spinning team for this pass (released when the pass returns)
        struct TeamScope {
            std::unique_ptr<SpinTeam>& team;
            ~TeamScope() { team.reset(); }
        } teamScope{team_};
        if (maxFast_ >= 4 && maxThreads_ > 1) team_ = std::make_unique<SpinTeam>(maxThreads_);
        if (maxFast_ >= 4) {   // before any worker builds a row cache
            rowArr_.resize(db_.segs.size());
            patched_.assign(db_.segs.size(), 0);
            if (gxy_.empty()) {
                gxy_.resize(db_.nodes.size());
                for (size_t v = 0; v < db_.nodes.size(); ++v) gxy_[v] = {db_.nodes[v].gx, db_.nodes[v].gy};
            }
        }
        cacheValid_.assign(db_.segs.size(), 0);
        profileMax_ = verbose;
        maxProfile_ = MaxProfile();
        int moves = 0, inserts = 0, evicts = 0;
        double evictTime = 0;
        const double t0 = wallSeconds();
        const char* why = "stuck";
        // the worst cell comes from a max-heap (stale entries are skipped)
        // instead of a scan over all cells per move: 200k cells x 23k moves
        // took most of the Stage 1 budget on the large test design.
        // Key (d, -v): among equal displacements the smallest id first, the
        // same cell the old scan picked (the max pass is path dependent).
        std::priority_queue<std::pair<double, int>> heap;
        double phaseT = profileMax_ ? wallSeconds() : 0;
        for (int v : db_.movable) heap.push({d_[v], -v});
        if (profileMax_) maxProfile_.heapSeconds += wallSeconds() - phaseT;
        double markT = t0, markD = -1, D0 = -1;
        double lastMoveSeconds = 0, moveSeconds = 0;
        for (;;) {
            if (completeMax_) {
                const double elapsed = wallSeconds() - t0;
                if (timeUp()) { why = "deadline"; break; }
                if (moves > 0 && elapsed >= 0.25 && moveSeconds / moves > 0.010) { why = "cost"; break; }
                if (moves >= 8 && elapsed >= 0.25 && until_ - wallSeconds() <
                    1.5 * std::max(lastMoveSeconds, moveSeconds / moves)) { why = "forecast"; break; }
            }
            if ((moves & 63) == 0 && timeUp()) { why = "deadline"; break; }
            if (moveLimit_ > 0 && moves >= moveLimit_) { why = "move-limit"; break; }   // --max-check-moves only
            const double moveStart = completeMax_ ? wallSeconds() : 0;
            phaseT = profileMax_ ? wallSeconds() : 0;
            while (!heap.empty() && heap.top().first != d_[-heap.top().second]) heap.pop();
            if (profileMax_) maxProfile_.heapSeconds += wallSeconds() - phaseT;
            if (heap.empty()) break;
            int c = -heap.top().second;
            double D = d_[c];
            if (D0 < 0) D0 = markD = D;
            if (floor > 0 && D <= floor * D0) { why = "floor"; break; }
            if (rateStop > 0) {   // every move (one move on L2 takes ~40 ms)
                const double now = wallSeconds();
                if (now - markT >= std::max(policyMinSeconds_, 0.25 * (now - t0))) {
                    // tail of the distribution: cells within 5 % / 10 % of the maximum
                    long t5 = 0, t10 = 0;
                    for (int v : db_.movable) { t5 += d_[v] > 0.95 * D; t10 += d_[v] > 0.9 * D; }
                    if (verbose)
                        std::fprintf(stderr, "[legal] max-refine check %d moves  max %.1f  tail5 %ld  tail10 %ld  %.2fs\n", moves, D, t5,
                                     t10, now);
                    if (tailStop_ > 0 && t10 > tailStop_ * db_.movable.size()) { why = "tail"; break; }
                    if ((markD - D) / std::max(1e-9, markD) < rateStop * (now - markT)) { why = "rate"; break; }
                    markT = now;
                    markD = D;
                }
            }
            Plan best;
            touched_.clear();
            phaseT = profileMax_ ? wallSeconds() : 0;
            searchFor(c, D - minStep(), D, best, pruneMax_);
            if (profileMax_) maxProfile_.searchSeconds += wallSeconds() - phaseT;
            if (best.delta < INF) {
                phaseT = profileMax_ ? wallSeconds() : 0;
                apply(c, best); ++inserts;
                if (profileMax_) maxProfile_.applySeconds += wallSeconds() - phaseT;
            }
            else {
                const double te = wallSeconds();
                const bool ok = evict_ && (evict(c, D - minStep(), true) || evict(c, D - minStep(), false));
                evictTime += wallSeconds() - te;
                if (!ok) break;
                ++evicts;
            }
            phaseT = profileMax_ ? wallSeconds() : 0;
            for (int v : touched_) heap.push({d_[v], -v});
            if (profileMax_) maxProfile_.heapSeconds += wallSeconds() - phaseT;
            ++moves;
            if (completeMax_) {
                lastMoveSeconds = wallSeconds() - moveStart;
                moveSeconds += lastMoveSeconds;
            }
            if (moveObserver_) moveObserver_(c, P_, touched_);
            if (verbose && traceMax_ && (moves % 2000) == 0)
                std::fprintf(stderr, "[legal] max-refine progress %d moves  max %.1f  %.2fs\n", moves, D, wallSeconds());
        }
        if (verbose) {
            DispStats s = displacement(db_, P_);
            std::fprintf(stderr, "[legal] max-refine %d moves (%d insert/swap, %d evict %.2fs)  total %.1f  max %.1f -> %.1f  "
                         "%.2fs in %.2fs (%s)\n", moves, inserts, evicts, evictTime, s.total, D0, s.max, wallSeconds() - t0,
                         wallSeconds(), why);
            std::fprintf(stderr, "[max-prof] rows %llu inserts %llu candidates %llu push-visits %llu "
                         "push-lengths[0,1,2-4,5-16,17-64,65+] %llu,%llu,%llu,%llu,%llu,%llu "
                         "search %.6fs apply %.6fs heap %.6fs evict %.6fs insert-sampled %.6fs/%llu "
                         "evict-calls %llu evict-choices %llu push-scratch-stores %llu push-replay-visits %llu walks %llu proven-infeasible %llu cache %.6fs/%llu replay %.6fs scan %.6fs work %.6fs critical %.6fs\n",
                         maxProfile_.rows, maxProfile_.inserts, maxProfile_.candidates, maxProfile_.pushVisits,
                         maxProfile_.pushLengths[0], maxProfile_.pushLengths[1], maxProfile_.pushLengths[2],
                         maxProfile_.pushLengths[3], maxProfile_.pushLengths[4], maxProfile_.pushLengths[5],
                         maxProfile_.searchSeconds, maxProfile_.applySeconds, maxProfile_.heapSeconds, evictTime,
                         maxProfile_.insertSampleSeconds, maxProfile_.insertSamples,
                         maxProfile_.evictCalls, maxProfile_.evictChoices, maxProfile_.pushStores, maxProfile_.pushReplayVisits,
                         maxProfile_.walks, maxProfile_.proven, maxProfile_.cacheSeconds, maxProfile_.cacheBuilds, maxProfile_.replaySeconds,
                         maxProfile_.scanSeconds, maxProfile_.work, maxProfile_.critical);
            std::fprintf(stderr, "[evict-prof] calls %llu choices %llu candidates %llu insert-c-ok %llu search-b-ok %llu rollbacks %llu "
                         "gather %.6fs select %.6fs lift %.6fs insert-c %.6fs prepare-apply-c %.6fs search-b %.6fs commit-b %.6fs rollback %.6fs inclusive %.6fs direct-rooms %llu room-fallbacks %llu\n",
                         maxProfile_.evictCalls, maxProfile_.evictChoices, maxProfile_.evictCandidates,
                         maxProfile_.evictInsertOK, maxProfile_.evictSearchOK, maxProfile_.evictRollbacks,
                         maxProfile_.evictGatherSeconds, maxProfile_.evictSelectSeconds, maxProfile_.evictLiftSeconds,
                         maxProfile_.evictInsertSeconds, maxProfile_.evictPrepareSeconds, maxProfile_.evictSearchSeconds,
                         maxProfile_.evictCommitSeconds, maxProfile_.evictRollbackSeconds, evictTime,
                         maxProfile_.evictDirectRooms, maxProfile_.evictRoomFallbacks);
            std::fprintf(stderr, "[max-a6-prof] endpoint-binary %llu endpoint-linear-steps %llu endpoint-fallbacks %llu row-partition %.6fs\n",
                         maxProfile_.endpointBinaries, maxProfile_.endpointSteps, maxProfile_.endpointFallbacks,
                         maxProfile_.rowPartitionSeconds);
        }
        profileMax_ = false;
        inMax_ = false;
        lastMoves_ = moves;
        return why;
    }

    // Greedy passes over cells (worst first): any insert/swap that lowers the
    // total without pushing anybody above the current maximum is applied.
    void reduceTotal(bool verbose, double deadline = 1e30) {
        double saved = until_;
        until_ = std::min(until_, deadline);   // this pass may use only part of the budget
        reduceTotalPasses(verbose);
        until_ = saved;
    }
    void reduceTotalPasses(bool verbose) {
        double bound = d_[worst()];
        for (int pass = 0; pass < 20 && !timeUp(); ++pass) {   // no pass starts after the deadline
            std::vector<int> order = db_.movable;
            std::sort(order.begin(), order.end(), [&](int a, int b) { return d_[a] > d_[b]; });
            double gain = 0;
            int moves = 0, seen = 0;
            for (int c : order) {
                if (d_[c] <= 0) break;
                // every 16 cells: the worst cells come first and have the largest
                // searches; on 1M cells 256 of them ran 10 s past the deadline
                if ((++seen & 15) == 0 && timeUp()) break;
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
                std::fprintf(stderr, "[legal] total-refine pass %d: %d moves  total %.1f  max %.1f  %.2fs\n", pass, moves,
                             s.total, s.max, wallSeconds());
            }
            if (gain < totalTol_ * displacement(db_, P_).total || timeUp()) break;
        }
    }

    double maxDisp() const { return d_[worst()]; }

    // ---- exact re-placement of each row with its cell order fixed.
    // Minimises sum phi(d_i) per segment, where d_i = |x_i - u_i| + dy_i and
    // phi is convex (see phi). With row and order fixed this is a chain-
    // constrained convex problem: pool-adjacent-violators gives the exact
    // optimum ("fixed row & fixed order" step of RippleLG, solved by clumping
    // instead of a dual min-cost flow). Cells may not go beyond `bound`.
    void optimizeRows(double delta0, double bound, bool verbose) {
        int changed = 0;
        // segments are independent; stop between them at the deadline (6.6 s on 1M cells)
        for (size_t s = 0; s < db_.segs.size() && !timeUp(); ++s) changed += optimizeSegment((int)s, delta0, bound);
        for (int v : db_.movable) d_[v] = displacementOf(db_, P_, v);
        if (verbose) {
            DispStats st = displacement(db_, P_);
            std::fprintf(stderr, "[legal] row-opt (d0 %.0f): %d segments  total %.1f  max %.1f  %.2fs\n", delta0, changed,
                         st.total, st.max, wallSeconds());
        }
    }

    // ---- same-width matching [RippleLG, DAC'18].
    // Cells of equal width can trade slots without any overlap. Around each
    // cell displaced more than delta0, every width class in a window is
    // re-assigned to its own slots by a min-cost assignment (Hungarian) with
    // cost phi(d): linear below delta0, steep above it. One assignment can move
    // a far cell inward and push a chain of nearby cells outward a little each,
    // which the one- and two-cell moves above cannot do.
    int matchWindows(double delta0, int maxWindows) {
        matchingForecast_ = false;
        if (!db_.uniformSites) return 0;
        double cap = maxDisp();
        int windows = 0;
        double windowSeconds = 0, lastWindowSeconds = 0;
        for (int iter = 0; iter < maxWindows && work_ < kMatchWork && !timeUp(); ++iter) {
            // Admission predicts one next window from observed windows, not completion of
            // the round. Its cost includes finding the worst cell and the matching itself.
            if (completeMax_ && windows > 0 && until_ - wallSeconds() <
                1.5 * std::max(lastWindowSeconds, windowSeconds / windows)) {
                matchingForecast_ = true;
                break;
            }
            const double windowStart = completeMax_ ? wallSeconds() : 0;
            const double ts = matchProf_ ? wallSeconds() : 0;
            int c = -1;
            for (int v : db_.movable)
                if (d_[v] > delta0 && !tried_[v] && (c < 0 || d_[v] > d_[c])) c = v;
            if (matchProf_) matchScan_ += wallSeconds() - ts;
            if (c < 0) break;
            tried_[c] = 1;
            matchAround(c, delta0, cap);
            ++windows;
            if (completeMax_) {
                lastWindowSeconds = wallSeconds() - windowStart;
                windowSeconds += lastWindowSeconds;
            }
        }
        std::fill(tried_.begin(), tried_.end(), 0);
        return windows;
    }

    // Lowers the maximum: matching moves most of the cells above a slightly
    // lower threshold, then insert/swap/evict moves the rest, and repeat.
    // The returned reason is diagnostic; only completeMax uses measured admission.
    const char* reduceMaxByMatching(bool verbose) {
        const double t0 = verbose ? wallSeconds() : 0;
        matchProf_ = verbose;
        matchHung_ = matchG3_ = matchScan_ = matchPar_ = 0;
        matchParallel_ = maxFast_ >= 4;
        matchClasses_ = 0;
        matchMaxG_ = 0;
        double windowSeconds = 0, greedySeconds = 0;
        int windows = 0, rounds = 0;
        const char* why = "done";
        for (int round = 0; round < 12 && !timeUp(); ++round) {
            double D = maxDisp(), d0 = 0.95 * D;
            int above = 0;
            for (int v : db_.movable) above += d_[v] > d0;
            const double windowStart = verbose ? wallSeconds() : 0;
            int w = matchWindows(d0, 400);
            if (verbose) windowSeconds += wallSeconds() - windowStart;
            windows += w;
            ++rounds;
            int after = 0;
            for (int v : db_.movable) after += d_[v] > d0;
            if (completeMax_ && (matchingForecast_ || timeUp())) {
                why = matchingForecast_ ? "forecast" : "deadline";
            } else {
                const double greedyStart = verbose ? wallSeconds() : 0;
                const char* greedyWhy = reduceMax(false);
                if (verbose) greedySeconds += wallSeconds() - greedyStart;
                if (completeMax_ && std::string(greedyWhy) != "stuck") why = greedyWhy;
            }
            if (verbose) {
                DispStats s = displacement(db_, P_);
                std::fprintf(stderr, "[legal] matching d0 %.0f: %d windows, cells above d0 %d -> %d  total %.1f  max %.1f  %.2fs\n",
                             d0, w, above, after, s.total, s.max, wallSeconds());
            }
            if (completeMax_ && std::string(why) != "done") break;
            if (maxDisp() > D - minStep()) break;
        }
        if (timeUp()) why = "deadline";
        else if (std::string(why) == "done") {
            if (work_ >= kMatchWork) why = "work-limit";
            else if (rounds == 12) why = "round-limit";
            else why = "stuck";
        }
        if (verbose)
            std::fprintf(stderr, "[matching-prof] windows %d rounds %d window-search %.6fs greedy %.6fs total %.6fs stop %s "
                         "worst-scan %.6fs hungarian %.6fs (worker) parallel %.6fs classes %ld g3 %.4e max-g %d\n",
                         windows, rounds, windowSeconds, greedySeconds, wallSeconds() - t0, why, matchScan_, matchHung_, matchPar_,
                         matchClasses_, matchG3_, matchMaxG_);
        matchProf_ = false;
        matchParallel_ = false;
        return why;
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
            for (double y = y0 - off * wy; y < y1 && !timeUp(); y += wy)
                for (double x = x0 - off * wx; x < x1 && !timeUp(); x += wx)
                    matchBox(x, x + wx - 1e-9, y, y + wy - 1e-9, cap, cap, false, x + wx / 2, y + wy / 2);
        }
        if (verbose) {
            DispStats s = displacement(db_, P_);
            std::fprintf(stderr, "[legal] matching sweep: total %.1f  max %.1f  %.2fs\n", s.total, s.max, wallSeconds());
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
        // the width classes of one box hold disjoint cells and slots, so their assignments do
        // not depend on each other. maxFast 4 collects them, solves the Hungarian problems in parallel
        // and applies them in class order: the same placement as one class after the other.
        struct Job { std::vector<int> grp, slotSeg, slotSite, to; };
        std::vector<Job> jobs;
        const bool parallel = matchParallel_ && maxThreads_ > 1;
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
            Job job;
            job.slotSeg.resize(g);
            job.slotSite.resize(g);
            for (int k = 0; k < g; ++k) {
                job.slotSeg[k] = P_.seg[grp[k]];
                job.slotSite[k] = P_.site[grp[k]];
            }
            job.grp = std::move(grp);
            if (onlyFar) work_ += (double)g * g * g;
            jobs.push_back(std::move(job));
            if (!parallel) {
                solveMatch(jobs.back(), delta0, cap);
                changed = applyMatch(jobs.back()) || changed;
                jobs.pop_back();
            }
        }
        if (parallel && !jobs.empty()) {
            if (!searchPool_) {
                searchPool_ = std::make_unique<Pool>(maxThreads_);
                searchWork_.resize(maxThreads_);
            }
            std::vector<int> order(jobs.size());   // largest first, taken by whichever worker is free
            for (size_t k = 0; k < order.size(); ++k) order[k] = int(k);
            std::stable_sort(order.begin(), order.end(), [&](int p, int q) { return jobs[p].grp.size() > jobs[q].grp.size(); });
            std::atomic<size_t> next{0};
            const double th = matchProf_ ? wallSeconds() : 0;
            searchPool_->run(std::min<int>(maxThreads_, (int)jobs.size()), [&](int) {
                for (size_t k; (k = next.fetch_add(1)) < order.size();) solveMatch(jobs[order[k]], delta0, cap);
            });
            if (matchProf_) matchPar_ += wallSeconds() - th;
            for (Job& job : jobs) changed = applyMatch(job) || changed;
        }
        if (changed) {
            std::vector<int> segs;
            for (auto& [wsites, v] : inWin) segs.push_back(P_.seg[v]);
            rm_.rebuild(segs);
            for (auto& [wsites, v] : inWin) d_[v] = displacementOf(db_, P_, v);
        }
    }

    template <class Job>
    void solveMatch(Job& job, double delta0, double cap) {
        const int g = (int)job.grp.size();
        std::vector<double> cost((size_t)g * g);
        for (int i = 0; i < g; ++i) {
            const Node& ni = db_.nodes[job.grp[i]];
            for (int k = 0; k < g; ++k) {
                const Segment& sg = db_.segs[job.slotSeg[k]];
                double d = std::fabs(sg.x0 + job.slotSite[k] * sg.sp - ni.gx) + std::fabs(sg.y - ni.gy);
                cost[(size_t)i * g + k] = (d > cap && k != i) ? 1e30 : phi(d, delta0);
            }
        }
        const double th = matchProf_ ? wallSeconds() : 0;
        job.to = hungarian(cost, g);
        if (matchProf_) {
            std::lock_guard<std::mutex> lock(matchProfMutex_);
            matchHung_ += wallSeconds() - th; ++matchClasses_; matchG3_ += double(g) * g * g; matchMaxG_ = std::max(matchMaxG_, g);
        }
    }
    template <class Job>
    bool applyMatch(const Job& job) {
        bool changed = false;
        for (size_t i = 0; i < job.grp.size(); ++i) {
            if (job.to[i] == int(i)) continue;
            changed = true;
            P_.seg[job.grp[i]] = job.slotSeg[job.to[i]];
            P_.site[job.grp[i]] = job.slotSite[job.to[i]];
        }
        return changed;
    }

    static constexpr size_t kMaxGroup = 300;

public:
    // knobs: matching-sweep window (rows x sites) and the total pass's stop threshold
    int sweepRows_ = 6, sweepSites_ = 120;
    bool swapPush_ = false;
    double totalTol_ = 5e-4;
    bool traceMax_ = false;   // measurement only (-v): progress of the max pass every 2000 moves
    bool pruneMax_ = false;
    double tailStop_ = 0;     // max pass stops when > tailStop_ x cells lie within 10 % of the maximum   // searchFor's row prune in the max pass (LegalizeOptions::maxPrune)
    int maxFast_ = 0, maxThreads_ = 1;
    double policyMinSeconds_ = 0.25;
    bool releaseMaxScratch_ = false;
    int maxReleaseCount_ = 0;
    // The caller knows when no later max pass in this Refiner can run. Keep
    // storage across matching's nested max passes, then release it exactly once.
    void closeMaxPhase(bool verbose) {
        if (releaseMaxScratch_ && maxReleaseCount_ == 0 && !maxScratchEmpty()) releaseMaxStorage(verbose);
    }
    bool maxScratchEmpty() const {
        if (rowArr_.capacity() || rowCache_.capacity() || gxy_.capacity() || rowCands_.capacity() ||
            cacheValid_.capacity() || patched_.capacity() || rowPlans_.capacity() || searchRows_.capacity() ||
            rowWeights_.capacity() || rowRanges_.capacity() || fastOwn_.pushes.capacity() || fastOwn_.pushes2.capacity()) return false;
        for (const auto& w : searchWork_)
            if (w.pushes.capacity() || w.open.capacity() || w.rows.capacity()) return false;
        return true;
    }
    bool completeMax_ = false;
    bool matchingForecast_ = false;   // complete-mode next-window admission only
    long moveLimit_ = 0;              // diagnostic (--max-check-moves)
    int lastMoves_ = 0;               // moves of the last reduceMax
    bool matchProf_ = false;          // -v profile of the max matching
    bool matchParallel_ = false;      // width classes of a box solved in parallel (maxFast >= 4)
    std::mutex matchProfMutex_;
    double matchHung_ = 0, matchG3_ = 0, matchScan_ = 0, matchPar_ = 0;
    long matchClasses_ = 0;
    int matchMaxG_ = 0;
    std::function<void(int, const Placement&, const std::vector<int>&)> moveObserver_;

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
        // swap-with-push: partner b goes to (seg2, site2) pushing pushes2
        int partner = -1, seg2 = -1, site2 = 0;
        std::vector<std::pair<int, int>> pushes2;
    };
    struct MaxProfile {
        unsigned long long rows = 0, inserts = 0, candidates = 0, pushVisits = 0, pushStores = 0, pushReplayVisits = 0;
        std::array<unsigned long long, 6> pushLengths{};
        double searchSeconds = 0, applySeconds = 0, heapSeconds = 0, insertSampleSeconds = 0;
        unsigned long long insertSamples = 0;
        unsigned long long evictCalls = 0, evictChoices = 0;
        // diagnostic: exclusive eviction substeps, main thread only.
        // searchB includes its worker/cache work; existing evictTime is inclusive.
        unsigned long long evictCandidates = 0, evictInsertOK = 0, evictSearchOK = 0, evictRollbacks = 0;
        unsigned long long evictDirectRooms = 0, evictRoomFallbacks = 0;
        double evictGatherSeconds = 0, evictSelectSeconds = 0, evictLiftSeconds = 0, evictInsertSeconds = 0;
        double evictPrepareSeconds = 0, evictSearchSeconds = 0, evictCommitSeconds = 0, evictRollbackSeconds = 0;
        unsigned long long walks = 0;   // insert candidates walked with the original arithmetic
        double cacheSeconds = 0, replaySeconds = 0;   // row-cache builds (worker s) / candidate replay
        unsigned long long cacheBuilds = 0, proven = 0;   // proven: walks skipped, a cell past the bound
        double stepSeconds = 0, scanSeconds = 0, critical = 0, work = 0;   // per-worker step / scan; per move max / sum
        unsigned long long endpointBinaries = 0, endpointSteps = 0, endpointFallbacks = 0;
        double rowPartitionSeconds = 0;
    };
    struct SearchWorkspace {
        std::vector<std::pair<int, int>> pushes;
        std::vector<std::pair<double, std::pair<int, int>>> open;   // (lower bound - margin, (row, index))
        std::vector<int> rows;                                      // rows this worker scanned
        MaxProfile profile;
        unsigned long long insertSampleCursor = 0;
    };
    bool profileMax_ = false, inMax_ = false;
    MaxProfile maxProfile_;
    std::unique_ptr<Pool> searchPool_;
    std::unique_ptr<SpinTeam> team_;   // alive only inside reduceMax
    std::vector<SearchWorkspace> searchWork_;
    std::vector<std::pair<int, double>> searchRows_;
    std::vector<unsigned long long> rowWeights_;   // mode 6: immutable during this search
    std::vector<std::pair<size_t, size_t>> rowRanges_;
    std::vector<Plan> rowPlans_;
    struct RowSearchCell { int site, width; double gx, dy, oldD; };
    std::vector<std::vector<RowSearchCell>> rowCache_;
    std::vector<char> cacheValid_;
    std::vector<char> patched_;   // row cache contents patched by apply, arrays not yet rebuilt
    bool cacheSuspended_ = false;

    void ensureRowCache(int s) {
        if (maxFast_ < 2 || !inMax_ || cacheSuspended_ || cacheValid_[s]) return;
        if (maxFast_ >= 4 && patched_[s]) {
            buildArrays(s);
            patched_[s] = 0;
            cacheValid_[s] = 1;
            return;
        }
        auto& cache = rowCache_[s];
        cache.clear();
        const Segment& sg = db_.segs[s];
        if (maxFast_ >= 4) {   // same values from contiguous arrays
            for (int v : rm_.cells(s)) cache.push_back({P_.site[v], db_.sitesOf(v, sg), gxy_[v].first,
                                                      std::fabs(sg.y - gxy_[v].second), d_[v]});
            buildArrays(s);
        } else {
            for (int v : rm_.cells(s)) cache.push_back({P_.site[v], db_.sitesOf(v, sg), db_.nodes[v].gx,
                                                      std::fabs(sg.y - db_.nodes[v].gy), d_[v]});
        }
        cacheValid_[s] = 1;
    }

    // ---- (maxFast 4): the max pass's insert candidates in O(log n) each.
    // In a legal row a[j] = site[j] - P[j] (P = prefix sum of widths) never decreases, so the cells
    // that an insert at site x pushes left are exactly those j < k with a[j] > x - P[k] (one binary
    // search), and pushed cell j lands at (x - P[k]) + P[j]. With e[j] = gx[j] - x0 - P[j] * sp its
    // new displacement is |T - e[j]| + dy[j], one T per side. From that: the chain's extent and its
    // edge checks (integers, exact), a lower bound of the cost change (|sum| <= sum of | |, prefix
    // sums), and which pushed cell is farthest (sparse-table argmax of dy - e and e + dy; that cell
    // is then checked with the walk's own expression). Only candidates that may still win are
    // walked with the original arithmetic, so every move is the one maxFast 3 makes.
    struct RowArrays {
        std::vector<int> P, a;                         // P: n + 1 prefix widths
        std::vector<double> e, se, sdy, sold;           // prefix sums: n + 1
        std::vector<std::vector<double>> hiMax, loMax;  // max of dy - e / e + dy over [j, j + 2^l)
        std::vector<std::vector<int>> hiArg, loArg;     // where it is
        double absSum = 0;                              // sum of |e| + dy + old (rounding margins)
        bool tables = false;                            // hi / lo tables built (on the first walk in the row)
    };
    std::vector<RowArrays> rowArr_;
    std::vector<std::pair<double, double>> gxy_;        // (gx, gy) per node, contiguous (maxFast 4 caches)

    void buildArrays(int s) {
        const auto& cache = rowCache_[s];
        const Segment& sg = db_.segs[s];
        RowArrays& A = rowArr_[s];
        const int n = (int)cache.size();
        A.P.assign(n + 1, 0);
        A.a.resize(n);
        A.e.resize(n);
        A.se.assign(n + 1, 0);
        A.sdy.assign(n + 1, 0);
        A.sold.assign(n + 1, 0);
        A.absSum = 0;
        A.tables = false;
        for (int j = 0; j < n; ++j) {
            const RowSearchCell& cell = cache[j];
            A.P[j + 1] = A.P[j] + cell.width;
            A.a[j] = cell.site - A.P[j];
            A.e[j] = cell.gx - sg.x0 - A.P[j] * sg.sp;
            A.se[j + 1] = A.se[j] + A.e[j];
            A.sdy[j + 1] = A.sdy[j] + cell.dy;
            A.sold[j + 1] = A.sold[j] + cell.oldD;
            A.absSum += std::fabs(A.e[j]) + cell.dy + cell.oldD;
        }
    }

    // Built by the worker that owns the row, only when one of its candidates is about to be walked.
    void buildTables(int s) {
        RowArrays& A = rowArr_[s];
        if (A.tables) return;
        const auto& cache = rowCache_[s];
        const int n = (int)cache.size();
        int levels = 1;
        while ((1 << levels) <= n) ++levels;
        A.hiMax.resize(levels);
        A.loMax.resize(levels);
        A.hiArg.resize(levels);
        A.loArg.resize(levels);
        A.hiMax[0].resize(n);
        A.loMax[0].resize(n);
        A.hiArg[0].resize(n);
        A.loArg[0].resize(n);
        for (int j = 0; j < n; ++j) {
            A.hiMax[0][j] = cache[j].dy - A.e[j];
            A.loMax[0][j] = A.e[j] + cache[j].dy;
            A.hiArg[0][j] = A.loArg[0][j] = j;
        }
        for (int l = 1; l < levels; ++l) {
            const int len = n - (1 << l) + 1, half = 1 << (l - 1);
            A.hiMax[l].resize(std::max(0, len));
            A.loMax[l].resize(std::max(0, len));
            A.hiArg[l].resize(std::max(0, len));
            A.loArg[l].resize(std::max(0, len));
            const double* h = A.hiMax[l - 1].data();
            const double* o = A.loMax[l - 1].data();
            const int* hi = A.hiArg[l - 1].data();
            const int* oi = A.loArg[l - 1].data();
            double* hh = A.hiMax[l].data();
            double* oo = A.loMax[l].data();
            int* hhi = A.hiArg[l].data();
            int* ooi = A.loArg[l].data();
            for (int j = 0; j < len; ++j) {
                const bool hr = h[j + half] > h[j], orr = o[j + half] > o[j];
                hh[j] = hr ? h[j + half] : h[j];
                hhi[j] = hr ? hi[j + half] : hi[j];
                oo[j] = orr ? o[j + half] : o[j];
                ooi[j] = orr ? oi[j + half] : oi[j];
            }
        }
        A.tables = true;
    }

    // A pushed run [lo, hi] (lo <= hi) landing at t + P[j]: 2 = some cell exceeds the bound (proven
    // with the walk's own expression on that cell: the walk would fail), 0 = every cell is within it
    // (by a margin far above rounding), 1 = unsure. The run's largest new displacement is
    // max(T + max(dy - e), -T + max(e + dy)); the two cells attaining them are checked exactly.
    int runStatus(int s, int lo, int hi, int t, double bound) const {
        const RowArrays& A = rowArr_[s];
        const auto& cache = rowCache_[s];
        const Segment& sg = db_.segs[s];
        int l = 0;
        while ((2 << l) <= hi - lo + 1) ++l;
        const int q = hi - (1 << l) + 1;
        const bool hq = A.hiMax[l][q] > A.hiMax[l][lo], lq = A.loMax[l][q] > A.loMax[l][lo];
        const int j1 = hq ? A.hiArg[l][q] : A.hiArg[l][lo], j2 = lq ? A.loArg[l][q] : A.loArg[l][lo];
        for (int j : {j1, j2}) {
            const int nx = t + A.P[j];
            const double nd = std::fabs(sg.x0 + nx * sg.sp - cache[j].gx) + cache[j].dy;
            if (!(nd <= bound + 1e-9 || nd <= cache[j].oldD + 1e-9)) return 2;
        }
        const double T = t * sg.sp;
        const double v = std::max(T + A.hiMax[0][j1], -T + A.loMax[0][j2]);
        const double tol = 1e-9 * (1 + std::fabs(T) + std::fabs(v) + std::fabs(sg.x0));
        return v <= bound - tol ? 0 : 1;
    }

    struct FastCand {
        double lb = 0, margin = 0, delta = INF;
        int seg = -1, k = 0, x = 0;
        int kind = 0;     // 0 insert (lb), 1 swap (exact), 2 c's own segment (exact, its tryInsert plan)
        int status = 0;   // insert: 0 not walked, 1 walked and allowed, 2 not allowed
        int b = -1, swapSeg = -1, swapSite = 0;
        int j0 = 0, j1 = 0;   // insert: the pushed runs are [j0, k) and [k, j1)
    };
    std::vector<std::vector<FastCand>> rowCands_;
    Plan fastOwn_;

    void releaseMaxStorage(bool verbose) {
        assert(!inMax_);
        const double t = verbose ? wallSeconds() : 0;
        // The entire max/matching phase has finished, and every dispatch has
        // joined. Keep the blocking pool/workspace shells and the placement/
        // RowMap/distances; later non-max operations may still use those.
        team_.reset();
        decltype(rowArr_)().swap(rowArr_);       // includes all sparse-table levels
        decltype(rowCache_)().swap(rowCache_);
        decltype(gxy_)().swap(gxy_);
        decltype(rowCands_)().swap(rowCands_);
        decltype(cacheValid_)().swap(cacheValid_);
        decltype(patched_)().swap(patched_);
        decltype(rowPlans_)().swap(rowPlans_);
        decltype(searchRows_)().swap(searchRows_);
        decltype(rowWeights_)().swap(rowWeights_);
        decltype(rowRanges_)().swap(rowRanges_);
        fastOwn_ = Plan();
        for (auto& w : searchWork_) {
            decltype(w.pushes)().swap(w.pushes);
            decltype(w.open)().swap(w.open);
            decltype(w.rows)().swap(w.rows);
        }
        ++maxReleaseCount_;
        if (verbose) std::fprintf(stderr, "[max-release] phase %d released %.6fs\n", maxReleaseCount_, wallSeconds() - t);
    }

    void fastInserts(int c, int s, double bound, std::vector<FastCand>& out, MaxProfile& profile) const {
        const Segment& sg = db_.segs[s];
        const Node& nc = db_.nodes[c];
        const int wc = db_.sitesOf(c, sg);
        const double dy = std::fabs(sg.y - nc.gy);
        const double r = (bound - dy) / sg.sp;
        if (r < 0) return;
        const double uc = (nc.gx - sg.x0) / sg.sp;
        const int cLo = std::max(0, (int)std::ceil(uc - r - 1e-9));
        const int cHi = std::min(sg.numSites - wc, (int)std::floor(uc + r + 1e-9));
        if (cLo > cHi) return;
        const auto& cache = rowCache_[s];
        const RowArrays& A = rowArr_[s];
        const int m = (int)cache.size();
        const double cc = uc + wc / 2.0;
        const int k0 = (int)(std::lower_bound(cache.begin(), cache.end(), cc, [](const RowSearchCell& v, double x) {
                                 return v.site + v.width / 2.0 < x;
                             }) - cache.begin());
        const double oldC = d_[c];
        for (int k = std::max(0, k0 - 1); k <= std::min(m, k0 + 1); ++k) {
            const int gLo = k > 0 ? cache[k - 1].site + cache[k - 1].width : 0;
            const int gHi = k < m ? cache[k].site : sg.numSites;
            long xs[4] = {std::lround(uc), gLo, gHi - wc, std::max<long>(gLo, std::min<long>(gHi - wc, std::lround(uc)))};
            for (long& x : xs) x = std::max<long>(cLo, std::min<long>(cHi, x));
            std::sort(xs, xs + 4);
            int j0 = 0, j1 = k;
            for (int i = 0; i < 4; ++i) {
                if (i > 0 && xs[i] == xs[i - 1]) continue;
                const int x = (int)xs[i];
                if (profileMax_) ++profile.candidates;
                const int tL = x - A.P[k], tR = x + wc - A.P[k];
                if (maxFast_ < 5 || i == 0) {
                    j0 = int(std::upper_bound(A.a.begin(), A.a.begin() + k, tL) - A.a.begin());
                    j1 = int(std::lower_bound(A.a.begin() + k, A.a.begin() + m, tR) - A.a.begin());
                    if (profileMax_) profile.endpointBinaries += 2;
                } else {
                    // For this k, distinct xs (hence tL/tR) increase. Reuse the
                    // previous endpoints; the strict inequalities reproduce the
                    // original upper/lower_bound including equal a[] plateaus.
                    // Bound each scan so a long plateau never reintroduces O(m).
                    int steps = 0;
                    while (j0 < k && A.a[j0] <= tL && steps < 8) { ++j0; ++steps; }
                    if (profileMax_) profile.endpointSteps += steps;
                    if (j0 < k && A.a[j0] <= tL) {
                        j0 = int(std::upper_bound(A.a.begin() + j0, A.a.begin() + k, tL) - A.a.begin());
                        if (profileMax_) { ++profile.endpointBinaries; ++profile.endpointFallbacks; }
                    }
                    steps = 0;
                    while (j1 < m && A.a[j1] < tR && steps < 8) { ++j1; ++steps; }
                    if (profileMax_) profile.endpointSteps += steps;
                    if (j1 < m && A.a[j1] < tR) {
                        j1 = int(std::lower_bound(A.a.begin() + j1, A.a.begin() + m, tR) - A.a.begin());
                        if (profileMax_) { ++profile.endpointBinaries; ++profile.endpointFallbacks; }
                    }
                }
                if (j0 < k && tL + A.P[j0] < 0) continue;            // the leftmost pushed cell leaves the row
                if (j1 > k && tR + A.P[j1] > sg.numSites) continue;  // the rightmost one does
                const double delta0 = dispAt(c, sg, x) - oldC;
                const double TL = tL * sg.sp, TR = tR * sg.sp;
                const int L = k - j0, R = j1 - k;
                FastCand f;
                f.lb = delta0 + std::fabs(L * TL - (A.se[k] - A.se[j0])) + (A.sdy[k] - A.sdy[j0]) - (A.sold[k] - A.sold[j0]) +
                       std::fabs(R * TR - (A.se[j1] - A.se[k])) + (A.sdy[j1] - A.sdy[k]) - (A.sold[j1] - A.sold[k]);
                // prefix sums of <= m terms and the walk's own sum of <= m + 1 terms: each off by at most
                // m * 2^-53 of the magnitudes involved; 1e-12 is ~9000 times that
                f.margin = 1e-6 + 1e-12 * (m + 8) *
                           (A.absSum + (L + R + 1) * (std::fabs(TL) + std::fabs(TR) + std::fabs(sg.x0)) + std::fabs(delta0) + oldC);
                f.seg = s; f.k = k; f.x = x; f.j0 = j0; f.j1 = j1;
                out.push_back(f);
            }
        }
    }

    // rm_.cellAt + trySwap for a segment s that does not hold c, from the row cache: the cell covering
    // site tt of s and its room, then trySwap's own expressions. False = no swap plan.
    bool fastSwap(int c, int s, int tt, double bound, const Gap& roomC, FastCand& f) const {
        const auto& cache = rowCache_[s];
        const int m = (int)cache.size();
        const int j = int(std::upper_bound(cache.begin(), cache.end(), tt, [](int t2, const RowSearchCell& a) {
                              return t2 < a.site; }) - cache.begin()) - 1;
        if (!(j >= 0 && cache[j].site + cache[j].width > tt)) return false;
        const int b = rm_.cells(s)[j], sc = P_.seg[c];
        const Segment &gc = db_.segs[sc], &gb = db_.segs[s];
        const int wc = db_.sitesOf(c, gb), wb = db_.sitesOf(b, gc);
        Gap roomB;
        roomB.lo = j > 0 ? cache[j - 1].site + cache[j - 1].width : 0;
        roomB.hi = j + 1 < m ? cache[j + 1].site : gb.numSites;
        if (roomB.width() < wc || roomC.width() < wb) return false;
        const Node& nc = db_.nodes[c];
        const int xc = (int)std::max<long>(roomB.lo, std::min<long>(roomB.hi - wc, std::lround((nc.gx - gb.x0) / gb.sp)));
        const int xb = (int)std::max<long>(roomC.lo, std::min<long>(roomC.hi - wb, std::lround((gxy_[b].first - gc.x0) / gc.sp)));
        const double dc = dispAt(c, gb, xc);
        const double dbb = std::fabs(gc.x0 + xb * gc.sp - gxy_[b].first) + std::fabs(gc.y - gxy_[b].second);
        if (dc > bound + 1e-9 || dbb > bound + 1e-9) return false;
        f.delta = dc + dbb - d_[c] - cache[j].oldD;
        f.seg = s; f.x = xc; f.b = b; f.swapSeg = sc; f.swapSite = xb;
        return true;
    }

    // The original cached walk (tryInsert's inner loops, same order and expressions): delta or INF.
    double walkInsert(int c, const FastCand& f, double bound, unsigned long long* visits) const {
        const Segment& sg = db_.segs[f.seg];
        const auto& cache = rowCache_[f.seg];
        const int m = (int)cache.size(), wc = db_.sitesOf(c, sg), k = f.k, x = f.x;
        double delta = dispAt(c, sg, x) - d_[c];
        int b = x;
        for (int j = k - 1; j >= 0; --j) {
            const RowSearchCell& cell = cache[j];
            if (visits) ++*visits;
            if (cell.site + cell.width <= b) break;
            const int nx = b - cell.width;
            if (nx < 0) return INF;
            const double nd = std::fabs(sg.x0 + nx * sg.sp - cell.gx) + cell.dy;
            if (!(nd <= bound + 1e-9 || nd <= cell.oldD + 1e-9)) return INF;
            delta += nd - cell.oldD;
            b = nx;
        }
        b = x + wc;
        for (int j = k; j < m; ++j) {
            const RowSearchCell& cell = cache[j];
            if (visits) ++*visits;
            if (cell.site >= b) break;
            const int nx = b;
            if (nx + cell.width > sg.numSites) return INF;
            const double nd = std::fabs(sg.x0 + nx * sg.sp - cell.gx) + cell.dy;
            if (!(nd <= bound + 1e-9 || nd <= cell.oldD + 1e-9)) return INF;
            delta += nd - cell.oldD;
            b = nx + cell.width;
        }
        return delta;
    }

    // maxFast 4: same rows, same candidates and the same winner (first strict minimum in maxFast 3's
    // visit order: rows by distance, per row every segment's inserts, then every segment's swap).
    void searchFast(int c, double bound, Plan& best) {
        const int workers = team_ ? std::min(team_->size(), std::max(1, int(searchRows_.size() / 4))) : 1;
        if (searchWork_.size() < size_t(workers)) searchWork_.resize(workers);
        double tc = 0;
        if (rowCands_.size() < searchRows_.size()) rowCands_.resize(searchRows_.size());
        for (int t = 0; t < workers; ++t) searchWork_[t].profile = MaxProfile();
        const int own = P_.seg[c];
        const Gap ownRoom = own >= 0 ? rm_.freeAround(c) : Gap();   // c's room: trySwap's roomC outside c's segment
        const bool weighted = maxFast_ == 6 && workers > 1;
        if (weighted) {
            const double tw = profileMax_ ? wallSeconds() : 0;
            rowWeights_.resize(searchRows_.size());
            rowRanges_.resize(workers);
            unsigned long long remaining = 0;
            for (size_t i = 0; i < searchRows_.size(); ++i) {
                unsigned long long weight = 1;
                for (int s : db_.rows[searchRows_[i].first].segs) {
                    const size_t m = rm_.cells(s).size();
                    // At most three insertion positions, four x candidates at
                    // each, plus one swap. The original own-segment walk and a
                    // stale cache also inspect row cells; count those cheaply.
                    // These are deterministic work estimates, not elapsed times.
                    weight += 4 * std::min<size_t>(3, m + 1) + 1;
                    if (s == own) weight += m;
                    if (!cacheValid_[s]) weight += m;
                }
                rowWeights_[i] = weight;
                remaining += weight;
            }
            size_t next = 0;
            for (int t = 0; t < workers; ++t) {
                const size_t begin = next;
                const int left = workers - t;
                const size_t limit = searchRows_.size() - size_t(left - 1);
                const unsigned long long target = (remaining + left - 1) / left;
                unsigned long long weight = 0;
                // Keep one or more rows for every worker. Contiguous partitions
                // take O(rows), and each row/cache still has exactly one writer.
                while (next < limit && (next == begin || weight < target)) weight += rowWeights_[next++];
                rowRanges_[t] = {begin, next};
                remaining -= weight;
            }
            if (profileMax_) maxProfile_.rowPartitionSeconds += wallSeconds() - tw;
        }
        auto scan = [&](int t) {
            SearchWorkspace& ws = searchWork_[t];
            const double t0 = profileMax_ ? wallSeconds() : 0;
            ws.rows.clear();
            const size_t begin = weighted ? rowRanges_[t].first : size_t(t);
            const size_t end = weighted ? rowRanges_[t].second : searchRows_.size();
            const size_t stride = weighted ? 1 : size_t(workers);
            for (size_t i = begin; i < end; i += stride) {
                ws.rows.push_back(int(i));
                auto& out = rowCands_[i];
                out.clear();
                const int r = searchRows_[i].first;
                if (profileMax_) ++ws.profile.rows;
                // Rows are split between workers and a row's segments are read only by its worker
                // (inserts, walks), so the worker rebuilds its own stale row caches: distinct elements.
                const double tb = profileMax_ ? wallSeconds() : 0;
                for (int s : db_.rows[r].segs) {
                    if (profileMax_ && !cacheValid_[s]) ++ws.profile.cacheBuilds;
                    ensureRowCache(s);
                }
                if (profileMax_) ws.profile.cacheSeconds += wallSeconds() - tb;
                for (int s : db_.rows[r].segs) {
                    if (s == own) {   // c's own segment: its view leaves c out; keep maxFast 3's search
                        fastOwn_ = Plan();
                        tryInsert(c, s, bound, fastOwn_, -1, &ws);
                        FastCand f;
                        f.kind = 2; f.seg = s; f.delta = fastOwn_.delta;
                        out.push_back(f);
                        continue;
                    }
                    if (profileMax_) ++ws.profile.inserts;
                    fastInserts(c, s, bound, out, ws.profile);
                }
                if (own < 0) continue;
                const Node& n = db_.nodes[c];
                for (int s : db_.rows[r].segs) {
                    const Segment& sg = db_.segs[s];
                    const int tt = (int)std::floor((n.gx + n.w / 2 - sg.x0) / sg.sp);
                    if (tt < 0 || tt >= sg.numSites) continue;
                    FastCand f;
                    if (s == own) {   // rm_.cellAt / trySwap, as maxFast 3
                        Gap g;
                        const int b = rm_.cellAt(s, tt, g, c);
                        if (b < 0) continue;
                        Plan p;
                        trySwap(c, b, bound, p);
                        if (p.delta == INF) continue;
                        f.delta = p.delta; f.seg = p.seg; f.x = p.site;
                        f.b = b; f.swapSeg = p.swapSeg; f.swapSite = p.swapSite;
                    } else if (!fastSwap(c, s, tt, bound, ownRoom, f)) continue;
                    f.kind = 1;
                    out.push_back(f);
                }
            }
            // This worker's rows: walk its inserts by increasing lower bound against its own incumbent.
            // The global incumbent (minimum over workers) is never higher, so every insert whose bound
            // could still beat it has been walked by its worker: one pool run per move.
            auto& open = ws.open;
            open.clear();
            double U = INF;
            for (int i : ws.rows)
                for (size_t j = 0; j < rowCands_[i].size(); ++j) {
                    const FastCand& f = rowCands_[i][j];
                    if (f.kind) U = std::min(U, f.delta);
                    else open.push_back({f.lb - f.margin, {int(i), int(j)}});
                }
            if (profileMax_) ws.profile.scanSeconds += wallSeconds() - t0;
            std::sort(open.begin(), open.end());
            unsigned long long* visits = profileMax_ ? &ws.profile.pushVisits : nullptr;
            for (const auto& o : open) {
                if (o.first > U) break;
                FastCand& f = rowCands_[o.second.first][o.second.second];
                // a run with a cell proven past the bound: the walk would fail
                buildTables(f.seg);
                const int wc = db_.sitesOf(c, db_.segs[f.seg]);
                const RowArrays& A = rowArr_[f.seg];
                if ((f.j0 < f.k && runStatus(f.seg, f.j0, f.k - 1, f.x - A.P[f.k], bound) == 2) ||
                    (f.j1 > f.k && runStatus(f.seg, f.k, f.j1 - 1, f.x + wc - A.P[f.k], bound) == 2)) {
                    f.status = 2;
                    if (profileMax_) ++ws.profile.proven;
                    continue;
                }
                f.delta = walkInsert(c, f, bound, visits);
                f.status = f.delta < INF ? 1 : 2;
                if (f.status == 1) U = std::min(U, f.delta);
                if (profileMax_) ++ws.profile.walks;
            }
            if (profileMax_) ws.profile.stepSeconds = wallSeconds() - t0;
        };
        if (workers > 1) team_->run(workers, scan);
        else scan(0);
        if (profileMax_) {
            double mx = 0;
            for (int t = 0; t < workers; ++t) mx = std::max(mx, searchWork_[t].profile.stepSeconds);
            maxProfile_.critical += mx;
        }
        if (profileMax_) for (int t = 0; t < workers; ++t) {
            const MaxProfile& a = searchWork_[t].profile;
            maxProfile_.rows += a.rows; maxProfile_.inserts += a.inserts;
            maxProfile_.candidates += a.candidates; maxProfile_.pushVisits += a.pushVisits;
            maxProfile_.pushStores += a.pushStores; maxProfile_.walks += a.walks;
            maxProfile_.cacheSeconds += a.cacheSeconds; maxProfile_.cacheBuilds += a.cacheBuilds;
            maxProfile_.proven += a.proven;
            maxProfile_.scanSeconds += a.scanSeconds;
            maxProfile_.work += a.stepSeconds;
            maxProfile_.endpointBinaries += a.endpointBinaries;
            maxProfile_.endpointSteps += a.endpointSteps;
            maxProfile_.endpointFallbacks += a.endpointFallbacks;
            maxProfile_.insertSampleSeconds += a.insertSampleSeconds; maxProfile_.insertSamples += a.insertSamples;
            for (size_t k = 0; k < a.pushLengths.size(); ++k) maxProfile_.pushLengths[k] += a.pushLengths[k];
        }
        // replay in visit order: the first strict minimum
        tc = profileMax_ ? wallSeconds() : 0;
        const FastCand* win = nullptr;
        double bestD = best.delta;
        for (size_t i = 0; i < searchRows_.size(); ++i)
            for (const FastCand& f : rowCands_[i]) {
                const double d = f.kind ? f.delta : (f.status == 1 ? f.delta : INF);
                if (d < bestD) { bestD = d; win = &f; }
            }
        if (profileMax_) maxProfile_.replaySeconds += wallSeconds() - tc;
        if (!win) return;
        if (win->kind == 2) { best = fastOwn_; return; }
        best.delta = win->delta;
        best.partner = -1;
        best.pushes.clear();
        best.pushes2.clear();
        if (win->kind == 1) {
            best.seg = win->seg; best.site = win->x;
            best.swapWith = win->b; best.swapSeg = win->swapSeg; best.swapSite = win->swapSite;
            return;
        }
        best.seg = win->seg; best.site = win->x; best.swapWith = -1;
        const Segment& sg = db_.segs[win->seg];
        const auto& cache = rowCache_[win->seg];
        const int m = (int)cache.size();
        int edge = win->x;   // the deferred replay of maxFast 3: left run, then right run
        for (int j = win->k - 1; j >= 0; --j) {
            if (cache[j].site + cache[j].width <= edge) break;
            edge -= cache[j].width;
            best.pushes.push_back({rm_.cells(win->seg)[j], edge});
        }
        edge = win->x + db_.sitesOf(c, sg);
        for (int j = win->k; j < m; ++j) {
            if (cache[j].site >= edge) break;
            best.pushes.push_back({rm_.cells(win->seg)[j], edge});
            edge += cache[j].width;
        }
    }

    void dirtyRow(int s) {
        if (s >= 0 && size_t(s) < cacheValid_.size()) cacheValid_[s] = 0;
        if (s >= 0 && size_t(s) < patched_.size()) patched_[s] = 0;
    }


    // c and b (different rows) trade rows; each is inserted into the other's
    // row with the new neighbours pushed just far enough (the plain swap needs
    // both to fit in the other's free room). Moves that the total-optimal
    // placements use but single-cell moves cannot reach.
    void trySwapPush(int c, int b, double bound, Plan& best, SearchWorkspace* workspace = nullptr) {
        if (P_.seg[c] < 0 || P_.seg[b] < 0 || P_.seg[c] == P_.seg[b]) return;
        Plan p1, p2;
        tryInsert(c, P_.seg[b], bound, p1, b, workspace);
        if (p1.delta == INF) return;
        tryInsert(b, P_.seg[c], bound, p2, c, workspace);
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
        double phaseT = profileMax_ ? wallSeconds() : 0;
        if (profileMax_) ++maxProfile_.evictCalls;
        // Ejection temporarily removes cells and can undo a tentative move. Keep that uncommon
        // path on the original views, and invalidate caches before returning to the normal pass.
        // maxFast 4 invalidates only the segments it changes (b's and c's; apply marks its own),
        // so the next search does not rebuild every row cache (L1: 259 evicts x ~210 rows).
        if (maxFast_ < 4) std::fill(cacheValid_.begin(), cacheValid_.end(), 0);
        struct SuspendCache {
            bool& flag; bool saved;
            explicit SuspendCache(bool& value) : flag(value), saved(value) { flag = true; }
            ~SuspendCache() { flag = saved; }
        } suspended(cacheSuspended_);
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
                for (; it != cs.end() && P_.site[*it] <= uc + rr; ++it) {
                    if (*it == c) continue;
                    bool roomTooSmall = false;
                    if (roomFirst) {
                        bool direct;
                        roomTooSmall = evictionRoomWidth(db_, P_, rm_, s, it, maxFast_ >= 7, direct) < wc;
                        if (profileMax_) {
                            if (direct) ++maxProfile_.evictDirectRooms;
                            else ++maxProfile_.evictRoomFallbacks;
                        }
                    }
                    cand.push_back({d_[*it] + (roomTooSmall ? 1e18 : 0.0), *it});
                }
            }
        }, [&] { return bound; });
        if (profileMax_) {
            maxProfile_.evictGatherSeconds += wallSeconds() - phaseT;
            maxProfile_.evictCandidates += cand.size();
            phaseT = wallSeconds();
        }
        size_t K = std::min<size_t>(cand.size(), 16);
        std::partial_sort(cand.begin(), cand.begin() + K, cand.end());
        if (profileMax_) maxProfile_.evictSelectSeconds += wallSeconds() - phaseT;

        for (size_t i = 0; i < K; ++i) {
            phaseT = profileMax_ ? wallSeconds() : 0;
            if (profileMax_) ++maxProfile_.evictChoices;
            int b = cand[i].second, sb = P_.seg[b], xb = P_.site[b];
            dirtyRow(sb);
            dirtyRow(P_.seg[c]);
            rm_.remove(b);
            P_.seg[b] = -1;
            Plan pc;
            if (profileMax_) {
                maxProfile_.evictLiftSeconds += wallSeconds() - phaseT;
                phaseT = wallSeconds();
            }
            tryInsert(c, sb, bound, pc);
            if (profileMax_) maxProfile_.evictInsertSeconds += wallSeconds() - phaseT;
            if (pc.delta < INF) {
                phaseT = profileMax_ ? wallSeconds() : 0;
                if (profileMax_) ++maxProfile_.evictInsertOK;
                int oc = P_.seg[c], ox = P_.site[c];
                std::vector<std::pair<int, int>> undo;
                for (auto [v, x] : pc.pushes) undo.push_back({v, P_.site[v]});
                apply(c, pc);
                Plan pb;
                if (profileMax_) {
                    maxProfile_.evictPrepareSeconds += wallSeconds() - phaseT;
                    phaseT = wallSeconds();
                }
                if (maxFast_ >= 4) {
                    // the rows c and b left or entered are marked stale; every other cache
                    // still matches the placement, so b's search can use them (maxFast 4's path)
                    cacheSuspended_ = false;
                    searchFor(b, bound, bound, pb);
                    cacheSuspended_ = true;
                } else {
                    searchFor(b, bound, bound, pb);
                }
                if (profileMax_) maxProfile_.evictSearchSeconds += wallSeconds() - phaseT;
                if (pb.delta < INF) {
                    phaseT = profileMax_ ? wallSeconds() : 0;
                    apply(b, pb);
                    if (profileMax_) {
                        ++maxProfile_.evictSearchOK;
                        maxProfile_.evictCommitSeconds += wallSeconds() - phaseT;
                    }
                    return true;
                }
                phaseT = profileMax_ ? wallSeconds() : 0;
                dirtyRow(sb);
                dirtyRow(oc);
                rm_.remove(c);
                for (auto [v, x] : undo) {
                    rm_.slide(v, x);
                    d_[v] = displacementOf(db_, P_, v);
                }
                if (oc >= 0) rm_.insert(c, oc, ox);
                else P_.seg[c] = -1;
                d_[c] = displacementOf(db_, P_, c);
                if (profileMax_) maxProfile_.evictRollbackSeconds += wallSeconds() - phaseT;
            }
            phaseT = profileMax_ ? wallSeconds() : 0;
            rm_.insert(b, sb, xb);
            if (profileMax_) {
                ++maxProfile_.evictRollbacks;   // every failed choice restores b; c may also have been undone
                maxProfile_.evictRollbackSeconds += wallSeconds() - phaseT;
            }
        }
        return false;
    }

    // Best move for c with every moved cell ending at displacement <= bound.
    // Only rows closer than `reach` (vertically) are searched. A cell that is
    // currently lifted out (seg -1) can only be inserted.
    // prune = stop at rows where c's own displacement alone (>= dy) would already make the move
    // worse than the best plan found (pushes of other cells are not counted: a heuristic, max pass only)
    void searchRow(int c, int r, double bound, Plan& best, SearchWorkspace* workspace = nullptr) {
        if (profileMax_) ++(workspace ? workspace->profile : maxProfile_).rows;
        for (int s : db_.rows[r].segs) {
            ensureRowCache(s);
            tryInsert(c, s, bound, best, -1, workspace);
        }
        if (P_.seg[c] < 0) return;
        const Node& n = db_.nodes[c];
        for (int s : db_.rows[r].segs) {
            const Segment& sg = db_.segs[s];
            int t = (int)std::floor((n.gx + n.w / 2 - sg.x0) / sg.sp);
            if (t < 0 || t >= sg.numSites) continue;
            Gap g;
            int b = rm_.cellAt(s, t, g, c);
            if (b >= 0) trySwap(c, b, bound, best);
            if (b >= 0 && swapPush_) trySwapPush(c, b, bound, best, workspace);
        }
    }

    void searchFor(int c, double bound, double reach, Plan& best, bool prune = false) {
        const Node& n = db_.nodes[c];
        const double oldC = d_[c];
        auto limit = [&] { return prune && best.delta < INF ? std::min(reach, oldC + best.delta) : reach; };
        if (maxFast_ >= 4 && inMax_ && !cacheSuspended_ && !swapPush_ && !prune) {
            searchRows_.clear();
            forRowsNear(db_, n.gy, [&](int r, double dy) {
                if (dy <= bound) searchRows_.push_back({r, dy});
            }, [&] { return reach; });
            searchFast(c, bound, best);
            return;
        }
        if (maxFast_ && inMax_ && maxThreads_ > 1 && db_.rows.size() >= 32) {
            searchRows_.clear();
            forRowsNear(db_, n.gy, [&](int r, double dy) {
                if (dy <= bound) searchRows_.push_back({r, dy});
            }, [&] { return reach; });
            // No placement or RowMap mutation occurs until every candidate reader has joined.
            // Small neighbourhoods retain the original serial path: dispatch would cost more.
            if (searchRows_.size() >= 32) {
                const int workers = std::min(maxThreads_, int(searchRows_.size() / 4));
                if (!searchPool_) {
                    searchPool_ = std::make_unique<Pool>(maxThreads_);
                    searchWork_.resize(maxThreads_);
                }
                rowPlans_.resize(searchRows_.size());
                // Cache construction is serialized; worker threads only read its completed arrays.
                for (const auto& row : searchRows_) for (int s : db_.rows[row.first].segs) ensureRowCache(s);
                for (int t = 0; t < workers; ++t) searchWork_[t].profile = MaxProfile();
                searchPool_->run(workers, [&](int t) {
                    SearchWorkspace& workspace = searchWork_[t];
                    for (size_t i = size_t(t); i < searchRows_.size(); i += size_t(workers)) {
                        Plan& row = rowPlans_[i];
                        row.delta = INF; row.seg = -1; row.swapWith = row.partner = -1;
                        row.pushes.clear(); row.pushes2.clear();
                        searchRow(c, searchRows_[i].first, bound, row, &workspace);
                    }
                });
                // Replay the ORIGINAL visit order, including its heuristic prune boundary.
                // Extra speculative row evaluations cannot change the incumbent or tie break.
                for (size_t i = 0; i < searchRows_.size(); ++i) {
                    if (searchRows_[i].second >= limit()) break;
                    if (rowPlans_[i].delta < best.delta) best = rowPlans_[i];
                }
                if (profileMax_) for (int t = 0; t < workers; ++t) {
                    const MaxProfile& a = searchWork_[t].profile;
                    maxProfile_.rows += a.rows; maxProfile_.inserts += a.inserts;
                    maxProfile_.candidates += a.candidates; maxProfile_.pushVisits += a.pushVisits;
                    maxProfile_.pushStores += a.pushStores;
                    maxProfile_.pushReplayVisits += a.pushReplayVisits;
                    maxProfile_.insertSampleSeconds += a.insertSampleSeconds; maxProfile_.insertSamples += a.insertSamples;
                    for (size_t k = 0; k < a.pushLengths.size(); ++k) maxProfile_.pushLengths[k] += a.pushLengths[k];
                }
                return;
            }
        }
        forRowsNear(db_, n.gy, [&](int r, double dy) {
            if (dy <= bound) searchRow(c, r, bound, best);
        }, limit);
    }

    // skip: another cell to treat as removed from s (swap-with-push), or -1.
    void tryInsert(int c, int s, double bound, Plan& best, int skip = -1, SearchWorkspace* workspace = nullptr) {
        MaxProfile& profile = workspace ? workspace->profile : maxProfile_;
        auto& pushes = workspace ? workspace->pushes : pushes_;
        // A sampled timer avoids a clock call for every one of the millions of row trials.
        // The exact counts and raw sampled seconds are reported only under -v.
        if (profileMax_) ++profile.inserts;
        const bool sampled = profileMax_ &&
            (((workspace ? ++workspace->insertSampleCursor : profile.inserts) & 255) == 0);
        struct SampleTimer {
            MaxProfile* p; double start;
            ~SampleTimer() { if (p) { p->insertSampleSeconds += wallSeconds() - start; ++p->insertSamples; } }
        } timer{sampled ? &profile : nullptr, sampled ? wallSeconds() : 0};
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
        const bool cached = maxFast_ >= 2 && inMax_ && !cacheSuspended_ && skip < 0 && cacheValid_[s];
        const auto* cache = cached ? &rowCache_[s] : nullptr;
        const bool deferred = maxFast_ >= 3 && inMax_;
        int i1 = P_.seg[c] == s ? rm_.indexOf(c) : -1;
        int i2 = skip >= 0 && P_.seg[skip] == s ? rm_.indexOf(skip) : -1;
        if (i1 > i2) std::swap(i1, i2);   // i1 < i2, -1 = none
        int m = (int)all.size() - (i1 >= 0) - (i2 >= 0);
        auto index = [&](int j) {
            if (i1 >= 0 && j >= i1) ++j;
            if (i2 >= 0 && j >= i2) ++j;
            return j;
        };
        auto L = [&](int j) { return all[index(j)]; };
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
                if (profileMax_) ++profile.candidates;
                double delta = dispAt(c, sg, x) - oldC;
                pushes.clear();
                size_t pushCount = 0;
                // left side: push left just enough
                int b = x;
                bool ok = true;
                for (int j = k - 1; j >= 0 && ok; --j) {
                    const int at = index(j), v = all[at];
                    const RowSearchCell* cell = cached ? &(*cache)[at] : nullptr;
                    const int wv = cell ? cell->width : db_.sitesOf(v, sg);
                    if (profileMax_) ++profile.pushVisits;
                    if ((cell ? cell->site : P_.site[v]) + wv <= b) break;
                    int nx = b - wv;
                    if (nx < 0) { ok = false; break; }
                    const double nd = cell ? std::fabs(sg.x0 + nx * sg.sp - cell->gx) + cell->dy : dispAt(v, sg, nx);
                    const double old = cell ? cell->oldD : d_[v];
                    if (!(nd <= bound + 1e-9 || nd <= old + 1e-9)) { ok = false; break; }
                    delta += nd - old;
                    ++pushCount;
                    if (!deferred) pushes.push_back({v, nx});
                    b = nx;
                }
                b = x + wc;
                for (int j = k; j < m && ok; ++j) {
                    const int at = index(j), v = all[at];
                    const RowSearchCell* cell = cached ? &(*cache)[at] : nullptr;
                    const int wv = cell ? cell->width : db_.sitesOf(v, sg);
                    if (profileMax_) ++profile.pushVisits;
                    if ((cell ? cell->site : P_.site[v]) >= b) break;
                    int nx = b;
                    if (nx + wv > sg.numSites) { ok = false; break; }
                    const double nd = cell ? std::fabs(sg.x0 + nx * sg.sp - cell->gx) + cell->dy : dispAt(v, sg, nx);
                    const double old = cell ? cell->oldD : d_[v];
                    if (!(nd <= bound + 1e-9 || nd <= old + 1e-9)) { ok = false; break; }
                    delta += nd - old;
                    ++pushCount;
                    if (!deferred) pushes.push_back({v, nx});
                    b = nx + wv;
                }
                if (profileMax_) {
                    const size_t n = pushCount;
                    ++profile.pushLengths[n == 0 ? 0 : n == 1 ? 1 : n <= 4 ? 2 : n <= 16 ? 3 : n <= 64 ? 4 : 5];
                    if (!deferred) profile.pushStores += pushCount;
                }
                if (ok && delta < best.delta) {
                    if (deferred) {
                        if (profileMax_) profile.pushStores += pushCount;
                        // A3 mode 3: unsuccessful/nonwinning candidates need a cost, not a list.
                        // The first pass kept the original allowed checks and floating additions.
                        // This row is immutable until all readers join, so replaying its (k, x)
                        // reconstructs exactly the same left-then-right push sequence. No cost is
                        // recomputed or reassociated; only improving candidates materialize stores.
                        pushes.reserve(pushCount);
                        int edge = x;
                        for (int j = k - 1; j >= 0; --j) {
                            const int at = index(j), v = all[at];
                            const RowSearchCell* cell = cached ? &(*cache)[at] : nullptr;
                            const int width = cell ? cell->width : db_.sitesOf(v, sg);
                            if (profileMax_) ++profile.pushReplayVisits;
                            if ((cell ? cell->site : P_.site[v]) + width <= edge) break;
                            edge -= width;
                            pushes.push_back({v, edge});
                        }
                        edge = x + wc;
                        for (int j = k; j < m; ++j) {
                            const int at = index(j), v = all[at];
                            const RowSearchCell* cell = cached ? &(*cache)[at] : nullptr;
                            const int width = cell ? cell->width : db_.sitesOf(v, sg);
                            if (profileMax_) ++profile.pushReplayVisits;
                            if ((cell ? cell->site : P_.site[v]) >= edge) break;
                            pushes.push_back({v, edge});
                            edge += width;
                        }
                        assert(pushes.size() == pushCount);
                    }
                    best.delta = delta;
                    best.seg = s;
                    best.site = x;
                    best.swapWith = -1;
                    best.partner = -1;
                    best.pushes = pushes;
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
        // an insert in the max pass patches the two row caches it changes (c leaves one
        // segment, enters another and pushes a run there) instead of a full rebuild from the
        // placement: the entries are what a rebuild would gather; the arrays are rebuilt later.
        const bool patch = maxFast_ >= 4 && inMax_ && !cacheSuspended_ && p.swapWith < 0 && p.partner < 0;
        const int sA = P_.seg[c];
        int iA = -1;
        if (patch && sA >= 0 && (cacheValid_[sA] || patched_[sA])) iA = rm_.indexOf(c);
        if (!patch) { dirtyRow(P_.seg[c]); dirtyRow(p.seg); }
        if (p.swapWith >= 0) { dirtyRow(P_.seg[p.swapWith]); dirtyRow(p.swapSeg); }
        if (p.partner >= 0) { dirtyRow(P_.seg[p.partner]); dirtyRow(p.seg2); }
        // remember whose displacement may change (for reduceMax's heap)
        touched_.push_back(c);
        if (p.swapWith >= 0) touched_.push_back(p.swapWith);
        if (p.partner >= 0) touched_.push_back(p.partner);
        for (auto [v, x] : p.pushes) touched_.push_back(v);
        for (auto [v, x] : p.pushes2) touched_.push_back(v);
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
        if (patch) {
            if (iA >= 0) {
                rowCache_[sA].erase(rowCache_[sA].begin() + iA);
                cacheValid_[sA] = 0;
                patched_[sA] = 1;
            }
            const int sB = p.seg;
            if (cacheValid_[sB] || patched_[sB]) {
                auto& cache = rowCache_[sB];
                const Segment& sg = db_.segs[sB];
                cache.insert(cache.begin() + rm_.indexOf(c), RowSearchCell{p.site, db_.sitesOf(c, sg), gxy_[c].first,
                                                                           std::fabs(sg.y - gxy_[c].second), d_[c]});
                for (auto [v, x] : p.pushes) {
                    RowSearchCell& e = cache[rm_.indexOf(v)];
                    e.site = x;
                    e.oldD = d_[v];
                }
                cacheValid_[sB] = 0;
                patched_[sB] = 1;
            }
        }
    }

    const PlaceDB& db_;
    Placement& P_;
    RowMap rm_;
    std::vector<double> d_;
    std::vector<std::pair<int, int>> pushes_;
    std::vector<int> touched_;   // cells moved by apply since the last clear
    bool evict_;
    double until_;
};

}  // namespace

namespace {

// One Stage-1 pipeline: (spreading) -> Abacus -> refinement.
// `until` = end of this pipeline's share of the Stage 1 budget; the
// final rounds (finish) run afterwards, on the better pipeline only.
bool pipeline(const PlaceDB& db, Placement& P, const LegalizeOptions& opt, bool spread, double until, std::string& err,
              bool* bound = nullptr, bool* maxPhase = nullptr, double* abacusSeconds = nullptr, LegalizeStats* info = nullptr) {
    P.seg.assign(db.nodes.size(), -1);
    P.site.assign(db.nodes.size(), 0);
    std::vector<double> tx(db.nodes.size()), ty(db.nodes.size());
    for (int v : db.movable) {
        tx[v] = db.nodes[v].gx;
        ty[v] = db.nodes[v].gy;
    }
    if (spread) Spreader(db, opt.binRows, opt.binCells, opt.spreadRounds).run(tx, ty, opt.verbose);
    const double abacusStart = wallSeconds();
    bool ok = opt.method == "tetris" ? tetris(db, P, err)
                                     : Abacus(db, P, opt.totalCost, tx, ty, opt.verbose, opt.abacusFast, opt.abacusExactWork).run(err);
    if (abacusSeconds) *abacusSeconds = wallSeconds() - abacusStart;
    if (info && opt.collectHwm) info->hwmAbacusKiB = readVmHwmKiB();
    if (!ok) return false;
    if (opt.verbose) {
        DispStats d = displacement(db, P);
        std::fprintf(stderr, "[legal] %-7s total %.1f  max %.1f  hpwl %.6e  %.1fs\n", opt.method.c_str(), d.total, d.max, totalHPWL(db, P), wallSeconds());
    }
    if (opt.refineMax || opt.refineTotal) {
        Refiner ref(db, P, opt.evict, until);
        ref.sweepRows_ = opt.sweepRows;
        ref.sweepSites_ = opt.sweepSites;
        ref.totalTol_ = opt.totalTol;
        ref.swapPush_ = opt.swapPush;
        ref.maxFast_ = opt.maxFast;
        ref.maxThreads_ = std::max(1, opt.maxThreads > 0 ? opt.maxThreads : opt.threads);
        ref.releaseMaxScratch_ = opt.releaseMaxScratch;
        ref.traceMax_ = opt.verbose;
        ref.pruneMax_ = opt.maxPrune;
        ref.tailStop_ = opt.maxTail;
        // First outer boundaries only: reduceMaxByMatching may call reduceMax
        // internally, and later rounds must not replace the initial samples.
        auto sampleMax = [&] {
            if (info && opt.collectHwm && info->hwmMaxKiB == 0) info->hwmMaxKiB = readVmHwmKiB();
        };
        auto sampleMatch = [&] {
            if (info && opt.collectHwm && info->hwmMatchKiB == 0) info->hwmMatchKiB = readVmHwmKiB();
        };
        if (opt.rowOpt) ref.optimizeRows(0, INF, opt.verbose);   // exact L1 per row
        // The total pass frees room that the max pass can use again, so alternate.
        double prevMax = INF;
        // this pipeline's share `until` was set before Abacus; on a design whose Abacus used most
        // of it (L1: Abacus ended at 8.2 s, the share at 7.8 s, so the max pass never ran and the max
        // stayed 92k instead of ~51k) the first max pass may run on its own budget, measured from the
        // end of Abacus: maxShare of the Stage 1 time left (opt.maxUntil). It also stops once the
        // maximum falls by less than maxRate per second, or more than maxTail of the cells lie within
        // 10 % of it. That legacy heuristic can stop an incomplete nonlocal redistribution;
        // complete mode instead keeps the original greedy path until stuck or its measured limits.
        // Both are checked after >= 0.25 s, so a max pass that converges at once (small designs) runs
        // as in . Once cut, no later step lowers the maximum further.
        bool maxCut = false;
        // the budget starts before the round loop: when Abacus itself ran past `until` the
        // loop would not start at all
        const double tA = wallSeconds();
        // complete mode uses the caller's absolute Stage 1 boundary. It does not
        // allocate a fixed fraction to max refinement; measured next-work admission
        // below is only an affordability estimate, not a run-to-stuck prediction.
        const double legacyBudget = opt.maxShare * std::max(0.0, opt.maxUntil - tA);
        const double legacyPhaseUntil = std::max(until, tA + legacyBudget);
        const double budget = opt.maxComplete ? std::max(0.0, opt.maxUntil - tA) : legacyBudget;
        if (opt.refineMax && opt.maxUntil > 0) {
            if (opt.maxComplete) ref.setUntil(opt.maxUntil);
            else if (until - tA < budget) ref.setUntil(tA + budget);
        }
        for (int round = 0; round < 4 && !ref.timeUp(); ++round) {
            bool matchingDone = false;
            if (opt.refineMax && !maxCut) {
                if (round == 0 && opt.maxUntil > 0) {
                    ref.completeMax_ = opt.maxComplete;
                    ref.policyMinSeconds_ = opt.maxPolicyMinSeconds;
                    const double greedyStart = wallSeconds();
                    const std::string why = ref.reduceMax(opt.verbose, opt.maxComplete ? 0 : opt.maxRate,
                                                          opt.maxComplete ? 0 : opt.maxFloor);
                    const double greedyEnd = wallSeconds();
                    ref.policyMinSeconds_ = 0.25;   // only the first greedy policy check is an E1 experiment
                    sampleMax();
                    const double greedySeconds = greedyEnd - greedyStart;
                    const int greedyMoves = ref.lastMoves_;
                    // Before 0.25s, legacy rate/tail checks cannot have fired. If
                    // this greedy pass also fits its legacy deadline and no immediate
                    // floor rule was requested, retain the legacy matching / total /
                    // final-round schedule. Matching's
                    // own duration must not reclassify a quick greedy convergence.
                    // M1 cleanup occurs only after the whole phase, below, so it
                    // does not change the original quick-convergence clock point.
                    const bool legacyQuick = opt.maxComplete && opt.maxFloor <= 0 && why == "stuck" &&
                                             greedySeconds < 0.25 && greedyEnd <= legacyPhaseUntil;
                    const bool completePath = opt.maxComplete && !legacyQuick;
                    if (legacyQuick) {
                        ref.completeMax_ = false;
                        ref.setUntil(until);
                    }
                    std::string matchingWhy = "not-run";
                    const double matchStart = wallSeconds();
                    if (completePath && why == "stuck" && !ref.timeUp() && opt.matching) {
                        matchingWhy = ref.reduceMaxByMatching(opt.verbose);
                        matchingDone = true;
                    }
                    const double matchingSeconds = wallSeconds() - matchStart;
                    if (matchingDone) sampleMatch();
                    if (info) {
                        info->maxMoves = greedyMoves;
                        info->maxSeconds = greedySeconds;
                        info->maxWhy = why;
                        info->matchSeconds = matchingSeconds;
                        info->matchWhy = matchingWhy;
                    }
                    const bool phaseExpired = ref.timeUp();
                    const double phaseSeconds = wallSeconds() - tA;
                    ref.setUntil(until);
                    ref.completeMax_ = false;
                    maxCut = why == "rate" || why == "tail" || why == "floor" ||
                             (completePath && (phaseExpired || why == "deadline" || why == "cost" || why == "forecast" ||
                                               matchingWhy == "deadline" || matchingWhy == "cost" || matchingWhy == "forecast"));
                    // Non-quick complete work (including work that needed more than the
                    // legacy deadline) hands off to main even if it finished successfully.
                    // A legacyQuick pass continues below, where matching runs exactly once
                    // with complete-mode admission disabled and the old deadline restored.
                    if (maxPhase && (maxCut || completePath)) *maxPhase = true;
                    if (opt.verbose)
                        std::fprintf(stderr, "[legal] max pass: budget %.2fs from %.2fs, used %.2fs (%s), matching %s, greedy %.6fs path %s\n",
                                     budget, tA, phaseSeconds, why.c_str(), matchingWhy.c_str(), greedySeconds,
                                     legacyQuick ? "legacy-quick" : completePath ? "complete" : "legacy");
                    if (maxCut || completePath) { // completed sustained/extended work is handed to main
                        if (bound) *bound = true;   // final rounds skipped, as did on L1
                        ref.closeMaxPhase(opt.verbose);
                        return true;
                    }
                } else {
                    ref.reduceMax(opt.verbose);
                    sampleMax();
                }
            }
            if (opt.refineMax && !maxCut && opt.matching && !matchingDone) {
                ref.reduceMaxByMatching(opt.verbose);
                sampleMatch();
            }
            if (opt.refineMax && !maxCut && opt.windowDP) {
                WindowOptions w;
                w.passes = 2000;
                w.perRow = opt.windowCells;
                w.timeLimit = std::max(0.0, until - wallSeconds());
                w.verbose = opt.verbose;
                if (reduceMaxWindows(db, P, w)) {
                    ref.resync();
                    ref.reduceMax(opt.verbose);
                    sampleMax();
                }
            }
            // No later max call follows in the final scheduled round. Earlier
            // rounds retain their buffers because total refinement can reopen it.
            if (round == 3) ref.closeMaxPhase(opt.verbose);
            if (opt.rowOpt) ref.optimizeRows(ref.maxDisp(), ref.maxDisp(), opt.verbose);
            if (opt.matching) ref.matchSweep(opt.verbose);
            // the local-move pass gets at most half of what is left (it is
            // slow on designs with far-displaced cells; the window DP is not)
            if (opt.refineTotal) ref.reduceTotal(opt.verbose, wallSeconds() + 0.5 * (until - wallSeconds()));
            double mx = displacement(db, P).max;
            if (!opt.refineMax || maxCut || mx > prevMax - 1e-9) break;
            prevMax = mx;
        }
        if (bound) *bound = ref.timeUp();
        ref.closeMaxPhase(opt.verbose);   // early convergence / exhausted deadline: this Refiner is done
    }
    return true;
}


// final rounds of exact row re-placement, matching and local moves (the
// local moves leave rows slightly off their L1 optimum).
// + the window DP (two- and three-row DP, swept to convergence) in every round: it moves groups of cells between rows,
// which the one- and two-cell moves cannot, and they in turn reopen windows.
// Rounds stop when they gain too little (see the end of the loop).
void finish(const PlaceDB& db, Placement& P, const LegalizeOptions& opt, WindowMemo* keep) {
    if (!(opt.rowOpt && opt.refineTotal)) return;
    Refiner ref(db, P, opt.evict, opt.until);
    ref.maxFast_ = opt.maxFast;
    ref.maxThreads_ = std::max(1, opt.maxThreads > 0 ? opt.maxThreads : opt.threads);
    ref.releaseMaxScratch_ = opt.releaseMaxScratch;
    ref.sweepRows_ = opt.sweepRows;
    ref.sweepSites_ = opt.sweepSites;
    ref.totalTol_ = opt.totalTol;
    ref.swapPush_ = opt.swapPush;
    // the caller's memo (main's stage1Memo): main's window DP after the pareto matching
    // then re-solves only windows that changed since the last final round. With a local memo
    // it solved every window again (27 cases: 32 s, e01 10,823 windows for no gain).
    WindowMemo local;
    WindowMemo& memo = keep ? *keep : local;
    for (int k = 0; k < opt.finishRounds && !ref.timeUp(); ++k) {
        double before = displacement(db, P).total, t0 = wallSeconds();
        ref.optimizeRows(ref.maxDisp(), ref.maxDisp(), opt.verbose);
        if (opt.matching) ref.matchSweep(opt.verbose);
        ref.reduceTotal(opt.verbose, wallSeconds() + 0.3 * (opt.until - wallSeconds()));
        if (opt.windowDP) {
            WindowOptions w;
            w.passes = 1000;
            w.perRow = opt.windowCells;
            w.perRow3 = opt.windowCells3;
            w.timeLimit = std::max(0.0, opt.until - wallSeconds());
            w.verbose = opt.verbose;
            w.threads = opt.threads;
            windowSweeps(db, P, w, &memo);
            ref.resync();
        }
        if (opt.verbose)
            std::fprintf(stderr, "[legal] final round %d: total %.1f  %.1fs\n", k, displacement(db, P).total, wallSeconds());
        // stop when a round gains < 0.01 %, or < 0.2 % and took over a second
        double after = displacement(db, P).total;
        if (after > before * (1 - 1e-4) || (after > before * (1 - 2e-3) && wallSeconds() - t0 > 1)) break;
    }
    // the final rounds freed room; try the maximum once more (the max pass
    // is path dependent: from this placement it often gets a bit further),
    // then let the DP win back total under the new cap.
    if (opt.refineMax && !ref.timeUp()) {
        double mx = ref.maxDisp();
        ref.reduceMax(opt.verbose);
        if (opt.matching) ref.reduceMaxByMatching(opt.verbose);
        ref.closeMaxPhase(opt.verbose);   // subsequent window DP never calls this Refiner's max search
        if (opt.windowDP) {
            WindowOptions w;
            w.passes = 2000;
            w.perRow = opt.windowCells;
            w.timeLimit = std::max(0.0, opt.until - wallSeconds());
            w.verbose = opt.verbose;
            if (reduceMaxWindows(db, P, w)) ref.resync();
        }
        if (ref.maxDisp() < mx - 1e-9 && opt.windowDP) {
            WindowOptions w;
            w.passes = 1000;
            w.perRow = opt.windowCells;
            w.perRow3 = opt.windowCells3;
            w.timeLimit = std::max(0.0, opt.until - wallSeconds());
            w.verbose = opt.verbose;
            w.threads = opt.threads;
            windowSweeps(db, P, w, &memo);
        }
        if (opt.verbose) {
            DispStats d = displacement(db, P);
            std::fprintf(stderr, "[legal] final max pass: max %.4f -> %.4f, total %.1f  %.1fs\n", mx, d.max, d.total, wallSeconds());
        }
    }
    ref.closeMaxPhase(opt.verbose);
}

}  // namespace

bool legalize(const PlaceDB& db, Placement& P, const LegalizeOptions& opt, std::string& err, WindowMemo* memo,
              LegalizeStats* stats) {
    if (stats) *stats = LegalizeStats();
    double overlap = 0;
    bool canSpread = opt.method != "tetris" && db.uniformSites && opt.spread != "off";
    double now = wallSeconds(), left = std::max(0.0, opt.until - now);
    // both pipelines are compared BEFORE the (expensive) final rounds,
    // which then run once, on the winner. Shares of the Stage 1 budget:
    // 30 % per pipeline, the rest for the final rounds.
    // a pipeline that ran out of its share means this design does not converge in the
    // Stage 1 budget on this machine. The final rounds (exact row re-placement, matching,
    // local moves: 0.004 - 0.26 %/s on the 200k / 1M-cell designs) are then skipped, and their
    // time goes to the pareto matching and the window DP that main runs next (0.1 - 1 %/s;
    //). Decided by what was measured here, not by the design size.
    bool bound = false;
    auto finishOrSkip = [&] {
        if (!bound) {
            LegalizeOptions finishOpt = opt;
            finishOpt.until -= overlap;
            finish(db, P, finishOpt, memo);
            return;
        }
        if (opt.verbose) std::fprintf(stderr, "[legal] refinement handed off: final rounds skipped  %.1fs\n", wallSeconds());
    };
    auto keepInfo = [&](const LegalizeStats& from) {
        if (!stats) return;
        stats->maxMoves = from.maxMoves; stats->maxSeconds = from.maxSeconds; stats->maxWhy = from.maxWhy;
        stats->matchSeconds = from.matchSeconds; stats->matchWhy = from.matchWhy;
    };
    auto keepHwm = [&](const LegalizeStats& from) {
        if (!stats || !opt.collectHwm) return;
        auto merge = [](long& value, long sample) {
            if (sample > 0) value = std::max(value, sample);
            else if (sample < 0 && value == 0) value = -1;
        };
        // VmHWM belongs to this process, including concurrent/unchosen work.
        // Merge after both pipelines join; never add their cumulative peaks.
        merge(stats->hwmAbacusKiB, from.hwmAbacusKiB);
        merge(stats->hwmMaxKiB, from.hwmMaxKiB);
        merge(stats->hwmMatchKiB, from.hwmMatchKiB);
    };
    auto sampleFinish = [&] {
        // finish returns immediately if row/total refinement is disabled.
        if (stats && opt.collectHwm && !bound && opt.rowOpt && opt.refineTotal)
            stats->hwmFinishKiB = readVmHwmKiB();
    };
    if (!canSpread || opt.spread == "on" || db.movable.size() > 60000) {   // one pipeline
        bool maxPhase = false;
        double abacusSeconds = 0;
        LegalizeStats info;
        if (!pipeline(db, P, opt, canSpread && opt.spread == "on", now + 0.5 * left, err, &bound, &maxPhase, &abacusSeconds, &info)) return false;
        keepInfo(info);
        keepHwm(info);
        if (stats) stats->abacusSeconds = abacusSeconds;
        if (stats) stats->maxPhase = maxPhase;
        const double tf = wallSeconds();
        finishOrSkip();
        if (stats) stats->finishSeconds = wallSeconds() - tf;
        sampleFinish();
        return true;
    }
    // auto: run both pipelines and keep the better one, total and maximum
    // displacement weighted equally (each relative to the run without spreading).
    Placement Q;
    std::string err2;
    bool bound2 = false, ok2 = false, maxPhase1 = false, maxPhase2 = false;
    double abacus1 = 0, abacus2 = 0;
    LegalizeStats info1, info2;
    if (opt.parallelPipelines && opt.threads >= 2) {
        // Only the immutable DB/options are shared. Each pipeline owns its
        // placement, Refiner, errors and deadline; its window pass is serial.
        double spreadStart = 0, spreadEnd = 0;
        auto spread = std::async(std::launch::async, [&] {
            spreadStart = wallSeconds();
            bool ok = pipeline(db, Q, opt, true, spreadStart + 0.3 * left, err2, &bound2, &maxPhase2, &abacus2, &info2);
            spreadEnd = wallSeconds();
            return ok;
        });
        double plainStart = wallSeconds();
        bool ok = pipeline(db, P, opt, false, now + 0.3 * left, err, &bound, &maxPhase1, &abacus1, &info1);
        double plainEnd = wallSeconds();
        // Join even when plain fails (also on exception via future destruction).
        // Plain failure takes precedence, as in the sequential implementation.
        if (!ok) { spread.wait(); return false; }
        ok2 = spread.get();  // joins and propagates worker exceptions to main's guard
        overlap = std::max(0.0, std::min(plainEnd, spreadEnd) - std::max(plainStart, spreadStart));
        if (stats) {
            stats->pipelineOverlap = overlap;
            stats->parallelPipelines = true;
        }
        if (opt.verbose)
            std::fprintf(stderr, "[legal] parallel pipelines: overlap %.6fs (removed from later Stage 1 deadlines)\n", overlap);
    } else {
        if (!pipeline(db, P, opt, false, now + 0.3 * left, err, &bound, &maxPhase1, &abacus1, &info1)) return false;
        ok2 = pipeline(db, Q, opt, true, wallSeconds() + 0.3 * left, err2, &bound2, &maxPhase2, &abacus2, &info2);
    }
    keepHwm(info1);
    keepHwm(info2);
    if (ok2) {
        bound = bound || bound2;
        DispStats a = displacement(db, P), b = displacement(db, Q);
        double score = b.total / std::max(1e-9, a.total) + b.max / std::max(1e-9, a.max);
        if (stats) {
            stats->plainTotal = a.total; stats->plainMax = a.max; stats->spreadTotal = b.total; stats->spreadMax = b.max;
            stats->score = score;
        }
        // the choice is made before the final rounds. Within tieBand of a tie (ibm05: 2.0029
        // plain, 1.9959 spread, the plain pipeline 0.9 % worse in from timing alone) both run
        // their final rounds, at the same time and to the same deadline, and the same score decides
        // on the finished placements.
        if (!bound && opt.tieBand > 0 && std::fabs(score - 2) < opt.tieBand) {
            if (opt.verbose)
                std::fprintf(stderr, "[legal] plain: total %.1f max %.1f | spread: total %.1f max %.1f -> score %.6f, tie: both finish\n",
                             a.total, a.max, b.total, b.max, score);
            LegalizeOptions finishOpt = opt;
            finishOpt.until -= overlap;   // each keeps its thread count: window DP results depend on it
            WindowMemo memoQ;
            const double tf = wallSeconds();
            auto other = std::async(std::launch::async, [&] { finish(db, Q, finishOpt, &memoQ); });
            WindowMemo memoP;
            finish(db, P, finishOpt, &memoP);
            other.get();
            const DispStats fa = displacement(db, P), fb = displacement(db, Q);
            const double finalScore = fb.total / std::max(1e-9, fa.total) + fb.max / std::max(1e-9, fa.max);
            if (opt.verbose)
                std::fprintf(stderr, "[legal] finished plain: total %.1f max %.1f | spread: total %.1f max %.1f -> score %.6f -> %s\n",
                             fa.total, fa.max, fb.total, fb.max, finalScore, finalScore < 2 ? "spread" : "plain");
            if (finalScore < 2) { P = std::move(Q); memoP = std::move(memoQ); }
            if (memo) *memo = std::move(memoP);
            if (stats) {
                stats->tieFinish = true;
                stats->finalScore = finalScore;
                stats->choice = finalScore < 2 ? "spread" : "plain";
                stats->maxPhase = finalScore < 2 ? maxPhase2 : maxPhase1;
                stats->abacusSeconds = finalScore < 2 ? abacus2 : abacus1;
                stats->finishSeconds = wallSeconds() - tf;
            }
            keepInfo(finalScore < 2 ? info2 : info1);
            sampleFinish();
            return true;
        }
        if (opt.verbose)
            std::fprintf(stderr, "[legal] plain: total %.1f max %.1f | spread: total %.1f max %.1f -> %s\n", a.total, a.max,
                         b.total, b.max, score < 2 ? "spread" : "plain");
        if (score < 2) P = Q;
        if (stats) {
            stats->maxPhase = score < 2 ? maxPhase2 : maxPhase1; stats->abacusSeconds = score < 2 ? abacus2 : abacus1;
            stats->choice = score < 2 ? "spread" : "plain";
        }
        keepInfo(score < 2 ? info2 : info1);
    } else {
        if (stats) { stats->maxPhase = maxPhase1; stats->abacusSeconds = abacus1; stats->choice = "plain(spread failed)"; }
        keepInfo(info1);
    }
    const double tf = wallSeconds();
    finishOrSkip();
    if (stats) stats->finishSeconds = wallSeconds() - tf;
    sampleFinish();
    return true;
}

// (--abacus-check, measurement only): Abacus alone in modes 0, 1 and 2 on the same targets
// (no spreading, as in the large-design pipeline), timed one after another; the placements must be
// identical cell for cell. Returns false if they are not.
bool abacusCheck(const PlaceDB& db, const LegalizeOptions& opt) {
    std::vector<double> tx(db.nodes.size()), ty(db.nodes.size());
    for (int v : db.movable) {
        tx[v] = db.nodes[v].gx;
        ty[v] = db.nodes[v].gy;
    }
    Placement ref;
    bool same = true;
    for (int mode : {0, 1, 2, 3}) {
        Placement P;
        P.seg.assign(db.nodes.size(), -1);
        P.site.assign(db.nodes.size(), 0);
        std::string err;
        const double t0 = wallSeconds();
        Abacus a(db, P, opt.totalCost, tx, ty, true, mode, opt.abacusExactWork);
        if (!a.run(err)) { std::fprintf(stderr, "[abacus-check] mode %d failed: %s\n", mode, err.c_str()); return false; }
        const double t = wallSeconds() - t0;
        unsigned long long h = 1469598103934665603ULL;
        for (int v : db.movable) h = (h ^ (unsigned long long)(P.seg[v] * 1000003LL + P.site[v])) * 1099511628211ULL;
        bool eq = mode == 0 || (P.seg == ref.seg && P.site == ref.site);
        same = same && eq;
        const DispStats d = displacement(db, P);
        std::fprintf(stderr, "[abacus-check] mode %d: %.3fs  work %.6e  total %.6f  max %.6f  hash %016llx  %s\n", mode, t,
                     a.work(), d.total, d.max, h, mode == 0 ? "reference" : eq ? "IDENTICAL" : "DIFFERENT");
        if (mode == 0) ref = std::move(P);
    }
    return same;
}

// fixed-work diagnostic. Each accepted move is compared exactly, not just by final metrics.
bool maxRefineCheck(const PlaceDB& db, const LegalizeOptions& opt) {
    LegalizeOptions initial = opt;
    initial.refineMax = initial.refineTotal = false;
    initial.spread = "off";
    Placement start;
    std::string err;
    if (!legalize(db, start, initial, err)) return false;
    if (opt.rowOpt) {
        Refiner row(db, start, opt.evict, 1e30);
        row.optimizeRows(0, INF, false);
    }
    std::vector<std::vector<int>> reference;
    Placement referenceFinal;
    const int parallelThreads = std::max(2, opt.threads);
    std::vector<std::pair<int, int>> configurations{{0, 1}, {1, 1}, {1, parallelThreads}, {2, 1}, {2, parallelThreads},
                                                    {3, 1}, {3, parallelThreads}, {4, 1}, {4, parallelThreads},
                                                    {5, 1}, {5, parallelThreads}, {6, 1}, {6, parallelThreads},
                                                    {7, 1}, {7, parallelThreads}};
    if (!opt.maxCheckModes.empty()) configurations = opt.maxCheckModes;
    for (size_t mode = 0; mode < configurations.size(); ++mode) {
        Placement p = start;
        Placement observed = start;
        Refiner ref(db, p, opt.evict, 1e30);
        ref.pruneMax_ = opt.maxPrune;
        ref.swapPush_ = opt.swapPush;
        ref.maxFast_ = configurations[mode].first;
        ref.maxThreads_ = configurations[mode].second;
        // With --max-release, the first mode remains an unreleased reference;
        // subsequent modes retain nested reuse and release when both phases end.
        ref.releaseMaxScratch_ = opt.releaseMaxScratch && mode != 0;
        ref.moveLimit_ = opt.maxCheckMoves;
        size_t move = 0;
        bool equal = true;
        ref.moveObserver_ = [&](int c, const Placement& current, const std::vector<int>& touched) {
            auto ids = touched;
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            std::vector<int> record{c};
            for (int v : ids) if (observed.seg[v] != current.seg[v] || observed.site[v] != current.site[v]) {
                record.push_back(v); record.push_back(current.seg[v]); record.push_back(current.site[v]);
                observed.seg[v] = current.seg[v]; observed.site[v] = current.site[v];
            }
            if (mode == 0) reference.push_back(std::move(record));
            else if (move >= reference.size() || record != reference[move]) equal = false;
            ++move;
        };
        const double t = wallSeconds();
        const char* why = ref.reduceMax(opt.verbose);
        if (ref.releaseMaxScratch_)
            equal = equal && ref.maxReleaseCount_ == 0 && !ref.maxScratchEmpty();
        equal = equal && (mode == 0 || move == reference.size());
        // (--max-check-matching): then the matching phase of the complete path, compared as a whole
        double tm = 0;
        if (opt.maxCheckMatching) {
            ref.moveObserver_ = nullptr;
            ref.moveLimit_ = 0;
            // Diagnostic fixed work: nested greedy passes must not hit the
            // complete-mode elapsed/per-move cost stop under slow tools (TSan).
            // Keep the matching work/round limits; production scheduling is unchanged.
            ref.completeMax_ = false;
            const double t1 = wallSeconds();
            ref.reduceMaxByMatching(opt.verbose);
            tm = wallSeconds() - t1;
        }
        equal = equal && (mode == 0 || (p.seg == referenceFinal.seg && p.site == referenceFinal.site));
        if (ref.releaseMaxScratch_) {
            // Nested matching must not free/reallocate these buffers. Close once
            // only when the complete diagnostic phase has no more max consumer.
            equal = equal && ref.maxReleaseCount_ == 0 && !ref.maxScratchEmpty();
            ref.closeMaxPhase(opt.verbose);
            equal = equal && ref.maxReleaseCount_ == 1 && ref.maxScratchEmpty();
            ref.closeMaxPhase(opt.verbose);   // closing a phase twice is harmless
            equal = equal && ref.maxReleaseCount_ == 1 && ref.maxScratchEmpty();
        }
        std::fprintf(stderr, "[max-check] mode %d threads %d prune %d: %zu moves %.6fs (%s)%s, %s\n", ref.maxFast_,
                     ref.maxThreads_, int(opt.maxPrune), move, wallSeconds() - t - tm, why,
                     opt.maxCheckMatching ? (" + matching " + std::to_string(tm) + "s").c_str() : "",
                     mode == 0 ? "reference" : equal ? "IDENTICAL EACH MOVE" : "DIFFERENT");
        if (!equal || !checkLegal(db, p).empty()) return false;
        if (mode == 0) referenceFinal = std::move(p);
    }
    return true;
}

bool maxEvictRoomCheck(const PlaceDB& db, const Placement& placement, int* direct, int* fallback) {
    if (!checkLegal(db, placement).empty()) return false;
    Placement p = placement;
    RowMap rm(db, p);
    int directCount = 0, fallbackCount = 0;
    for (int s = 0; s < int(db.segs.size()); ++s) {
        const auto& cells = rm.cells(s);
        for (auto it = cells.begin(); it != cells.end(); ++it) {
            bool usedDirect;
            const int actual = evictionRoomWidth(db, p, rm, s, it, true, usedDirect);
            if (actual != rm.freeAround(*it).width()) return false;
            if (usedDirect) ++directCount;
            else ++fallbackCount;
        }
    }
    if (direct) *direct = directCount;
    if (fallback) *fallback = fallbackCount;
    return true;
}
