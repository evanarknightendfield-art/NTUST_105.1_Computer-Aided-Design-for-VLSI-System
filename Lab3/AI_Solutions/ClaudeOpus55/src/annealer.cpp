// annealer.cpp — Stage 2.
//
// State: a legal placement. Every move keeps it legal, so the cost is just HPWL.
// Moves (TimberWolf M1/M2 adapted to rows):
//   * pick a cell a and a target point: inside a's optimal region (median of
//     its nets' bounding boxes) or a random offset within the range window;
//   * target is free space that fits a  -> move a there
//   * target is another cell b          -> swap a and b if each fits in the
//                                          other's room (neighbours in the same
//                                          row just exchange order)
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

struct Move {
    int n = 0;                    // cells moved: 1 (move) or 2 (swap)
    int v[2], seg[2], site[2];    // new positions
    bool reorder = false;         // single move that leaves its own gap -> remove + insert
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
        netH_.resize(db.nets.size());
        netStamp_.assign(db.nets.size(), 0);
        cellEpoch_.assign(N, -1);
        cur_ = 0;
        for (size_t n = 0; n < db.nets.size(); ++n) cur_ += netH_[n] = netHPWL((int)n);

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
        double T0 = initialTemperature();
        double Tend = T0 * opt_.tEndRatio;
        int steps = std::max(1, opt_.steps);
        double alpha = std::pow(Tend / T0, 1.0 / steps);
        long long total = (long long)(opt_.movesPerCell * db_.movable.size());
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
            for (; k < perStep; ++k, ++it) {
                Move m;
                if (propose(m)) {
                    double d = delta(m);
                    if (d <= 0 || rng_.u01() < std::exp(-d / T)) {
                        commit(m, d);
                        ++acc;
                    } else {
                        revert(m);
                    }
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
            winX_ = std::min(xHi_ - xLo_, std::max(minWinX_, winX_ * f));
            winY_ = std::min(yHi_ - yLo_, std::max(minWinY_, winY_ * f));
            resum();
            if (opt_.verbose && (step % 20 == 0 || step == steps - 1))
                std::fprintf(stderr, "[sa] step %3d  T %.4g  acc %.3f  win %.0f x %.0f  cur %.0f  best %.0f  %.1fs\n",
                             step, T, rate, winX_, winY_, cur_, best_, elapsed());
            T *= alpha;
        }
        if (timeUp && opt_.verbose) std::fprintf(stderr, "[sa] time limit reached\n");

        restoreBest();
        res.attempts = it;
        res.best = best_;
        res.seconds = elapsed();
        if (trace.is_open()) trace << it << ',' << cur_ << ',' << best_ << ',' << T << ',' << winX_ << ',' << winY_ << ",0\n";
        return res;
    }

private:
    // ---------------------------------------------------------- cost
    double netHPWL(int n) const {
        const int *p = pins_.begin(n), *e = pins_.end(n);
        if (e - p < 2) return 0;
        double xl = c_[*p].x, xh = xl, yl = c_[*p].y, yh = yl;
        for (++p; p != e; ++p) {
            const Pt& q = c_[*p];
            if (q.x < xl) xl = q.x; else if (q.x > xh) xh = q.x;
            if (q.y < yl) yl = q.y; else if (q.y > yh) yh = q.y;
        }
        return (xh - xl) + (yh - yl);
    }

    void resum() {
        cur_ = 0;
        for (double h : netH_) cur_ += h;
    }

    // Puts the moved cells at their new centers and returns the HPWL change.
    double delta(const Move& m) {
        ++stampId_;
        touched_.clear();
        for (int i = 0; i < m.n; ++i) {
            int v = m.v[i];
            const Segment& s = db_.segs[m.seg[i]];
            old_[i] = c_[v];
            c_[v] = {s.x0 + m.site[i] * s.sp + db_.nodes[v].w / 2, s.y + db_.nodes[v].h / 2};
        }
        double d = 0;
        for (int i = 0; i < m.n; ++i)
            for (const int* it = nets_.begin(m.v[i]); it != nets_.end(m.v[i]); ++it) {
                int n = *it;
                if (netStamp_[n] == stampId_) continue;
                netStamp_[n] = stampId_;
                double h = netHPWL(n);
                touched_.push_back({n, h});
                d += h - netH_[n];
            }
        return d;
    }

