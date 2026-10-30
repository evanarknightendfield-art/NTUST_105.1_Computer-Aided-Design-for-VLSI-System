// annealer.cpp — Stage 2.
//
// State: a legal placement. Every move keeps it legal, so the cost is just HPWL.
// Moves (TimberWolf M1/M2 adapted to rows), for cells on at least one net:
//   * pick a cell a and a target point: inside a's optimal region (median of
//     its nets' bounding boxes) or a random offset within the range window;
//   * target is free space that fits a  -> move a there
//   * target is another cell b          -> swap a and b if each fits in the
//                                          other's room (neighbours in the same
//                                          row just exchange order)
//   * neither fits -> try the nearest cells and gaps of that row instead
//     (ibm01: infeasible proposals 49% -> 10%; only in dense designs, see near_)
//   * local reordering [FastPlace-DP]: 3 neighbours trade order in their span
//   * in the colder half: linear placement of a 12-cell window [Kahng et al.,
//     ASPDAC'99, lecture L07] and independent set matching of 12 same-width cells
//     [NTUplace3 / ABCDPlace] — both exact for their sub-problem, never worse
//   * cached net bounding boxes; moves = 5000 per cell, kept in [5e6, 6e7]
// Range limiter (VPR style): the window grows/shrinks to keep acceptance near 44%.
// Schedule: T0 from sampled uphill moves, geometric cooling to T0 * tEndRatio.
// The best placement is recovered from an undo journal, without full copies.
// with several threads the chip is cut into row bands annealed at the
// same time (annealParallel, see there); bands merge after about one attempt
// per cell; the band edges sweep four deterministic offsets at merges.
#include "annealer.hpp"
#include <iomanip>
#include <stdexcept>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <cassert>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

#include "pool.hpp"
#include "sa_rowmap.hpp"
#include "sa_rows.hpp"
#include "sa_region_cache.hpp"
#include "sa_bbox.hpp"
#include "sa_witness.hpp"
#include "sa_bands.hpp"
#include "sa_sync.hpp"
#include "sa_merge_box.hpp"

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

using Pt = sa_detail::SAPoint;
using Box = sa_detail::SABox;

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

// connectivity shared by all workers of the parallel SA (read-only).
struct Graph {
    // Immutable hot geometry, without Node names and other cold input fields.
    struct Geometry { double w, h; };
    std::vector<Geometry> geometry;
    sa_detail::SANearestRows rows;
    Csr pins, nets;   // net -> nodes, node -> nets (degree >= 2)
    explicit Graph(const PlaceDB& db) : rows({}) {
        geometry.reserve(db.nodes.size());
        for (const Node& node : db.nodes) geometry.push_back({node.w, node.h});
        std::vector<double> rowY;
        for (const auto& row : db.rows) rowY.push_back(row.y);
        rows = sa_detail::SANearestRows(std::move(rowY));
        for (const auto& net : db.nets) pins.add(net);
        for (size_t v = 0; v < db.nodes.size(); ++v) {   // nets with < 2 pins never change HPWL
            std::vector<int> keep;
            for (int n : db.nodeNets[v])
                if (db.nets[n].size() >= 2) keep.push_back(n);
            nets.add(keep);
        }
    }
};

// The merged state of the parallel SA. Every merge writes it once (bands
// in parallel, nets in parallel chunks). Workers copy all moved centers, but
// only the net boxes needed by their next band, using per-net revisions.
// had every worker redo every other band's updates itself: row-list
// erase / insert and a net-box scan per moved cell, 25 - 37 % of the SA time.
struct Shared {
    std::vector<Pt> c;                   // cell centers
    std::vector<Box> box;                // net boxes
    std::vector<double> H;               // net HPWL
    std::vector<uint64_t> revision;      // all touched nets, even unchanged boxes
    std::vector<std::vector<int>> rows;  // per segment: cells by site
    double cur = 0;
};

struct Move {
    enum Kind { kMove, kSwap, kReorder, kRowOpt, kIsm };
    Kind kind = kMove;
    int n = 0;                    // cells moved: 1 (move), 2 (swap), 3 (reorder), up to kMaxMove
    int v[kMaxMove], seg[kMaxMove], site[kMaxMove];   // new positions
    bool reorder = false;         // kMove that leaves its own gap -> remove + insert
    int seg0 = -1, idx0 = 0;      // kReorder: segment and list index of the first of the 3 cells
};

