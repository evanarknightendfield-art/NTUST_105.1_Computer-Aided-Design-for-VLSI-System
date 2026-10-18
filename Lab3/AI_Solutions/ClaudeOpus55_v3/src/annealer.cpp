// annealer.cpp — Stage 2 (v3).
//
// State: a legal placement. Every move keeps it legal, so the cost is just HPWL.
// Moves (TimberWolf M1/M2 adapted to rows), for cells on at least one net (v2):
//   * pick a cell a and a target point: inside a's optimal region (median of
//     its nets' bounding boxes) or a random offset within the range window;
//   * target is free space that fits a  -> move a there
//   * target is another cell b          -> swap a and b if each fits in the
//                                          other's room (neighbours in the same
//                                          row just exchange order)
//   * (v2) neither fits -> try the nearest cells and gaps of that row instead
//     (ibm01: infeasible proposals 49% -> 10%; only in dense designs, see near_)
//   * (v2) local reordering [FastPlace-DP]: 3 neighbours trade order in their span
//   * (v3) in the colder half: linear placement of a 12-cell window [Kahng et al.,
//     ASPDAC'99, lecture L07] and independent set matching of 12 same-width cells
//     [NTUplace3 / ABCDPlace] — both exact for their sub-problem, never worse
//   * (v3) cached net bounding boxes; moves = 5000 per cell, kept in [5e6, 6e7]
// Range limiter (VPR style): the window grows/shrinks to keep acceptance near 44%.
// Schedule: T0 from sampled uphill moves, geometric cooling to T0 * tEndRatio.
// The best placement is recovered from an undo journal, without full copies.
#include "annealer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>

#include "rowmap.hpp"