    void revert(const Move& m) {
        for (int i = 0; i < m.n; ++i) c_[m.v[i]] = old_[i];
    }

    void commit(const Move& m, double d) {
        for (auto& [n, h] : touched_) netH_[n] = h;
        cur_ += d;
        for (int i = 0; i < m.n; ++i) {   // undo journal: first change since the best was seen
            int v = m.v[i];
            if (cellEpoch_[v] != epoch_) {
                cellEpoch_[v] = epoch_;
                journal_.push_back({v, P_.seg[v], P_.site[v]});
            }
        }
        if (m.n == 1) {
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
        for (size_t n = 0; n < db_.nets.size(); ++n) netH_[n] = netHPWL((int)n);
        resum();
        best_ = std::min(best_, cur_);
    }

    // ---------------------------------------------------------- moves
    // Optimal region of v: the median interval of its nets' bounding boxes
    // (computed without v). Any point inside minimizes v's own HPWL.
    bool optimalRegion(int v, double& xl, double& xh, double& yl, double& yh) {
        xs_.clear();
        ys_.clear();
        for (const int* it = nets_.begin(v); it != nets_.end(v); ++it) {
            double bxl = 1e300, bxh = -1e300, byl = 1e300, byh = -1e300;
            for (const int* p = pins_.begin(*it); p != pins_.end(*it); ++p) {
                if (*p == v) continue;
                const Pt& q = c_[*p];
                bxl = std::min(bxl, q.x); bxh = std::max(bxh, q.x);
                byl = std::min(byl, q.y); byh = std::max(byh, q.y);
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

    bool propose(Move& m) {
        int a = db_.movable[rng_.below((int)db_.movable.size())];
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
        if (b < 0) {   // free space (possibly including a's own spot)
            if (g.width() < w) return false;
            int site = (int)std::max<long>(g.lo, std::min<long>(g.hi - w, want));
            if (s == P_.seg[a] && site == P_.site[a]) return false;
            m.n = 1;
            m.v[0] = a; m.seg[0] = s; m.site[0] = site;
            Gap own = rm_.freeAround(a);
            m.reorder = !(s == P_.seg[a] && site >= own.lo && site + w <= own.hi);
            return true;
        }

        int sa = P_.seg[a];
        int wb = db_.sitesOf(b, db_.segs[sa]);
        m.n = 2;
        m.v[0] = a; m.v[1] = b;
        if (sa == s && std::abs(rm_.indexOf(a) - rm_.indexOf(b)) == 1) {   // neighbours: exchange order
            int l = P_.site[a] < P_.site[b] ? a : b, r = l == a ? b : a;
            int lSite = P_.site[l], rEnd = P_.site[r] + db_.sitesOf(r, sg);
            int newR = lSite, newL = rEnd - db_.sitesOf(l, sg);
            m.seg[0] = m.seg[1] = s;
            m.site[0] = a == l ? newL : newR;
            m.site[1] = b == l ? newL : newR;
            return true;
        }
        Gap ga = rm_.freeAround(a), gb = rm_.freeAround(b);
        if (gb.width() < w || ga.width() < wb) return false;
        long bWant = P_.site[a] + std::lround((db_.nodes[a].w - db_.nodes[b].w) / 2 / db_.segs[sa].sp);
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
    std::vector<std::pair<int, double>> touched_;
    Pt old_[2];
    double cur_ = 0, best_ = 0;
    std::vector<int> cellEpoch_;
    int epoch_ = 0;
    std::vector<Undo> journal_;
    std::vector<double> xs_, ys_;
    double xLo_, xHi_, yLo_, yHi_, rowH_, winX_ = 0, winY_ = 0, minWinX_ = 0, minWinY_ = 0;
};

}  // namespace

SAResult anneal(const PlaceDB& db, Placement& P, const SAOptions& opt) {
    return Annealer(db, P, opt).run();
}