class Annealer {
public:
    Annealer(const PlaceDB& db, Placement& P, const SAOptions& opt, std::shared_ptr<const Graph> g, uint64_t seed)
        : db_(db), P_(P), opt_(opt), rm_(db, P), rng_(seed), regionCache_(db.nodes.size(), db.nets.size()), g_(std::move(g)), pins_(g_->pins), nets_(g_->nets) {
        int N = (int)db.nodes.size();
        c_.resize(N);
        for (int v = 0; v < N; ++v) c_[v] = {P.x(db, v) + db.nodes[v].w / 2, P.y(db, v) + db.nodes[v].h / 2};
        bandHi_ = (int)db.rows.size();
        for (int v : db.movable)
            if (nets_.end(v) != nets_.begin(v)) active_.push_back(v);

        // Any move contains at most kMaxMove movable cells. Distinct touched
        // nets cannot exceed their total degree or the graph's net count.
        size_t maxDegree = 0;
        for (int v : active_)
            maxDegree = std::max(maxDegree, (size_t)(nets_.end(v) - nets_.begin(v)));
        touched_.resize(std::min(db.nets.size(), (size_t)kMaxMove * maxDegree));

        // Dense designs block most targeted moves and the fallback rescues them;
        // in sparse ones targets are usually free and the fallback only adds
        // weaker moves (measured: helps at utilization >= 0.74, hurts <= 0.70).
        near_ = opt.nearTries >= 0 ? opt.nearTries : db.utilization() >= 0.72 ? 3 : 0;
        netState_.resize(db.nets.size());
        cellEpoch_.assign(N, -1);
        winStamp_.assign(N, 0);
        winIdx_.assign(N, 0);
        terms_.resize(kMaxMove);
        cur_ = 0;
        for (size_t n = 0; n < db.nets.size(); ++n) {
            const auto b = scanWitness((int)n);
            netState_[n].box = b.b;
            netState_[n].witness = b.owner;
            cur_ += netState_[n].h = span(netState_[n].box);
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
        if (active_.empty()) {res.best=cur_;return res;}

        // the window never grows past maxWin of the chip; with few failed
        // proposals the acceptance rate is high early on and VPR's rule would
        // otherwise blow the window up to half the chip (long, useless moves).
        initWindow();
        double T0 = initialTemperature();
        double Tend = T0 * opt_.tEndRatio;
        int steps = std::max(1, opt_.steps);
        double alpha = std::pow(Tend / T0, 1.0 / steps);
        long long total = (long long)std::max(opt_.minMoves, std::min(opt_.maxMoves, opt_.movesPerCell * db_.movable.size()));
        const long long planned = std::max(1LL, total / steps);
        long long perStep = planned;
        long long traceEvery = std::max(1LL, total / 2000);

        std::ofstream trace;
        if (!opt_.tracePath.empty()) {
            trace.open(opt_.tracePath);
            if(!trace) throw std::runtime_error("cannot open SA trace");
            trace << std::setprecision(15);
            trace << "iteration,current,best,temperature,window_x,window_y,accept_rate\n";
            trace << 0 << ',' << cur_ << ',' << best_ << ',' << T0 << ',' << winX_ << ',' << winY_ << ",1\n";
        }
        if (opt_.verbose)
            std::fprintf(stderr, "[sa] HPWL %.0f  T0 %.4g  alpha %.4f  %lld moves in %d steps\n", cur_, T0, alpha,
                         total, steps);

        double T = T0, lastSecs = elapsed();
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
            // the count may also go back UP to the plan. only lowered it,
            // and the first (hottest, slowest) step set it for the whole run.
            int left = steps - step - 1;
            double secs = elapsed(), dt = secs - lastSecs;   // speed of the last step
            if (left > 0 && dt > 0) {
                double movesLeft = (opt_.timeLimit - secs) * (k / dt);
                perStep = std::max(1LL, std::min(planned, (long long)(movesLeft / left)));
            }
            lastSecs = secs;
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

    // ---------------------------------------------------------- parallel SA pieces
    // Moves only cells of rows [r0, r1) and only into those rows; the rest of
    // the chip is frozen at this worker's copy. No best-so-far journal here:
    // the driver keeps the best merged placement.
    void setBand(int r0, int r1) {
        bandLo_ = r0;
        bandHi_ = r1;
        bandY0_ = db_.rows[r0].y;
        bandY1_ = db_.rows[r1 - 1].y + db_.rows[r1 - 1].height;
        track_ = false;
        active_.clear();
        for (int r = r0; r < r1; ++r)
            for (int s : db_.rows[r].segs)
                for (int v : rm_.cells(s))
                    if (nets_.end(v) != nets_.begin(v)) active_.push_back(v);
    }
    long long activeCount() const { return (long long)active_.size(); }
    long long failed() const { return failed_; }
    void setWindow(double wx, double wy) {
        winX_ = std::min(maxWinX_, std::max(minWinX_, wx));
        winY_ = std::min(maxWinY_, std::max(minWinY_, wy));
    }
    void initWindow() {
        winX_ = std::max(minWinX_, opt_.win0 * (xHi_ - xLo_));
        winY_ = std::max(minWinY_, opt_.win0 * (yHi_ - yLo_));
        maxWinX_ = std::max(winX_, opt_.maxWin * (xHi_ - xLo_));
        maxWinY_ = std::max(winY_, opt_.maxWin * (yHi_ - yLo_));
    }
    double winX() const { return winX_; }
    double winY() const { return winY_; }
    double current() const { return cur_; }
    void reseed(uint64_t seed) { rng_ = Rng(seed); }

    // `moves` attempts at temperature T; returns the number accepted.
    long long batch(double T, long long moves, bool polish) {
        polish_ = polish;
        moved_.clear();
        ++movedStamp_;
        if (movedMark_.empty()) movedMark_.assign(db_.nodes.size(), 0);
        long long acc = 0;
        if (active_.empty()) return 0;
        for (long long k = 0; k < moves; ++k) {
            Move m;
            if (!propose(m)) { ++failed_; continue; }
            double d = delta(m);
            if (d <= 0 || rng_.u01() < std::exp(-d / T)) {
                commit(m, d);
                ++acc;
            } else {
                revert(m);
            }
        }
        return acc;
    }

    const std::vector<int>& moved() const { return moved_; }
    void clearMoved() { moved_.clear(); }

    // parallel merge through Shared.
    // The whole state, once, from this worker (all workers start equal).
    void exportAll(Shared& G) const {
        G.c = c_;
        G.box.resize(netState_.size());
        G.H.resize(netState_.size());
        G.revision.assign(netState_.size(), 0);
        for (size_t n = 0; n < netState_.size(); ++n) {
            G.box[n] = netState_[n].box;
            G.H[n] = netState_[n].h;
        }
        G.cur = cur_;
        G.rows.resize(db_.segs.size());
        for (size_t s = 0; s < db_.segs.size(); ++s) G.rows[s] = rm_.cells((int)s);
    }
    // After a batch: this band's moved cells and rows into G (bands write
    // disjoint cells and rows, so bands may export at the same time), and the
    // nets those cells touch into touchedOut_ (for the shared box update).
    void exportBand(Placement& GP, Shared& G) {
        for (int v : moved_) {
            GP.seg[v] = P_.seg[v];
            GP.site[v] = P_.site[v];
            G.c[v] = c_[v];
        }
        for (int r = bandLo_; r < bandHi_; ++r)
            for (int s : db_.rows[r].segs) G.rows[s] = rm_.cells(s);
        touchedOut_.clear();
        ++stampId_;
        for (int v : moved_)
            for (const int* it = nets_.begin(v); it != nets_.end(v); ++it)
                if (netState_[*it].stamp != stampId_) {
                    netState_[*it].stamp = stampId_;
                    touchedOut_.push_back(*it);
                }
    }
    const std::vector<int>& touchedNets() const { return touchedOut_; }
    // All centers must be current: a local net can have pins in any band.
    // Net boxes are imported by loadBand only for the next band's cells.
    void importFrom(const Placement& GP, const Shared& G,
                    const std::vector<const std::vector<int>*>& lists, size_t externalMoved) {
        sa_detail::importCells(P_.seg, P_.site, c_, GP.seg, GP.site, G.c, lists, externalMoved);
        regionCache_.advance();
        // G.cur is reduced after this parallel phase; loadBand adopts it
        // before the next batch, including a worker returning from idling.
    }
    // The row lists of the coming band (other rows may be stale in this
    // worker; moves never read them), then the band itself.
    void loadBand(const Shared& G, int r0, int r1) {
        cur_ = G.cur;
        for (int r = r0; r < r1; ++r)
            for (int s : db_.rows[r].segs) rm_.setCells(s, G.rows[s]);
        setBand(r0, r1);
        if (loadedRevision_.empty()) loadedRevision_.assign(G.revision.size(), 0);
        // Every proposal (including swaps and macros) stays in this band.
        // Refresh its incident nets before any speculative center changes;
        // untouched/out-of-band nets need no per-worker cache-line writes.
        // Revisions also deduplicate nets shared by several active cells.
        for (int v : active_)
            for (const int* it = nets_.begin(v); it != nets_.end(v); ++it) {
                const int n = *it;
                if (loadedRevision_[n] == G.revision[n]) continue;
                loadedRevision_[n] = G.revision[n];
                regionCache_.touch(n);
                auto& state = netState_[n];
                state.box = G.box[n];
                state.h = G.H[n];
                // Even after several skipped rounds, a witness whose current
                // center equals the shared endpoint is still an exact proof.
                // Invalid sides rebuild lazily on the next proposal.
                auto& w = state.witness;
                const Box& b = state.box;
                if (w.xl >= 0 && c_[w.xl].x != b.xl) w.xl = -1;
                if (w.xh >= 0 && c_[w.xh].x != b.xh) w.xh = -1;
                if (w.yl >= 0 && c_[w.yl].y != b.yl) w.yl = -1;
                if (w.yh >= 0 && c_[w.yh].y != b.yh) w.yh = -1;
            }
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

private:
    // ---------------------------------------------------------- cost
    sa_detail::SAWitnessBox scanWitness(int n, bool scanX = true, bool scanY = true) const {
        const int *p = pins_.begin(n), *e = pins_.end(n);
        if (e - p < 2) return {{0, 0, 0, 0}, {}};
        const int first = *p;
        sa_detail::SAWitnessBox r{{c_[first].x, c_[first].x, c_[first].y, c_[first].y},
                                  {first, first, first, first}};
        for (++p; p != e; ++p) {
            const int v = *p;
            const Pt& q = c_[v];
            if (scanX) {
                if (q.x < r.b.xl) { r.b.xl = q.x; r.owner.xl = v; }
                if (q.x > r.b.xh) { r.b.xh = q.x; r.owner.xh = v; }
            }
            if (scanY) {
                if (q.y < r.b.yl) { r.b.yl = q.y; r.owner.yl = v; }
                if (q.y > r.b.yh) { r.b.yh = q.y; r.owner.yh = v; }
            }
        }
        return r;
    }
    static double span(const Box& b) { return (b.xh - b.xl) + (b.yh - b.yl); }

    void resum() {
        cur_ = 0;
        for (const auto& n : netState_) cur_ += n.h;
    }

    // Preserve touched-net order and floating-point accumulation order. A
    // surviving endpoint witness proves that side exact, even for many movers.
    double delta(const Move& m) {
        ++stampId_;
        assert(m.n <= kMaxMove);
        touchedSize_ = 0;
        for (int i = 0; i < m.n; ++i) {
            int v = m.v[i];
            const Segment& s = db_.segs[m.seg[i]];
            old_[i] = c_[v];
            c_[v] = {s.x0 + m.site[i] * s.sp + g_->geometry[v].w / 2, s.y + g_->geometry[v].h / 2};
            for (const int* it = nets_.begin(v); it != nets_.end(v); ++it) {
                int n = *it;
                if (netState_[n].stamp != stampId_) {
                    netState_[n].stamp = stampId_;
                    assert(touchedSize_ < touched_.size());
                    netState_[n].touch = (int)touchedSize_;
                    Touch& t = touched_[touchedSize_++];
                    t.n = n;
                    t.h = 0;
                    t.bounds.b = netState_[n].box;
                    t.bounds.owner = netState_[n].witness;
                }
                sa_detail::expandWitness(touched_[netState_[n].touch].bounds, v, c_[v]);
            }
        }
        double d = 0;
        for (size_t ti = 0; ti < touchedSize_; ++ti) {
            Touch& t = touched_[ti];
            t.bounds = sa_detail::repairWitness(t.bounds, [&](int v) { return c_[v]; },
                [&](bool scanX, bool scanY) { return scanWitness(t.n, scanX, scanY); });
            t.h = span(t.bounds.b);
            d += t.h - netState_[t.n].h;
        }
        return d;
    }

    void revert(const Move& m) {
        for (int i = 0; i < m.n; ++i) c_[m.v[i]] = old_[i];
    }

    void commit(const Move& m, double d) {
        regionCache_.advance();
        for (size_t ti = 0; ti < touchedSize_; ++ti) {
            const Touch& t = touched_[ti];
            regionCache_.touch(t.n);
            netState_[t.n].h = t.h;
            netState_[t.n].box = t.bounds.b;
            netState_[t.n].witness = t.bounds.owner;
        }
        cur_ += d;
        if (!track_)   // parallel worker: remember which cells this batch moved
            for (int i = 0; i < m.n; ++i)
                if (movedMark_[m.v[i]] != movedStamp_) {
                    movedMark_[m.v[i]] = movedStamp_;
                    moved_.push_back(m.v[i]);
                }
        for (int i = 0; i < m.n && track_; ++i) {   // undo journal: first change since the best was seen
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
        if (track_ && cur_ < best_ - 1e-6) {
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
        regionCache_.clear();
        for (int v : db_.movable) c_[v] = {P_.x(db_, v) + g_->geometry[v].w / 2, P_.y(db_, v) + g_->geometry[v].h / 2};
        for (size_t n = 0; n < db_.nets.size(); ++n) {
            const auto b = scanWitness((int)n);
            netState_[n].box = b.b;
            netState_[n].witness = b.owner;
            netState_[n].h = span(netState_[n].box);
        }
        resum();
        best_ = std::min(best_, cur_);
    }

    // ---------------------------------------------------------- moves
    // Optimal region of v: the median interval of its nets' bounding boxes
    // (computed without v). Any point inside minimizes v's own HPWL.
    bool optimalRegion(int v, double& xl, double& xh, double& yl, double& yh) {
        Box cached;
        if (regionCache_.get(v, nets_.begin(v), nets_.end(v), cached)) {
            xl = cached.xl; xh = cached.xh; yl = cached.yl; yh = cached.yh;
            return true;
        }
        xs_.clear();
        ys_.clear();
        const Pt& cv = c_[v];
        for (const int* it = nets_.begin(v); it != nets_.end(v); ++it) {
            const Box& o = netState_[*it].box;
            // if v is strictly inside the net's box (per axis), the box without v is the same
            const auto& w = netState_[*it].witness;
            bool inX = (cv.x > o.xl && cv.x < o.xh) ||
                       (w.xl >= 0 && w.xh >= 0 && w.xl != v && w.xh != v);
            bool inY = (cv.y > o.yl && cv.y < o.yh) ||
                       (w.yl >= 0 && w.yh >= 0 && w.yl != v && w.yh != v);
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
        regionCache_.put(v, {xl, xh, yl, yh});
        return true;
    }

    // Segment of row r (inside the band) nearest to x; -1 if the band has none.
    int segmentAt(int r, double x) const {
        if(db_.rows[r].segs.empty()) {
            int nearest=-1;double d=1e300;
            for(int j=bandLo_;j<bandHi_;++j) if(!db_.rows[j].segs.empty()) {
                double v=std::fabs(db_.rows[j].y-db_.rows[r].y);if(v<d){d=v;nearest=j;}
            }
            if(nearest<0) return -1;
            r=nearest;
        }
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

    // local reordering (FastPlace-DP): three neighbours in a row trade
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

    // linear placement [Kahng et al., ASPDAC'99; lecture L07]: a window of
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
                if (netState_[n].stamp == stampId_) continue;
                netState_[n].stamp = stampId_;
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
            double cx = sg.x0 + x * sg.sp + g_->geometry[cl[first + j]].w / 2, f = 0;
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

    // independent set matching [NTUplace3; ABCDPlace]: up to kIsmSize cells
    // of a's width near a that share no net trade slots by a min-cost
    // assignment (Hungarian). Independent -> each cell's cost depends only on
    // its own slot, so the assignment is exactly HPWL-optimal for the set.
    bool proposeIsm(Move& m, int a) {
        int sa = P_.seg[a];
        int w = db_.sitesOf(a, db_.segs[sa]);
        int r0 = db_.segs[sa].row;
        double ax = P_.x(db_, a);
        cand_.clear();
        for (int r = std::max(bandLo_, r0 - 2); r <= std::min(bandHi_ - 1, r0 + 2); ++r)
            for (int s : db_.rows[r].segs) {
                const Segment& sg = db_.segs[s];
                for (int v : rm_.cells(s)) {
                    if (db_.sitesOf(v, sg) != w || g_->geometry[v].w != g_->geometry[a].w || g_->geometry[v].h != g_->geometry[a].h || sg.sp != db_.segs[sa].sp || nets_.end(v) == nets_.begin(v)) continue;
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
            for (const int* it = nets_.begin(v); it != nets_.end(v) && free; ++it) free = netState_[*it].stamp != stampId_;
            if (!free) continue;
            for (const int* it = nets_.begin(v); it != nets_.end(v); ++it) netState_[*it].stamp = stampId_;
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

    // Evaluate several legal global/vertical swaps, including whitespace,
    // against the exact affected-net union. In 1-3 rows this still searches
    // horizontally far beyond a local ordering window. Each invocation is
    // one SA attempt, but candidate evaluations are additional work.
    bool proposeGlobalSwap(Move& best, int a) {
        double xl,xh,yl,yh;
        if(!optimalRegion(a,xl,xh,yl,yh)) return false;
        double tx=std::clamp(c_[a].x,xl,xh);
        double ty=std::clamp(c_[a].y,yl,yh);
        int target=db_.nearestRow(ty-g_->geometry[a].h/2);
        int own=db_.segs[P_.seg[a]].row;
        std::vector<int> rows={target-1,target,target+1,own};
        std::sort(rows.begin(),rows.end());rows.erase(std::unique(rows.begin(),rows.end()),rows.end());
        bool found=false;double gain=1e300;
        auto consider=[&](Move& m) {
            double d=delta(m);revert(m);
            if(d<gain) {gain=d;best=m;found=true;}
        };
        for(int row:rows) {
            if(row<bandLo_ || row>=bandHi_) continue;
            int s=segmentAt(row,tx);if(s<0)continue;
            const auto& sg=db_.segs[s];int w=db_.sitesOf(a,sg);
            if(w>sg.numSites || std::abs(g_->geometry[a].h-sg.height)>1e-7)continue;
            long want=std::lround((tx-g_->geometry[a].w/2-sg.x0)/sg.sp);
            int t=std::clamp(int(std::floor((tx-sg.x0)/sg.sp)),0,sg.numSites-1);
            Gap g;int b=rm_.cellAt(s,t,g,a);Move m;
            if(b<0 && moveInto(m,a,s,g,want,w)) consider(m);
            const auto& list=rm_.cells(s);
            int j=int(std::lower_bound(list.begin(),list.end(),t,[&](int v,int x){return P_.site[v]<x;})-list.begin());
            for(int k=std::max(0,j-2);k<std::min(int(list.size()),j+2);++k) {
                int v=list[k];if(v==a)continue;
                if(swapWith(m,a,v,s,want,w)) consider(m);
                Gap after=gapAfter(s,k,a);
                if(moveInto(m,a,s,after,want,w)) consider(m);
            }
        }
        return found;
    }

    bool propose(Move& m) {
        int a = active_[rng_.below((int)active_.size())];
        double r = rng_.u01();
        if (polish_) {   // greedy macro moves only once the run has cooled down
            if (r < opt_.pGlobalSwap) return proposeGlobalSwap(m, a);
            r -= opt_.pGlobalSwap;
            if (r < opt_.pRowOpt) return proposeRowOpt(m, a);
            if (r < opt_.pRowOpt + opt_.pIsm) return proposeIsm(m, a);
        }
        if (rng_.u01() < opt_.pReorder) return proposeReorder(m, a);
        m.kind = Move::kMove;
        const auto& na = g_->geometry[a];
        double tx, ty, xl, xh, yl, yh;
        bool aimed = false;
        if (rng_.u01() < opt_.pTarget && optimalRegion(a, xl, xh, yl, yh)) {
            aimed = true;
            tx = rng_.uniform(xl, xh);
            ty = rng_.uniform(yl, yh);
            if (opt_.clampTarget) {
                tx = std::min(c_[a].x + winX_, std::max(c_[a].x - winX_, tx));
                ty = std::min(c_[a].y + winY_, std::max(c_[a].y - winY_, ty));
            }
        } else {
            tx = c_[a].x + rng_.uniform(-winX_, winX_);
            // in a band, sample y only where window and band overlap (no wasted proposals)
            double lo = std::max(c_[a].y - winY_, bandY0_), hi = std::min(c_[a].y + winY_, bandY1_);
            ty = lo < hi ? rng_.uniform(lo, hi) : c_[a].y;
        }
        int row = g_->rows.nearestRow(ty - na.h / 2);
        if (row < bandLo_ || row >= bandHi_) {   // band: aimed moves stop at the edge, random ones fail
            if (!aimed) return false;
            row = std::min(bandHi_ - 1, std::max(bandLo_, row));
        }
        int s = segmentAt(row, tx);
        if (s < 0) return false;
        const Segment& sg = db_.segs[s];
        int w = db_.sitesOf(a, sg);
        if (w > sg.numSites) return false;
        int tSite = std::min(sg.numSites - 1, std::max(0, (int)std::floor((tx - sg.x0) / sg.sp)));
        long want = std::lround((tx - na.w / 2 - sg.x0) / sg.sp);   // left site that centers a on tx

        Gap g;
        int j0;
        int b = rm_.cellAt(s, tSite, g, a, &j0);
        if (b < 0 ? moveInto(m, a, s, g, want, w) : swapWith(m, a, b, s, want, w)) return true;
        // the target did not work (no room) -> try the nearest cells and gaps of that
        // row, alternating left and right of the target, before giving up.
        const auto& cl = rm_.cells(s);
        // cellAt already located this upper bound; proposal checks do not mutate rows.
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
        long bWant = P_.site[a] + std::lround((g_->geometry[a].w - g_->geometry[b].w) / 2 / db_.segs[sa].sp);
        m.n = 2;
        m.kind = Move::kSwap;
        m.v[0] = a; m.v[1] = b;
        m.seg[0] = s;
        m.site[0] = (int)std::max<long>(gb.lo, std::min<long>(gb.hi - w, want));
        m.seg[1] = sa;
        m.site[1] = (int)std::max<long>(ga.lo, std::min<long>(ga.hi - wb, bWant));
        return true;
    }

    struct Undo { int v, seg, site; };

    const PlaceDB& db_;
    Placement& P_;
    SAOptions opt_;
    SARowMap rm_;
    Rng rng_;
    sa_detail::SARegionCache regionCache_;
    std::shared_ptr<const Graph> g_;
    const Csr &pins_, &nets_;      // net -> nodes, node -> nets (degree >= 2), shared
    std::vector<Pt> c_;            // cell centers
    int bandLo_ = 0, bandHi_ = 0;  // rows this annealer may use
    double bandY0_ = -1e300, bandY1_ = 1e300;
    bool track_ = true;            // best-so-far journal (off in parallel workers)
    std::vector<int> changed_, moved_, movedMark_, touchedOut_;
    int movedStamp_ = 0;
    // Proposal bookkeeping and exact bounds share one cache line per net.
    // Shared remains SoA; this storage is private to each annealing worker.
    struct alignas(64) NetState {
        Box box{};
        sa_detail::SAWitness witness{};
        double h = 0;
        int stamp = 0, touch = 0;
    };
    static_assert(sizeof(NetState) == 64, "SA net state must occupy one cache line");
    std::vector<NetState> netState_;
    std::vector<uint64_t> loadedRevision_;  // parallel band cache; shared state is read-only in batch
    int stampId_ = 0;
    struct Touch { int n; double h; sa_detail::SAWitnessBox bounds; };
    std::vector<Touch> touched_;
    size_t touchedSize_ = 0;
    Pt old_[kMaxMove];
    std::vector<int> active_;      // movable cells on at least one net (others never change HPWL)
    int near_ = 0;                 // fallback tries per side (see SAOptions::nearTries)
    long long stat_[6][3] = {}, failed_ = 0;   // per move kind: tried, accepted, improving
    std::vector<int> winStamp_, winIdx_, rwW_;  // window membership / widths for rowopt
    std::vector<std::pair<double, int>> cand_;  // ism candidates (distance, cell)
    std::vector<double> ismCost_;
    bool polish_ = false;           // rowopt / ism enabled (late, cold part of the schedule)
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

namespace {

// parallel SA. The schedule is the same as the sequential one (T0 from
// sampled moves, geometric cooling, VPR range limiter), but every temperature
// step runs in all bands at once, one worker thread per band. A worker sees
// the rest of the chip as it was at the start of the batch (its own copy), so
// move decisions do not depend on thread timing (a wall-time cutoff can still
// change the number of completed batches). After each batch the bands are merged (rows are disjoint,
// so the merge is legal), the true HPWL is measured and the best merged
// placement is kept. Nets that span two bands are the only source of
// error: their delta is computed against the other band's start-of-step
// positions [parallel SA by spatial decomposition, cf. Kravitz & Rutenbar,
// "Placement by simulated annealing on a multiprocessor", TCAD'87].
// a net's box from the shared centers (same as Annealer::netBox)
Box sharedBox(const Graph& g, const std::vector<Pt>& c, int n) {
    const int *p = g.pins.begin(n), *e = g.pins.end(n);
    return sa_detail::scanSharedBox(c.data(), p, static_cast<size_t>(e - p));
}

SAResult annealParallel(const PlaceDB& db, Placement& P, const SAOptions& opt, int bands,
                        std::shared_ptr<const Graph> g) {
    auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    SAResult res;
    std::vector<Placement> copies(bands, P);
    std::vector<std::unique_ptr<Annealer>> work;
    for (int b = 0; b < bands; ++b)
        work.emplace_back(new Annealer(db, copies[b], opt, g, opt.seed * 1000003ULL + b));
    res.initial = work[0]->current();
    double best = res.initial;
    Placement bestP = P;
    // Graph-filtered active cells are conserved by every move. Complete
    // row bands partition exactly this set, even when thin bands coalesce.
    const long long bandActive = work[0]->activeCount();
    if (bandActive == 0) { res.best = best; return res; }
    for (auto& w : work) w->initWindow();   // window limits for every worker
    double T0 = work[0]->initialTemperature();
    double winX = work[0]->winX(), winY = work[0]->winY();
    int steps = std::max(1, opt.steps);
    double alpha = std::pow(opt.tEndRatio, 1.0 / steps);
    double perCell = opt.movesPerCell * bands;   // every band gets the sequential per-cell budget
    long long total = (long long)std::max(opt.minMoves * bands,
                                          std::min(opt.maxMoves * bands, perCell * db.movable.size()));
    const long long planned = std::max(1LL, total / steps);
    long long perStep = planned;
    long long active = 0;
    for (int v : db.movable)
        if (!db.nodeNets[v].empty()) ++active;
    active = std::max(1LL, active);
    if (opt.verbose)
        std::fprintf(stderr, "[sa] parallel: %d bands/threads  HPWL %.0f  T0 %.4g  %lld moves in %d steps, sync every %.1f moves/cell\n",
                     bands, res.initial, T0, total, steps, opt.syncMoves);
    std::ofstream trace;
    if (!opt.tracePath.empty()) {
        trace.open(opt.tracePath);
        if (!trace) throw std::runtime_error("cannot open SA trace");
        trace << std::setprecision(15);
        trace << "iteration,current,best,temperature,window_x,window_y,accept_rate\n";
        trace << 0 << ',' << res.initial << ',' << best << ',' << T0 << ',' << winX << ',' << winY << ",1\n";
    }
    Pool pool(bands);
    std::vector<long long> acc(bands), att(bands);
    Shared G;
    work[0]->exportAll(G);
    std::vector<int> netStamp(db.nets.size(), 0), touched;
    int netStampId = 0;
    std::vector<double> dH(bands, 0), batchSecs(bands, 0);
    // moved vector objects remain stable. Inactive workers clear their list
    // before phase 1, so these lists need no per-round allocation/rebuilding.
    std::vector<std::vector<const std::vector<int>*>> otherMoves(bands);
    for (int b = 0; b < bands; ++b)
        for (int o = 0; o < bands; ++o)
            if (o != b) otherMoves[b].push_back(&work[o]->moved());
    double T = T0, cur = res.initial, lastSecs = elapsed(), tBatch = 0, tSync = 0;
    long long it = 0, round = 0;
    bool timeUp = false;
    double limit = opt.timeLimit;   // may be cut after step 0 (starved SA)
    for (int step = 0; step < steps; ++step) {
        bool polish = step >= opt.polishFrom * steps;
        long long a = 0, n = 0;
        // sub-rounds of about syncMoves attempts per cell, then merge
        long long sub = std::max(1LL, (long long)std::llround(perStep / (opt.syncMoves * active)));
        for (long long k = 0; k < sub; ++k, ++round) {
            // A complete batch (including its merge) is the cancellation unit:
            // never start another one after the deadline has passed.
            if (elapsed() >= limit) { timeUp = true; break; }
            std::vector<int> edge = sa_detail::bandEdges(db, P, bands, round, std::max(1, opt.minBandRows / 2));
            int nb = (int)edge.size() - 1;
            // Phase 1: a worker loads, anneals, and exports its own band.
            // Loading reads only its disjoint shared rows and shared net state;
            // exports write only its disjoint shared rows/centers/positions.
            // No worker reads another worker's mutable local state here.
            for (int b = nb; b < bands; ++b) work[b]->clearMoved();
            double tb = elapsed();
            pool.run(nb, [&](int b) {
                work[b]->loadBand(G, edge[b], edge[b + 1]);
                work[b]->setWindow(winX, winY);
                work[b]->reseed(opt.seed * 7919ULL + round * 131ULL + b);
                att[b] = (perStep / sub) * work[b]->activeCount() / bandActive;
                double beforeBatch = elapsed();
                acc[b] = work[b]->batch(T, att[b], polish);
                batchSecs[b] = elapsed() - beforeBatch;
                work[b]->exportBand(P, G);
            });
            // Phases overlap: report the longest worker batch as moves, and
            // the remaining phase wall time as sync. Their sum stays wall time.
            double longestBatch = *std::max_element(batchSecs.begin(), batchSecs.begin() + nb);
            tBatch += longestBatch;
            tSync += elapsed() - tb - longestBatch;
            tb = elapsed();
            // Keep the original band-order touched list and net chunks so
            // both net scans and floating-point reductions remain identical.
            ++netStampId;
            touched.clear();
            for (int b = 0; b < nb; ++b)
                for (int net : work[b]->touchedNets())
                    if (netStamp[net] != netStampId) {
                        netStamp[net] = netStampId;
                        touched.push_back(net);
                    }
            size_t movedCount = 0;
            for (int b = 0; b < nb; ++b) movedCount += work[b]->moved().size();
            size_t chunk = (touched.size() + bands - 1) / bands;
            pool.run(bands, [&](int b) {
                double d = 0;
                size_t lo = std::min(touched.size(), b * chunk), hi = std::min(touched.size(), lo + chunk);
                for (size_t i = lo; i < hi; ++i) {
                    int net = touched[i];
                    Box bx = sharedBox(*g, G.c, net);
                    double h = (bx.xh - bx.xl) + (bx.yh - bx.yl);
                    d += h - G.H[net];
                    G.box[net] = bx;
                    G.H[net] = h;
                    ++G.revision[net];
                }
                dH[b] = d;
                // Phase 2: merge reads G.c and writes shared net state;
                // import reads G.c/P and writes only this worker's copies.
                // All workers, including inactive ones, catch up here.
                work[b]->importFrom(P, G, otherMoves[b], movedCount - work[b]->moved().size());
            });
            for (int b = 0; b < bands; ++b) G.cur += dH[b];
            for (int b = 0; b < nb; ++b) { a += acc[b]; n += att[b]; }
            tSync += elapsed() - tb;
            // A later batch at this same temperature may worsen HPWL. Keep
            // every synchronized incumbent, not just the step's final one.
            cur = G.cur;
            if (cur < best - 1e-6) { best = cur; bestP = P; }
        }
        cur = G.cur;
        it += n;
        res.accepted += a;
        double rate = (double)a / std::max(1LL, n);
        double f = std::min(2.0, std::max(0.5, 1 - 0.44 + rate));
        work[0]->setWindow(winX * f, winY * f);   // clamps to the allowed range
        winX = work[0]->winX();
        winY = work[0]->winY();
        if (trace.is_open())
            trace << it << ',' << cur << ',' << best << ',' << T << ',' << winX << ',' << winY << ',' << rate << '\n';
        if (opt.verbose && (step % 20 == 0 || step == steps - 1))
            std::fprintf(stderr, "[sa] step %3d  T %.4g  acc %.3f  win %.0f x %.0f  cur %.0f  best %.0f  %.1fs\n", step, T,
                         rate, winX, winY, cur, best, elapsed());
        if (timeUp || elapsed() >= limit || n == 0) break;
        // too slow for the time limit: fewer moves per step, same temperatures.
        // speed measured over the last step only (hot steps are slower;
        // the overall average stopped the run 10 - 30 % early)
        // and back up to the plan when the later, colder steps run faster.
        // kept the count of the first (slowest) step: 43 - 50 of 60 s used,
        // on the 200k-cell L1 15 % of the planned moves.
        int left = steps - step - 1;
        double secs = elapsed(), dt = secs - lastSecs;
        // a starved SA hands its time to the polish. When the whole limit
        // buys fewer than starvedAttemptsPerCell attempts per cell (at the rate
        // of step 0, the slowest), the SA keeps only starvedMinFraction of it.
        // From the same L2 start (1M cells, ~45 attempts per cell in 60 s):
        // SA 0 / 7 / 15 / 30 / 60 s, rest polish -> HPWL 1.545 / 1.546 /
        // 1.569 / 1.616 / 1.802e9. The
        // 200k-cell L1 (~375 per cell) is best with the full 60 s (45 s: +3 %).
        // A cut in proportion cannot serve both, hence a threshold
        //.
        if (step == 0 && dt > 0 && opt.starvedAttemptsPerCell > 0) {
            double perCellAll = n / dt * limit / std::max<size_t>(1, db.movable.size());
            if (perCellAll < opt.starvedAttemptsPerCell) {
                double cut = std::max(elapsed(), opt.starvedMinFraction * limit);
                if (opt.verbose)
                    std::fprintf(stderr, "[sa] starved: %.0f attempts per cell in %.1f s, limit -> %.1f s\n", perCellAll, limit, cut);
                res.limitCut = limit - cut;
                limit = cut;
            }
        }
        if (left > 0 && dt > 0) {
            double movesLeft = (limit - secs) * (n / dt);
            perStep = std::max(1LL, std::min(planned, (long long)(movesLeft / left)));
        }
        lastSecs = secs;
        T *= alpha;
    }
    P = bestP;
    res.best = best;
    res.attempts = it;
    res.seconds = elapsed();
    if (opt.verbose) {
        long long failed = 0;
        for (auto& w : work) failed += w->failed();
        std::fprintf(stderr, "[sa] %lld sync rounds, %lld attempts (%.1f%% proposals failed)  %.1fs (moves %.1fs, merge %.1fs)\n",
                     round, it, 100.0 * failed / std::max(1LL, it), res.seconds, tBatch, tSync);
    }
    return res;
}

}  // namespace

int saThreads(const PlaceDB& db, const SAOptions& opt) {
    int threads = opt.threads > 0 ? opt.threads : 8;   // not min(8, hardware threads), see main.cpp
    // Parallel SA only pays off when one thread could not finish the schedule
    // in the time limit (ibm01 -3.7 %, h06 -1.8 %, 200k cells -6.8 % at equal
    // time). When one thread finishes early anyway (h05: 30M moves in 16 s) the
    // extra moves add nothing and the band edges cost quality (h05: +0.4 %
    // with 2 threads, +1.4 % with 6). Below 8000 cells (< 4e7 planned moves)
    // the automatic choice is one thread; --threads N still forces N.
    // ... unless one thread cannot finish its schedule in the time limit
    // (about 1.2M moves/s on the holdout designs). With SA limited to 10 s,
    // 4 threads were better on h03 / h04 / h05 by 1.8 / 0.4 / 0.4 % (3 seeds,
    // tune/sa_threads_t10); with the default 60 s this keeps 's choice.
    if (opt.threads <= 0 && db.movable.size() < 8000) {
        double planned = std::max(opt.minMoves, std::min(opt.maxMoves, opt.movesPerCell * double(db.movable.size())));
        if (planned / 1.2e6 <= 0.7 * opt.timeLimit) threads = 1;
    }
    // memory: every worker holds its own copy of positions and net boxes. Past
    // a few hundred thousand objects the copies no longer fit in the cache:
    // on the 200k-cell test design 8 threads did no more moves than 4 (memory
    // bound) but had more band edges, and ended 8 % worse; 4 was best.
    long long n = std::max<long long>(1, (long long)db.movable.size() + (long long)db.nets.size());
    // at least 4, and the hard cap 12e6 / n: on the
    // 1M-cell L2 (n = 2.7M) four workers did 29 % more attempts than two and
    // ended 0.7-0.8 % better (2 seeds); 8e6 / n gave 3
    threads = (int)std::min<long long>(threads, std::max<long long>(4, 2000000LL / n));
    threads = (int)std::min<long long>(threads, std::max<long long>(1, 12000000LL / n));
    // the automatic count is 8 whatever the core count (main.cpp). Where the memory cap
    // above applies, more workers than CPUs only add band edges: on 2 P-cores the 200k-cell L1
    // did 6 % more attempts with 4 workers than with 2 and ended 1.0 - 1.4 % worse
    // while h08 (no cap) gained 28 % attempts and 1.6 %
    // HPWL with 8 workers on the same 2 cores. On these designs the count is the min of its
    // cap and the CPUs this process may use.
    if (opt.threads <= 0 && 2000000LL / n < 8) threads = std::min(threads, availableCpus());
    // a band needs enough rows for vertical moves, and enough cells to be worth a thread
    threads = std::min(threads, std::max(1, (int)db.rows.size() / std::max(1, opt.minBandRows)));
    threads = std::min(threads, std::max(1, (int)(db.movable.size() / 1000)));
    return std::max(1, threads);
}

SAResult anneal(const PlaceDB& db, Placement& P, const SAOptions& opt) {
    auto g = std::make_shared<const Graph>(db);
    int threads = saThreads(db, opt);
    if (threads > 1) return annealParallel(db, P, opt, threads, g);
    return Annealer(db, P, opt, g, opt.seed).run();
}