namespace {

struct Rng {   // xoshiro256** — fast and good enough for SA
    uint64_t s[4];
    explicit Rng(uint64_t seed) {
        for (auto& x : s) {   // splitmix64 seeding
            seed += 0x9e3779b97f4a7c15ULL;
            uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            x = z ^ (z >> 31);
        }
    }
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t next() {
        uint64_t r = rotl(s[1] * 5, 7) * 9, t = s[1] << 17;
        s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
        s[2] ^= t; s[3] = rotl(s[3], 45);
        return r;
    }
    double u01() { return (next() >> 11) * 0x1.0p-53; }
    double uniform(double a, double b) { return a + (b - a) * u01(); }
    int below(int n) { return (int)((next() >> 33) % (uint64_t)n); }
};

// Compressed adjacency lists (one array per relation) — far fewer cache misses
// than vector<vector<int>> in the inner loop.
struct Csr {
    std::vector<int> start{0}, item;
    void add(const std::vector<int>& list) {
        item.insert(item.end(), list.begin(), list.end());
        start.push_back((int)item.size());
    }
    const int* begin(int i) const { return item.data() + start[i]; }
    const int* end(int i) const { return item.data() + start[i + 1]; }
};

struct Pt { double x, y; };
struct Box { double xl, xh, yl, yh; };   // v3: cached bounding box of a net's pin centers

// Min-cost perfect assignment (Hungarian algorithm with potentials, O(n^3)).
std::vector<int> hungarian(const std::vector<double>& cost, int n) {
    const double INF = 1e300;
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

constexpr int kMaxMove = 32;

struct Move {
    enum Kind { kMove, kSwap, kReorder, kRowOpt, kIsm };
    Kind kind = kMove;
    int n = 0;                    // cells moved: 1 (move), 2 (swap), 3 (reorder), up to kMaxMove (v3)
    int v[kMaxMove], seg[kMaxMove], site[kMaxMove];   // new positions
    bool reorder = false;         // kMove that leaves its own gap -> remove + insert
    int seg0 = -1, idx0 = 0;      // kReorder: segment and list index of the first of the 3 cells
};

class Annealer {
public:
    Annealer(const PlaceDB& db, Placement& P, const SAOptions& opt)
        : db_(db), P_(P), opt_(opt), rm_(db, P), rng_(opt.seed) {
        int N = (int)db.nodes.size();
        c_.resize(N);
        for (int v = 0; v < N; ++v) c_[v] = {P.x(db, v) + db.nodes[v].w / 2, P.y(db, v) + db.nodes[v].h / 2};
        for (const auto& net : db.nets) pins_.add(net);
        for (int v = 0; v < N; ++v) {   // nets with < 2 pins never change HPWL
            std::vector<int> keep;
            for (int n : db.nodeNets[v])
                if (db.nets[n].size() >= 2) keep.push_back(n);
            nets_.add(keep);
        }
        for (int v : db.movable)
            if (nets_.end(v) != nets_.begin(v)) active_.push_back(v);
        if (active_.empty()) active_ = db.movable;
        // Dense designs block most targeted moves and the fallback rescues them;
        // in sparse ones targets are usually free and the fallback only adds
        // weaker moves (measured: helps at utilization >= 0.74, hurts <= 0.70).
        near_ = opt.nearTries >= 0 ? opt.nearTries : db.utilization() >= 0.72 ? 3 : 0;
        netH_.resize(db.nets.size());
        netStamp_.assign(db.nets.size(), 0);
        cellEpoch_.assign(N, -1);
        winStamp_.assign(N, 0);
        winIdx_.assign(N, 0);
        terms_.resize(kMaxMove);
        cur_ = 0;
        box_.resize(db.nets.size());
        netCnt_.assign(db.nets.size(), 0);
        netMover_.assign(db.nets.size(), 0);
        for (size_t n = 0; n < db.nets.size(); ++n) {
            box_[n] = netBox((int)n);
            cur_ += netH_[n] = span(box_[n]);
        }

        // chip extent and row height for the range window
        xLo_ = 1e300; xHi_ = -1e300;
        for (const Segment& s : db.segs) {
            xLo_ = std::min(xLo_, s.x0);
            xHi_ = std::max(xHi_, s.x0 + s.numSites * s.sp);
        }
        yLo_ = db.rows.front().y;
        yHi_ = db.rows.back().y + db.rows.back().height;
        rowH_ = db.rows.front().height;
        double avgW = 0;
        for (int v : db.movable) avgW += db.nodes[v].w;
        avgW /= std::max<size_t>(1, db.movable.size());
        minWinX_ = std::min(xHi_ - xLo_, opt.minWinCells * avgW);
        minWinY_ = std::min(yHi_ - yLo_, opt.minWinRows * rowH_);
    }

    SAResult run() {
        auto t0 = std::chrono::steady_clock::now();
        auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
        SAResult res;
        res.initial = best_ = cur_;
        if (db_.movable.empty()) return res;

        winX_ = std::max(minWinX_, opt_.win0 * (xHi_ - xLo_));
        winY_ = std::max(minWinY_, opt_.win0 * (yHi_ - yLo_));
        // v2: the window never grows past maxWin of the chip; with few failed
        // proposals the acceptance rate is high early on and VPR's rule would
        // otherwise blow the window up to half the chip (long, useless moves).
        maxWinX_ = std::max(winX_, opt_.maxWin * (xHi_ - xLo_));
        maxWinY_ = std::max(winY_, opt_.maxWin * (yHi_ - yLo_));
        double T0 = initialTemperature();
        double Tend = T0 * opt_.tEndRatio;
        int steps = std::max(1, opt_.steps);
        double alpha = std::pow(Tend / T0, 1.0 / steps);
        long long total = (long long)std::max(opt_.minMoves, std::min(opt_.maxMoves, opt_.movesPerCell * db_.movable.size()));
        long long perStep = std::max(1LL, total / steps);
        long long traceEvery = std::max(1LL, total / 2000);

        std::ofstream trace;
        if (!opt_.tracePath.empty()) {
            trace.open(opt_.tracePath);
            trace << "iteration,current,best,temperature,window_x,window_y,accept_rate\n";
            trace << 0 << ',' << cur_ << ',' << best_ << ',' << T0 << ',' << winX_ << ',' << winY_ << ",1\n";
        }
        if (opt_.verbose)
            std::fprintf(stderr, "[sa] HPWL %.0f  T0 %.4g  alpha %.4f  %lld moves in %d steps\n", cur_, T0, alpha,
                         total, steps);

        double T = T0;
        long long it = 0, accTrace = 0, attTrace = 0;
        bool timeUp = false;
        for (int step = 0; step < steps && !timeUp; ++step) {
            long long acc = 0, k = 0;
            polish_ = step >= opt_.polishFrom * steps;
            for (; k < perStep; ++k, ++it) {
                Move m;
                if (propose(m)) {
                    int kind = m.kind == Move::kMove ? (m.reorder ? 1 : 0) : m.kind == Move::kSwap ? 2
                             : m.kind == Move::kReorder ? 3 : m.kind == Move::kRowOpt ? 4 : 5;
                    ++stat_[kind][0];
                    double d = delta(m);
                    if (d <= 0 || rng_.u01() < std::exp(-d / T)) {
                        commit(m, d);
                        ++acc;
                        ++stat_[kind][1];
                        if (d < 0) ++stat_[kind][2];
                    } else {
                        revert(m);
                    }
                } else {
                    ++failed_;
                }
                if (trace.is_open() && (it + 1) % traceEvery == 0) {
                    long long a = res.accepted + acc;
                    trace << it + 1 << ',' << cur_ << ',' << best_ << ',' << T << ',' << winX_ << ',' << winY_
                          << ',' << (double)(a - accTrace) / (it + 1 - attTrace) << '\n';
                    accTrace = a;
                    attTrace = it + 1;
                }
                if ((k & 1023) == 0 && elapsed() > opt_.timeLimit) { timeUp = true; ++it; ++k; break; }
            }
            res.accepted += acc;
            double rate = (double)acc / std::max(1LL, k);
            // Too slow for the time limit? Use fewer moves per step but keep the
            // whole temperature range, so the run still ends cold.
            int left = steps - step - 1;
            double secs = elapsed();
            if (left > 0 && secs > 0) {
                double movesLeft = (opt_.timeLimit - secs) * (it / secs);
                perStep = std::max(1LL, std::min(perStep, (long long)(movesLeft / left)));
            }
            // range limiter: keep the acceptance rate near 0.44 [Betz & Rose, VPR]
            double f = std::min(2.0, std::max(0.5, 1 - 0.44 + rate));
            winX_ = std::min(maxWinX_, std::max(minWinX_, winX_ * f));
            winY_ = std::min(maxWinY_, std::max(minWinY_, winY_ * f));
            resum();
            if (opt_.verbose && (step % 20 == 0 || step == steps - 1))
                std::fprintf(stderr, "[sa] step %3d  T %.4g  acc %.3f  win %.0f x %.0f  cur %.0f  best %.0f  %.1fs\n",
                             step, T, rate, winX_, winY_, cur_, best_, elapsed());
            T *= alpha;
        }
        if (timeUp && opt_.verbose) std::fprintf(stderr, "[sa] time limit reached\n");
        if (opt_.verbose) {
            const char* names[6] = {"slide", "move", "swap", "reorder", "rowopt", "ism"};
            std::fprintf(stderr, "[sa] proposals failed %.1f%%\n", 100.0 * failed_ / std::max(1LL, it));
            for (int k = 0; k < 6; ++k)
                std::fprintf(stderr, "[sa] %-8s %5.1f%% of attempts, accepted %5.1f%%, improving %5.1f%%\n", names[k],
                             100.0 * stat_[k][0] / std::max(1LL, it), 100.0 * stat_[k][1] / std::max(1LL, stat_[k][0]),
                             100.0 * stat_[k][2] / std::max(1LL, stat_[k][0]));
        }

        restoreBest();
        res.attempts = it;
        res.best = best_;
        res.seconds = elapsed();
        if (trace.is_open()) trace << it << ',' << cur_ << ',' << best_ << ',' << T << ',' << winX_ << ',' << winY_ << ",0\n";
        return res;
    }

private:
    // ---------------------------------------------------------- cost
    Box netBox(int n) const {
        const int *p = pins_.begin(n), *e = pins_.end(n);
        if (e - p < 2) return {0, 0, 0, 0};
        Box b{c_[*p].x, c_[*p].x, c_[*p].y, c_[*p].y};
        for (++p; p != e; ++p) {
            const Pt& q = c_[*p];
            if (q.x < b.xl) b.xl = q.x; else if (q.x > b.xh) b.xh = q.x;
            if (q.y < b.yl) b.yl = q.y; else if (q.y > b.yh) b.yh = q.y;
        }
        return b;
    }
    static double span(const Box& b) { return (b.xh - b.xl) + (b.yh - b.yl); }

    void resum() {
        cur_ = 0;
        for (double h : netH_) cur_ += h;
    }

    // Puts the moved cells at their new centers and returns the HPWL change.
    // v3: a net moved by one cell whose old center was strictly inside the
    // net's cached box keeps that box, stretched to the new center: O(1)
    // instead of a scan over all pins.
    double delta(const Move& m) {
        ++stampId_;
        touched_.clear();
        touchedNets_.clear();
        for (int i = 0; i < m.n; ++i) {
            int v = m.v[i];
            const Segment& s = db_.segs[m.seg[i]];
            old_[i] = c_[v];
            c_[v] = {s.x0 + m.site[i] * s.sp + db_.nodes[v].w / 2, s.y + db_.nodes[v].h / 2};
            for (const int* it = nets_.begin(v); it != nets_.end(v); ++it) {
                int n = *it;
                if (netStamp_[n] != stampId_) {
                    netStamp_[n] = stampId_;
                    netCnt_[n] = 0;
                    touchedNets_.push_back(n);
                }
                ++netCnt_[n];
                netMover_[n] = i;
            }
        }
        double d = 0;
        for (int n : touchedNets_) {
            Box b;
            const Box& o = box_[n];
            const Pt* old = netCnt_[n] == 1 ? &old_[netMover_[n]] : nullptr;
            if (old && old->x > o.xl && old->x < o.xh && old->y > o.yl && old->y < o.yh) {
                const Pt& q = c_[m.v[netMover_[n]]];
                b = {std::min(o.xl, q.x), std::max(o.xh, q.x), std::min(o.yl, q.y), std::max(o.yh, q.y)};
            } else {
                b = netBox(n);
            }
            double h = span(b);
            touched_.push_back({n, h, b});
            d += h - netH_[n];
        }
        return d;
    }

    void revert(const Move& m) {
        for (int i = 0; i < m.n; ++i) c_[m.v[i]] = old_[i];
    }

    void commit(const Move& m, double d) {
        for (const Touch& t : touched_) {
            netH_[t.n] = t.h;
            box_[t.n] = t.b;
        }
        cur_ += d;
        for (int i = 0; i < m.n; ++i) {   // undo journal: first change since the best was seen
            int v = m.v[i];
            if (cellEpoch_[v] != epoch_) {
                cellEpoch_[v] = epoch_;
                journal_.push_back({v, P_.seg[v], P_.site[v]});
            }
        }
        if (m.kind == Move::kReorder) {
            for (int i = 0; i < m.n; ++i) rm_.slide(m.v[i], m.site[i]);
            rm_.resort(m.seg0, m.idx0, m.n);
        } else if (m.kind == Move::kRowOpt) {   // order unchanged
            for (int i = 0; i < m.n; ++i) rm_.slide(m.v[i], m.site[i]);
        } else if (m.kind == Move::kIsm) {      // same-width cells trade slots
            for (int i = 0; i < m.n; ++i) rm_.remove(m.v[i]);
            for (int i = 0; i < m.n; ++i) rm_.insert(m.v[i], m.seg[i], m.site[i]);
        } else if (m.n == 1) {
            if (m.reorder) {
                rm_.remove(m.v[0]);
                rm_.insert(m.v[0], m.seg[0], m.site[0]);
            } else {
                rm_.slide(m.v[0], m.site[0]);
            }
        } else {
            rm_.exchange(m.v[0], m.v[1], m.seg[0], m.site[0], m.seg[1], m.site[1]);
        }
        if (cur_ < best_ - 1e-6) {
            best_ = cur_;
            ++epoch_;
            journal_.clear();
        }
    }

    void restoreBest() {
        if (cur_ <= best_ + 1e-6) return;
        for (auto it = journal_.rbegin(); it != journal_.rend(); ++it) {
            P_.seg[it->v] = it->seg;
            P_.site[it->v] = it->site;
        }
        rm_.build();
        for (int v : db_.movable) c_[v] = {P_.x(db_, v) + db_.nodes[v].w / 2, P_.y(db_, v) + db_.nodes[v].h / 2};
        for (size_t n = 0; n < db_.nets.size(); ++n) {
            box_[n] = netBox((int)n);
            netH_[n] = span(box_[n]);
        }
        resum();
        best_ = std::min(best_, cur_);
    }

    // ---------------------------------------------------------- moves
    // Optimal region of v: the median interval of its nets' bounding boxes
    // (computed without v). Any point inside minimizes v's own HPWL.
    bool optimalRegion(int v, double& xl, double& xh, double& yl, double& yh) {
        xs_.clear();
        ys_.clear();
        const Pt& cv = c_[v];
        for (const int* it = nets_.begin(v); it != nets_.end(v); ++it) {
            const Box& o = box_[*it];
            // v3: if v is strictly inside the net's box (per axis), the box without v is the same
            bool inX = cv.x > o.xl && cv.x < o.xh, inY = cv.y > o.yl && cv.y < o.yh;
            double bxl = o.xl, bxh = o.xh, byl = o.yl, byh = o.yh;
            if (!inX || !inY) {
                double xl2 = 1e300, xh2 = -1e300, yl2 = 1e300, yh2 = -1e300;
                for (const int* p = pins_.begin(*it); p != pins_.end(*it); ++p) {
                    if (*p == v) continue;
                    const Pt& q = c_[*p];
                    xl2 = std::min(xl2, q.x); xh2 = std::max(xh2, q.x);
                    yl2 = std::min(yl2, q.y); yh2 = std::max(yh2, q.y);
                }
                if (!inX) { bxl = xl2; bxh = xh2; }
                if (!inY) { byl = yl2; byh = yh2; }
            }
            xs_.push_back(bxl); xs_.push_back(bxh);
            ys_.push_back(byl); ys_.push_back(byh);
        }
        if (xs_.empty()) return false;
        size_t k = xs_.size() / 2;
        std::nth_element(xs_.begin(), xs_.begin() + k, xs_.end());
        xh = xs_[k];
        xl = *std::max_element(xs_.begin(), xs_.begin() + k);
        std::nth_element(ys_.begin(), ys_.begin() + k, ys_.end());
        yh = ys_[k];
        yl = *std::max_element(ys_.begin(), ys_.begin() + k);
        return true;
    }

    int segmentAt(int r, double x) const {
        const auto& ss = db_.rows[r].segs;
        int best = ss[0];
        double bd = 1e300;
        for (int s : ss) {
            const Segment& g = db_.segs[s];
            double d = x < g.x0 ? g.x0 - x : x > g.x0 + g.numSites * g.sp ? x - g.x0 - g.numSites * g.sp : 0;
            if (d < bd) { bd = d; best = s; }
        }
        return best;
    }

    // v2: local reordering (FastPlace-DP): three neighbours in a row trade
    // order inside their span; the gaps between them stay where they were.
    bool proposeReorder(Move& m, int a) {
        int s = P_.seg[a];
        const auto& cl = rm_.cells(s);
        int k = (int)cl.size();
        if (k < 3) return false;
        int i = rm_.indexOf(a);
        int first = std::min(std::max(0, i - (int)rng_.below(3)), k - 3);
        const Segment& sg = db_.segs[s];
        int c[3], w[3];
        for (int j = 0; j < 3; ++j) {
            c[j] = cl[first + j];
            w[j] = db_.sitesOf(c[j], sg);
        }
        int g1 = P_.site[c[1]] - (P_.site[c[0]] + w[0]);
        int g2 = P_.site[c[2]] - (P_.site[c[1]] + w[1]);
        static const int perms[5][3] = {{0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
        const int* p = perms[rng_.below(5)];
        int pos = P_.site[c[0]];
        m.kind = Move::kReorder;
        m.n = 3;
        m.seg0 = s;
        m.idx0 = first;
        for (int j = 0; j < 3; ++j) {
            m.v[j] = c[p[j]];
            m.seg[j] = s;
            m.site[j] = pos;
            pos += w[p[j]] + (j == 0 ? g1 : j == 1 ? g2 : 0);
        }
        return true;
    }

    // v3: linear placement [Kahng et al., ASPDAC'99; lecture L07]: a window of
    // consecutive cells of one row moves to the HPWL-optimal x positions with
    // their order kept and everything else fixed. With the order fixed only
    // the leftmost / rightmost window cell of each net matters, so each cell's
    // cost is convex piecewise linear in its x; clumping (pool-adjacent-
    // violators) gives the exact optimum. Never increases HPWL.
    bool proposeRowOpt(Move& m, int a) {
        int s = P_.seg[a];
        const Segment& sg = db_.segs[s];
        const auto& cl = rm_.cells(s);
        int k = (int)cl.size(), L = std::min(opt_.rowWin, kMaxMove);
        if (k < 2) return false;
        int i = rm_.indexOf(a);
        int first = std::max(0, std::min(i - L / 2, k - L)), last = std::min(k - 1, first + L - 1);
        int lo = first > 0 ? P_.site[cl[first - 1]] + db_.sitesOf(cl[first - 1], sg) : 0;
        int hi = last < k - 1 ? P_.site[cl[last + 1]] : sg.numSites;
        int cnt = last - first + 1;
        ++winStampId_;
        for (int j = 0; j < cnt; ++j) {
            winStamp_[cl[first + j]] = winStampId_;
            winIdx_[cl[first + j]] = j;
            terms_[j].clear();
        }
        // cost terms from every net touching the window
        ++stampId_;
        for (int j = 0; j < cnt; ++j)
            for (const int* it = nets_.begin(cl[first + j]); it != nets_.end(cl[first + j]); ++it) {
                int n = *it;
                if (netStamp_[n] == stampId_) continue;
                netStamp_[n] = stampId_;
                double fl = 1e300, fr = -1e300;
                int jl = cnt, jr = -1;
                for (const int* p = pins_.begin(n); p != pins_.end(n); ++p) {
                    if (winStamp_[*p] == winStampId_) {
                        jl = std::min(jl, winIdx_[*p]);
                        jr = std::max(jr, winIdx_[*p]);
                    } else {
                        fl = std::min(fl, c_[*p].x);
                        fr = std::max(fr, c_[*p].x);
                    }
                }
                if (fr >= fl) {                       // HPWL_x = const + max(0, fl - c_jl) + max(0, c_jr - fr)
                    terms_[jl].push_back({0, fl});
                    terms_[jr].push_back({1, fr});
                } else {                              // all pins in the window: c_jr - c_jl
                    terms_[jl].push_back({2, 0});
                    terms_[jr].push_back({3, 0});
                }
            }
        std::vector<int>& w = rwW_;
        w.resize(cnt);
        for (int j = 0; j < cnt; ++j) w[j] = db_.sitesOf(cl[first + j], sg);
        auto cellCost = [&](int j, int x) {
            double cx = sg.x0 + x * sg.sp + db_.nodes[cl[first + j]].w / 2, f = 0;
            for (const Term& t : terms_[j])
                f += t.type == 0 ? std::max(0.0, t.v - cx) : t.type == 1 ? std::max(0.0, cx - t.v)
                   : t.type == 2 ? -cx : cx;
            return f;
        };
        struct Cl { int first, last, W, lo, hi, x; };
        auto F = [&](const Cl& c, int x) {
            double f = 0;
            for (int j = c.first; j <= c.last; ++j) {
                f += cellCost(j, x);
                x += w[j];
            }
            return f;
        };
        auto argmin = [&](Cl& c) {
            int A = c.lo, B = c.hi;
            while (A < B) {
                int mid = A + (B - A) / 2;
                if (F(c, mid + 1) - F(c, mid) >= 0) B = mid;
                else A = mid + 1;
            }
            c.x = A;
        };
        std::vector<Cl> st;
        int prefix = 0, total = 0;
        for (int j = 0; j < cnt; ++j) total += w[j];
        if (hi - lo < total) return false;
        for (int j = 0; j < cnt; ++j) {
            // cell j must leave room for the cells before / after it inside [lo, hi)
            Cl c{j, j, w[j], lo + prefix, hi - (total - prefix), 0};
            prefix += w[j];
            argmin(c);
            while (!st.empty() && st.back().x + st.back().W > c.x) {
                const Cl& p = st.back();
                Cl mg{p.first, c.last, p.W + c.W, std::max(p.lo, c.lo - p.W), std::min(p.hi, c.hi - p.W), 0};
                st.pop_back();
                c = mg;
                argmin(c);
            }
            st.push_back(c);
        }
        m.kind = Move::kRowOpt;
        m.n = 0;
        bool changed = false;
        for (const Cl& c : st) {
            int pos = c.x;
            for (int j = c.first; j <= c.last; ++j) {
                int v = cl[first + j];
                if (P_.site[v] != pos) changed = true;
                m.v[m.n] = v; m.seg[m.n] = s; m.site[m.n] = pos; ++m.n;
                pos += w[j];
            }
        }
        return changed;
    }

    // v3: independent set matching [NTUplace3; ABCDPlace]: up to kIsmSize cells
    // of a's width near a that share no net trade slots by a min-cost
    // assignment (Hungarian). Independent -> each cell's cost depends only on
    // its own slot, so the assignment is exactly HPWL-optimal for the set.
    bool proposeIsm(Move& m, int a) {
        int sa = P_.seg[a];
        int w = db_.sitesOf(a, db_.segs[sa]);
        int r0 = db_.segs[sa].row;
        double ax = P_.x(db_, a);
        cand_.clear();
        for (int r = std::max(0, r0 - 2); r <= std::min((int)db_.rows.size() - 1, r0 + 2); ++r)
            for (int s : db_.rows[r].segs) {
                const Segment& sg = db_.segs[s];
                for (int v : rm_.cells(s)) {
                    if (db_.sitesOf(v, sg) != w || nets_.end(v) == nets_.begin(v)) continue;
                    double dx = std::fabs(sg.x0 + P_.site[v] * sg.sp - ax);
                    if (dx <= opt_.ismReach * minWinX_) cand_.push_back({dx + std::fabs(sg.y - P_.y(db_, a)), v});
                }
            }
        if (cand_.size() < 3) return false;
        std::sort(cand_.begin(), cand_.end());
        ++stampId_;
        int k = 0;
        for (auto& [d, v] : cand_) {
            bool free = true;
            for (const int* it = nets_.begin(v); it != nets_.end(v) && free; ++it) free = netStamp_[*it] != stampId_;
            if (!free) continue;
            for (const int* it = nets_.begin(v); it != nets_.end(v); ++it) netStamp_[*it] = stampId_;
            m.v[k++] = v;
            if (k == std::min(opt_.ismSize, kMaxMove)) break;
        }
        if (k < 3) return false;
        // cost[i][j]: HPWL of cell i's nets with i at slot j (slots = the cells' current spots)
        std::vector<double>& cost = ismCost_;
        cost.assign((size_t)k * k, 0);
        for (int i = 0; i < k; ++i) {
            int v = m.v[i];
            for (const int* it = nets_.begin(v); it != nets_.end(v); ++it) {
                double xl = 1e300, xh = -1e300, yl = 1e300, yh = -1e300;
                for (const int* p = pins_.begin(*it); p != pins_.end(*it); ++p) {
                    if (*p == v) continue;
                    xl = std::min(xl, c_[*p].x); xh = std::max(xh, c_[*p].x);
                    yl = std::min(yl, c_[*p].y); yh = std::max(yh, c_[*p].y);
                }
                for (int j = 0; j < k; ++j) {
                    const Pt& q = c_[m.v[j]];   // same width: slot j's center = cell j's center
                    cost[(size_t)i * k + j] += (std::max(xh, q.x) - std::min(xl, q.x)) + (std::max(yh, q.y) - std::min(yl, q.y));
                }
            }
        }
        std::vector<int> to = hungarian(cost, k);
        bool moved = false;
        int slotSeg[kMaxMove], slotSite[kMaxMove];
        for (int j = 0; j < k; ++j) {
            slotSeg[j] = P_.seg[m.v[j]];
            slotSite[j] = P_.site[m.v[j]];
        }
        for (int i = 0; i < k; ++i) {
            m.seg[i] = slotSeg[to[i]];
            m.site[i] = slotSite[to[i]];
            moved = moved || to[i] != i;
        }
        m.kind = Move::kIsm;
        m.n = k;
        return moved;
    }

    bool propose(Move& m) {
        int a = active_[rng_.below((int)active_.size())];
        double r = rng_.u01();
        if (polish_) {   // greedy macro moves only once the run has cooled down
            if (r < opt_.pRowOpt) return proposeRowOpt(m, a);
            if (r < opt_.pRowOpt + opt_.pIsm) return proposeIsm(m, a);
        }
        if (rng_.u01() < opt_.pReorder) return proposeReorder(m, a);
        m.kind = Move::kMove;
        const Node& na = db_.nodes[a];
        double tx, ty, xl, xh, yl, yh;
        if (rng_.u01() < opt_.pTarget && optimalRegion(a, xl, xh, yl, yh)) {
            tx = rng_.uniform(xl, xh);
            ty = rng_.uniform(yl, yh);
            if (opt_.clampTarget) {
                tx = std::min(c_[a].x + winX_, std::max(c_[a].x - winX_, tx));
                ty = std::min(c_[a].y + winY_, std::max(c_[a].y - winY_, ty));
            }
        } else {
            tx = c_[a].x + rng_.uniform(-winX_, winX_);
            ty = c_[a].y + rng_.uniform(-winY_, winY_);
        }
        int s = segmentAt(db_.nearestRow(ty - na.h / 2), tx);
        const Segment& sg = db_.segs[s];
        int w = db_.sitesOf(a, sg);
        if (w > sg.numSites) return false;
        int tSite = std::min(sg.numSites - 1, std::max(0, (int)std::floor((tx - sg.x0) / sg.sp)));
        long want = std::lround((tx - na.w / 2 - sg.x0) / sg.sp);   // left site that centers a on tx

        Gap g;
        int b = rm_.cellAt(s, tSite, g, a);
        if (b < 0 ? moveInto(m, a, s, g, want, w) : swapWith(m, a, b, s, want, w)) return true;
        // v2: the target did not work (no room) -> try the nearest cells and gaps of that
        // row, alternating left and right of the target, before giving up.
        const auto& cl = rm_.cells(s);
        int j0 = (int)(std::upper_bound(cl.begin(), cl.end(), tSite,
                                        [&](int t, int v) { return t < P_.site[v]; }) - cl.begin());
        // Only options close to the target count: in sparse rows the nearest
        // cells in the list can be far away, and such moves rarely help.
        long reach = std::lround(minWinX_ / sg.sp) + w;
        for (int step = 0; step < 2 * near_; ++step) {
            int j = step % 2 ? j0 + step / 2 : j0 - 1 - step / 2;
            if (j < 0 || j >= (int)cl.size() || cl[j] == a || cl[j] == b) continue;
            if (std::labs(P_.site[cl[j]] - want) > reach) continue;
            Gap after = gapAfter(s, j, a);
            if (after.hi - w >= want - reach && after.lo <= want + reach && moveInto(m, a, s, after, want, w))
                return true;
            if (swapWith(m, a, cl[j], s, want, w)) return true;
        }
        return false;
    }

    // Free run right after list entry j of segment s (cell `skip` counts as removed).
    Gap gapAfter(int s, int j, int skip) const {
        const auto& cl = rm_.cells(s);
        Gap g;
        g.lo = P_.site[cl[j]] + db_.sitesOf(cl[j], db_.segs[s]);
        int k = j + 1;
        if (k < (int)cl.size() && cl[k] == skip) ++k;
        g.hi = k < (int)cl.size() ? P_.site[cl[k]] : db_.segs[s].numSites;
        return g;
    }

    bool moveInto(Move& m, int a, int s, const Gap& g, long want, int w) {
        if (g.width() < w) return false;
        int site = (int)std::max<long>(g.lo, std::min<long>(g.hi - w, want));
        if (s == P_.seg[a] && site == P_.site[a]) return false;
        m.n = 1;
        m.kind = Move::kMove;
        m.v[0] = a; m.seg[0] = s; m.site[0] = site;
        Gap own = rm_.freeAround(a);
        m.reorder = !(s == P_.seg[a] && site >= own.lo && site + w <= own.hi);
        return true;
    }

    bool swapWith(Move& m, int a, int b, int s, long want, int w) {
        const Segment& sg = db_.segs[s];
        int sa = P_.seg[a];
        int wb = db_.sitesOf(b, db_.segs[sa]);
        if (sa == s && std::abs(rm_.indexOf(a) - rm_.indexOf(b)) == 1) {   // neighbours: exchange order
            int l = P_.site[a] < P_.site[b] ? a : b, r = l == a ? b : a;
            int lSite = P_.site[l], rEnd = P_.site[r] + db_.sitesOf(r, sg);
            int newR = lSite, newL = rEnd - db_.sitesOf(l, sg);
            m.n = 2;
            m.kind = Move::kSwap;
            m.v[0] = a; m.v[1] = b;
            m.seg[0] = m.seg[1] = s;
            m.site[0] = a == l ? newL : newR;
            m.site[1] = b == l ? newL : newR;
            return true;
        }
        Gap ga = rm_.freeAround(a), gb = rm_.freeAround(b);
        if (gb.width() < w || ga.width() < wb) return false;
        long bWant = P_.site[a] + std::lround((db_.nodes[a].w - db_.nodes[b].w) / 2 / db_.segs[sa].sp);
        m.n = 2;
        m.kind = Move::kSwap;
        m.v[0] = a; m.v[1] = b;
        m.seg[0] = s;
        m.site[0] = (int)std::max<long>(gb.lo, std::min<long>(gb.hi - w, want));
        m.seg[1] = sa;
        m.site[1] = (int)std::max<long>(ga.lo, std::min<long>(ga.hi - wb, bWant));
        return true;
    }

    // T0 such that an average uphill move is accepted with probability p0.
    double initialTemperature() {
        double sum = 0;
        int cnt = 0;
        for (int k = 0; k < 20000 && cnt < 5000; ++k) {
            Move m;
            if (!propose(m)) continue;
            double d = delta(m);
            revert(m);
            if (d > 1e-9) { sum += d; ++cnt; }
        }
        double avg = cnt ? sum / cnt : 1.0;
        return -avg / std::log(opt_.p0);
    }

    struct Undo { int v, seg, site; };

    const PlaceDB& db_;
    Placement& P_;
    SAOptions opt_;
    RowMap rm_;
    Rng rng_;
    std::vector<Pt> c_;            // cell centers
    Csr pins_, nets_;              // net -> nodes, node -> nets (degree >= 2)
    std::vector<double> netH_;
    std::vector<int> netStamp_;
    int stampId_ = 0;
    struct Touch { int n; double h; Box b; };
    std::vector<Touch> touched_;
    std::vector<Box> box_;                     // v3: exact bounding box per net
    std::vector<int> netCnt_, netMover_, touchedNets_;
    Pt old_[kMaxMove];
    std::vector<int> active_;      // movable cells on at least one net (others never change HPWL)
    int near_ = 0;                 // fallback tries per side (see SAOptions::nearTries)
    long long stat_[6][3] = {}, failed_ = 0;   // per move kind: tried, accepted, improving
    std::vector<int> winStamp_, winIdx_, rwW_;  // v3: window membership / widths for rowopt
    std::vector<std::pair<double, int>> cand_;  // v3: ism candidates (distance, cell)
    std::vector<double> ismCost_;
    bool polish_ = false;           // v3: rowopt / ism enabled (late, cold part of the schedule)
    int winStampId_ = 0;
    struct Term { int type; double v; };      // rowopt cost term of one cell (see proposeRowOpt)
    std::vector<std::vector<Term>> terms_;
    double cur_ = 0, best_ = 0;
    std::vector<int> cellEpoch_;
    int epoch_ = 0;
    std::vector<Undo> journal_;
    std::vector<double> xs_, ys_;
    double xLo_, xHi_, yLo_, yHi_, rowH_, winX_ = 0, winY_ = 0, minWinX_ = 0, minWinY_ = 0, maxWinX_ = 0, maxWinY_ = 0;
};

}  // namespace

SAResult anneal(const PlaceDB& db, Placement& P, const SAOptions& opt) {
    return Annealer(db, P, opt).run();
}
