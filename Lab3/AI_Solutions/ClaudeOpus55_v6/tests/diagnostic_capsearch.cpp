#include "diagnostic.hpp"
// capsearch.cpp — v5: lower the maximum displacement by reassigning rows.
//
// Why (Testcases/results/ClaudeOpus55_v5/diagnosis.md): ChainDP's global cap
// repair kept every cell on a fixed site and moved one cell at a time (site
// congestion negotiation, MRV DFS); on toy and h01 it never found a lower
// cap. The exact min-max placements move whole groups: 22 of 31 toy cells
// change row (up to 5 rows), and in h01 17 cells change row while 62 others
// slide inside their rows. A single-site move cannot open the room a row
// needs; the row has to be re-packed. Here the unit of change is a segment's
// membership, and every segment is packed exactly:
//   * feasibility of a segment under the cap D = single-machine scheduling
//     with release dates (leftmost allowed site) and deadlines (rightmost
//     allowed site + width); Schrage's rule (non-delay earliest deadline)
//     decides it. It is a heuristic in general (it can only miss feasible
//     memberships, never accept an infeasible one);
//   * cost of an on-time segment = exact L1 clumping under the cap
//     intervals, for the better of two orders (by ideal x, and Schrage's).
// Search = simulated annealing over relocate / swap moves between segments:
// phase 1 drives the total lateness to 0, phase 2 lowers the exact L1 cost
// while every segment stays on time.
#include "capsearch.hpp"
#include "global.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
#include <functional>

namespace {
constexpr double EPS = 1e-9;
constexpr double INF = 1e300;

struct Rng {   // xorshift64*
    uint64_t s;
    explicit Rng(uint64_t x) : s(x * 0x9e3779b97f4a7c15ULL + 0x632be59bd9b4e019ULL) { if (!s) s = 1; }
    uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 0x2545F4914F6CDD1DULL; }
    int below(int n) { return int((next() >> 33) % uint64_t(n)); }
    double u01() { return double(next() >> 11) * 0x1.0p-53; }
};

class Repair {
public:
    Repair(const PlaceDB& db, const Placement& p, double D) : db_(db), D_(D) {
        list.assign(db.segs.size(), {});
        seg.assign(db.nodes.size(), -1);
        home_.assign(db.nodes.size(), -1);
        homeX_.assign(db.nodes.size(), 0);
        for (int v : db.movable) {
            seg[v] = home_[v] = p.seg[v];
            homeX_[v] = p.x(db, v) + 0.5 * db.nodes[v].w;
            list[p.seg[v]].push_back(v);
        }
        late.assign(db.segs.size(), 0);
        for (size_t s = 0; s < list.size(); ++s)
            if (!list[s].empty()) late[s] = lateness(int(s), list[s]);
        reach_.resize(db.nodes.size());
        have_.assign(db.nodes.size(), 0);
    }

    // Start sites of v in segment s with displacement <= D: [lo, hi].
    // false: v never fits s (height / width). pen > 0: no site is close
    // enough; lo = hi = the nearest site and pen = how far (in sites) it is off.
    bool window(int v, int s, double& lo, double& hi, double& pen) const {
        const Segment& g = db_.segs[s];
        const Node& n = db_.nodes[v];
        int w = db_.sitesOf(v, g);
        pen = 0;
        if (w > g.numSites || std::fabs(g.height - n.h) > 1e-6) return false;
        double reach = (D_ - std::fabs(g.y - n.gy)) / g.sp;
        if (reach < 0) { pen = -reach; reach = 0; }
        double ideal = (n.gx - g.x0) / g.sp;
        lo = std::max(0.0, std::ceil(ideal - reach - 1e-9));
        hi = std::min(double(g.numSites - w), std::floor(ideal + reach + 1e-9));
        if (lo > hi) {
            double c = std::clamp(std::round(ideal), 0.0, double(g.numSites - w));
            pen += std::max(1e-3, std::fabs(c - ideal) - reach);
            lo = hi = c;
        }
        return true;
    }

    // Order key that keeps the incumbent order: a cell in its incumbent
    // segment sorts by its incumbent center, a newcomer by its ideal center.
    double key(int v, int s) const {
        return s == home_[v] ? homeX_[v] : db_.nodes[v].gx + 0.5 * db_.nodes[v].w;
    }

    // Total lateness (sites) of segment s with cells l: the smaller of
    // Schrage's order and the incumbent-like order (by key). The incumbent
    // order matters where the window DP chose a non-trivial order.
    double lateness(int s, const std::vector<int>& l, std::vector<int>* order = nullptr) const {
        double a = schrage(s, l, order);
        if (a <= EPS) return a;
        // the incumbent order, repaired locally: near-legal inputs (e01) overlap
        // so much that only the order the incumbent already has fits
        double b = repaired(s, l, &keyed_);
        if (b < a) { a = b; if (order) *order = keyed_; }
        if (a <= EPS) return a;
        double c = hybrid(s, l, &keyed_);
        if (c < a) { a = c; if (order) *order = keyed_; }
        if (a <= EPS) return a;
        double d = perturbed(s, l, &keyed_);
        if (d < a) { a = d; if (order) *order = keyed_; }
        return a;
    }

    // The incumbent order (newcomers by ideal center), repaired locally.
    double repaired(int s, const std::vector<int>& l, std::vector<int>* order) const {
        rk_ = l;
        std::sort(rk_.begin(), rk_.end(), [&](int x, int y) {
            double kx = key(x, s), ky = key(y, s);
            return kx != ky ? kx < ky : x < y;
        });
        double c = repairOrder(s, rk_);
        if (order) *order = rk_;
        return c;
    }

    // Local repair of an order: the leftmost schedule; the first late cell
    // trades places with the cell before it while its deadline is earlier, and
    // so on. Only the order around the problem changes. Returns the lateness.
    double repairOrder(int s, std::vector<int>& o) const {
        const Segment& g = db_.segs[s];
        int n = int(o.size());
        rlo_.resize(n); rhi_.resize(n); rw_.resize(n); rend_.assign(n + 1, 0);
        double c = 0;
        for (int j = 0; j < n; ++j) {
            double lo, hi, pen;
            if (!window(o[j], s, lo, hi, pen)) return INF;
            c += pen;
            rlo_[j] = lo; rhi_[j] = hi; rw_[j] = db_.sitesOf(o[j], g);
        }
        int swaps = 0, i = 0;
        while (i < n) {
            double x = std::max(rend_[i], rlo_[i]);
            if (x > rhi_[i] + EPS && i > 0 && swaps < 4 * n && rhi_[i] + rw_[i] < rhi_[i - 1] + rw_[i - 1] - EPS) {
                std::swap(o[i], o[i - 1]); std::swap(rlo_[i], rlo_[i - 1]);
                std::swap(rhi_[i], rhi_[i - 1]); std::swap(rw_[i], rw_[i - 1]);
                ++swaps;
                --i;
                continue;
            }
            if (x > rhi_[i]) c += x - rhi_[i];
            rend_[i + 1] = x + rw_[i];
            ++i;
        }
        return c;
    }

    // Minimal perturbation: the cells that were in s keep their incumbent
    // order; every newcomer goes to the position near its ideal rank that
    // leaves the least lateness, then the local repair. In a hotspot the
    // window DP's order is far from any simple order (h06: one segment 17,749
    // in the incumbent order, 32,898 in ideal order), so a single newcomer
    // must not reorder the whole segment.
    double perturbed(int s, const std::vector<int>& l, std::vector<int>* order) const {
        pk_.clear();
        nk_.clear();
        for (int v : l) (home_[v] == s ? pk_ : nk_).push_back(v);
        std::sort(pk_.begin(), pk_.end(), [&](int x, int y) { return homeX_[x] != homeX_[y] ? homeX_[x] < homeX_[y] : x < y; });
        auto ic = [&](int v) { return db_.nodes[v].gx + 0.5 * db_.nodes[v].w; };
        std::sort(nk_.begin(), nk_.end(), [&](int x, int y) { return ic(x) != ic(y) ? ic(x) < ic(y) : x < y; });
        for (int u : nk_) {
            // rank by incumbent / ideal center, then try a few places around it
            double cu = ic(u);
            int r = int(std::lower_bound(pk_.begin(), pk_.end(), cu, [&](int v, double x) { return key(v, s) < x; }) - pk_.begin());
            int bestPos = r;
            double bestLate = INF;
            for (int pos = std::max(0, r - 6); pos <= std::min(int(pk_.size()), r + 6); ++pos) {
                tmp_ = pk_;
                tmp_.insert(tmp_.begin() + pos, u);
                double late = greedy(s, tmp_) + 1e-6 * std::abs(pos - r);
                if (late < bestLate) { bestLate = late; bestPos = pos; }
            }
            pk_.insert(pk_.begin() + bestPos, u);
        }
        double c = repairOrder(s, pk_);
        if (order) *order = pk_;
        return c;
    }

    // Incumbent-like order that gives way only when it must: among the cells
    // that may start, the one with the smallest key, unless another of them
    // would then start after its latest start (then the most urgent one).
    // Follows the incumbent order where there is slack, Schrage where tight.
    double hybrid(int s, const std::vector<int>& l, std::vector<int>* order) const {
        const Segment& g = db_.segs[s];
        items_.clear();
        double c = 0;
        for (int v : l) {
            double lo, hi, pen;
            if (!window(v, s, lo, hi, pen)) return INF;
            c += pen;
            items_.push_back({lo, key(v, s), double(db_.sitesOf(v, g)), hi, double(v)});
        }
        std::sort(items_.begin(), items_.end());   // by release
        using P = std::pair<double, int>;
        auto gt = [](const P& a, const P& b) { return a.first > b.first; };
        byKey_.clear();
        byHi_.clear();
        done_.assign(items_.size(), 0);
        size_t i = 0, placed = 0, open = 0;
        double end = 0;
        if (order) order->clear();
        while (placed < items_.size()) {
            if (open == 0 && items_[i][0] > end) end = items_[i][0];
            while (i < items_.size() && items_[i][0] <= end + EPS) {
                byKey_.push_back({items_[i][1], int(i)}); std::push_heap(byKey_.begin(), byKey_.end(), gt);
                byHi_.push_back({items_[i][3], int(i)}); std::push_heap(byHi_.begin(), byHi_.end(), gt);
                ++i; ++open;
            }
            while (done_[byKey_.front().second]) { std::pop_heap(byKey_.begin(), byKey_.end(), gt); byKey_.pop_back(); }
            while (done_[byHi_.front().second]) { std::pop_heap(byHi_.begin(), byHi_.end(), gt); byHi_.pop_back(); }
            int k = byKey_.front().second, u = byHi_.front().second;
            int j = (u != k && items_[u][3] < end + items_[k][2] - EPS) ? u : k;
            done_[j] = 1;
            --open;
            ++placed;
            if (end > items_[j][3]) c += end - items_[j][3];
            end += items_[j][2];
            if (order) order->push_back(int(items_[j][4]));
        }
        return c;
    }

    // Lateness of a fixed order: leftmost schedule (minimizes every position).
    double greedy(int s, const std::vector<int>& o) const {
        const Segment& g = db_.segs[s];
        double c = 0, end = 0;
        for (int v : o) {
            double lo, hi, pen;
            if (!window(v, s, lo, hi, pen)) return INF;
            c += pen;
            double x = std::max(end, lo);
            if (x > hi) c += x - hi;
            end = x + db_.sitesOf(v, g);
        }
        return c;
    }

    // Lateness in Schrage's order (non-delay, earliest deadline first).
    double schrage(int s, const std::vector<int>& l, std::vector<int>* order) const {
        const Segment& g = db_.segs[s];
        items_.clear();
        double c = 0;
        for (int v : l) {
            double lo, hi, pen;
            if (!window(v, s, lo, hi, pen)) return INF;
            c += pen;
            double w = db_.sitesOf(v, g);
            items_.push_back({lo, hi + w, w, hi, double(v)});
        }
        std::sort(items_.begin(), items_.end());   // by release
        heap_.clear();
        auto later = [](const std::pair<double, int>& a, const std::pair<double, int>& b) { return a.first > b.first; };
        size_t i = 0;
        double end = 0;
        if (order) order->clear();
        while (i < items_.size() || !heap_.empty()) {
            if (heap_.empty() && items_[i][0] > end) end = items_[i][0];
            while (i < items_.size() && items_[i][0] <= end + EPS) {
                heap_.push_back({items_[i][1], int(i)});
                std::push_heap(heap_.begin(), heap_.end(), later);
                ++i;
            }
            std::pop_heap(heap_.begin(), heap_.end(), later);
            int j = heap_.back().second;
            heap_.pop_back();
            if (end > items_[j][3]) c += end - items_[j][3];
            end += items_[j][2];
            if (order) order->push_back(int(items_[j][4]));
        }
        return c;
    }

    // Exact L1 displacement of segment s (INF if it is late): the best of
    // Schrage's order, the hybrid, the locally repaired incumbent order and the
    // order by ideal center (each only if it is on time). Fills order / sites.
    double cost(int s, const std::vector<int>& l, std::vector<int>* order = nullptr,
                std::vector<int>* sites = nullptr) const {
        std::vector<int> cand[5];
        double lat[5];
        lat[4] = perturbed(s, l, &cand[4]);
        lat[0] = schrage(s, l, &cand[0]);
        lat[1] = hybrid(s, l, &cand[1]);
        lat[2] = repaired(s, l, &cand[2]);
        if (cand[2] == cand[1]) lat[2] = INF;
        cand[3] = l;
        std::sort(cand[3].begin(), cand[3].end(), [&](int x, int y) {
            double cx = db_.nodes[x].gx + 0.5 * db_.nodes[x].w, cy = db_.nodes[y].gx + 0.5 * db_.nodes[y].w;
            return cx != cy ? cx < cy : x < y;
        });
        lat[3] = (cand[3] == cand[2] || cand[3] == cand[1]) ? INF : greedy(s, cand[3]);
        if (cand[0] == cand[1]) lat[0] = INF;
        double best = INF;
        int bi = -1;
        std::vector<int> st, bestSites;
        for (int i : {4, 0, 1, 2, 3}) {
            if (lat[i] > EPS) continue;
            if (i != 4 && cand[i] == cand[4] && lat[4] <= EPS) continue;
            double c = clump(s, cand[i], sites ? &st : nullptr);
            if (c < best) { best = c; bi = i; if (sites) bestSites = st; }
        }
        if (bi < 0) return INF;
        if (order) *order = cand[bi];
        if (sites) *sites = bestSites;
        return best;
    }

    // Segments a cell can be in under D (lazily).
    const std::vector<int>& targets(int v) {
        if (have_[v]) return reach_[v];
        have_[v] = 1;
        const Node& n = db_.nodes[v];
        auto first = std::lower_bound(db_.rows.begin(), db_.rows.end(), n.gy - D_ - 1e-9,
                                      [](const Row& r, double y) { return r.y < y; });
        for (auto row = first; row != db_.rows.end() && row->y <= n.gy + D_ + 1e-9; ++row)
            for (int s : row->segs) {
                const Segment& g = db_.segs[s];
                double rx = D_ - std::fabs(g.y - n.gy);
                if (n.gx + rx < g.x0 - 1e-9 || n.gx - rx > g.x0 + g.numSites * g.sp - n.w + 1e-9) continue;
                double lo, hi, pen;
                if (window(v, s, lo, hi, pen) && pen == 0) reach_[v].push_back(s);
            }
        return reach_[v];
    }

    std::vector<std::vector<int>> list;   // per segment: its cells (unordered)
    std::vector<int> seg;                 // per node
    std::vector<double> late;             // per segment

private:
    struct Cluster { int first, last, width, lo, hi, x; };

    // L1 clumping for a fixed order under the cap intervals (exact, discrete).
    double clump(int s, const std::vector<int>& o, std::vector<int>* sites) const {
        const Segment& g = db_.segs[s];
        int n = int(o.size());
        width_.resize(n); lo_.resize(n); hi_.resize(n); ideal_.resize(n);
        double dy = 0;
        for (int j = 0; j < n; ++j) {
            double a = 0, b = 0, pen = 0;
            if (!window(o[j], s, a, b, pen)) return INF;   // cannot happen: callers check lateness first
            width_[j] = db_.sitesOf(o[j], g);
            lo_[j] = int(a);
            hi_[j] = int(b);
            ideal_[j] = (db_.nodes[o[j]].gx - g.x0) / g.sp;
            dy += std::fabs(g.y - db_.nodes[o[j]].gy);
        }
        stack_.clear();
        auto place = [&](Cluster& c) {
            pts_.clear();
            int pre = 0;
            for (int j = c.first; j <= c.last; ++j) { pts_.push_back(ideal_[j] - pre); pre += width_[j]; }
            auto mid = pts_.begin() + (pts_.size() - 1) / 2;
            std::nth_element(pts_.begin(), mid, pts_.end());
            // the rounded median need not be the discrete optimum: compare both neighbours
            int a = int(std::clamp(std::floor(*mid), double(c.lo), double(c.hi)));
            int b = int(std::clamp(std::ceil(*mid), double(c.lo), double(c.hi)));
            double ca = 0, cb = 0;
            for (double t : pts_) { ca += std::fabs(a - t); cb += std::fabs(b - t); }
            c.x = cb < ca ? b : a;
        };
        for (int j = 0; j < n; ++j) {
            Cluster c{j, j, width_[j], lo_[j], hi_[j], 0};
            place(c);
            while (!stack_.empty() && stack_.back().x + stack_.back().width > c.x) {
                Cluster b = stack_.back();
                stack_.pop_back();
                c = {b.first, c.last, b.width + c.width, std::max(b.lo, c.lo - b.width), std::min(b.hi, c.hi - b.width), 0};
                if (c.lo > c.hi) return INF;
                place(c);
            }
            stack_.push_back(c);
        }
        double tot = dy;
        if (sites) sites->assign(n, 0);
        for (const Cluster& c : stack_) {
            int x = c.x;
            for (int j = c.first; j <= c.last; ++j) {
                tot += std::fabs(x - ideal_[j]) * g.sp;
                if (sites) (*sites)[j] = x;
                x += width_[j];
            }
        }
        return tot;
    }

    const PlaceDB& db_;
    double D_;
    std::vector<std::vector<int>> reach_;
    std::vector<char> have_;
    std::vector<int> home_;
    std::vector<double> homeX_;
    mutable std::vector<int> keyed_, rk_, pk_, nk_, tmp_;
    mutable std::vector<double> rlo_, rhi_, rw_, rend_;
    mutable std::vector<std::pair<double, int>> byKey_, byHi_;
    mutable std::vector<char> done_;
    mutable std::vector<std::array<double, 5>> items_;
    mutable std::vector<std::pair<double, int>> heap_;
    mutable std::vector<int> width_, lo_, hi_;
    mutable std::vector<double> ideal_, pts_;
    mutable std::vector<Cluster> stack_;
};

// v goes from s1 to s2; the cells `back` of s2 go to s1 (none: relocate,
// one: swap, several: a wide cell trades places with a run of narrow ones)
struct Trial {
    int v = -1, s1 = -1, s2 = -1;
    std::vector<int> back, l1, l2;
};

bool propose(const PlaceDB& db, Repair& R, Rng& rng, int v, Trial& t) {
    t.v = v;
    t.s1 = R.seg[v];
    const auto& tv = R.targets(v);
    if (tv.empty()) return false;
    t.s2 = tv[rng.below(int(tv.size()))];
    if (t.s2 == t.s1) return false;
    const auto& l2 = R.list[t.s2];
    t.back.clear();
    double r = rng.u01();
    double lo, hi, pen;
    if (!l2.empty() && r < 0.35) {   // swap with one cell
        int u = l2[rng.below(int(l2.size()))];
        if (R.window(u, t.s1, lo, hi, pen) && pen == 0) t.back.push_back(u);
    } else if (l2.size() >= 2 && r < 0.6) {
        // block swap: the k cells of s2 whose centers are nearest v's ideal center
        double cv = db.nodes[v].gx + 0.5 * db.nodes[v].w;
        int k = 2 + rng.below(3);
        std::vector<std::pair<double, int>> near;
        near.reserve(l2.size());
        for (int u : l2) {
            double cu = db.nodes[u].gx + 0.5 * db.nodes[u].w;   // the run is chosen by ideal x
            near.push_back({std::fabs(cu - cv), u});
        }
        k = std::min<int>(k, int(near.size()));
        std::partial_sort(near.begin(), near.begin() + k, near.end());
        for (int j = 0; j < k; ++j) {
            int u = near[j].second;
            if (!R.window(u, t.s1, lo, hi, pen) || pen > 0) return false;
            t.back.push_back(u);
        }
    }
    t.l1 = R.list[t.s1];
    t.l1.erase(std::find(t.l1.begin(), t.l1.end(), v));
    t.l2 = l2;
    t.l2.push_back(v);
    for (int u : t.back) {
        t.l2.erase(std::find(t.l2.begin(), t.l2.end(), u));
        t.l1.push_back(u);
    }
    return true;
}

void apply(Repair& R, Trial& t) {
    R.list[t.s1].swap(t.l1);
    R.list[t.s2].swap(t.l2);
    R.seg[t.v] = t.s2;
    for (int u : t.back) R.seg[u] = t.s1;
}

}  // namespace

bool rowRepair(const PlaceDB& db, Placement& p, double D, const RepairOptions& opt) {
    const double until = opt.phase1Until;
    const bool verbose = opt.verbose;
    if (db.movable.empty()) return true;
    Repair R(db, p, D);
    Rng rng(opt.seed);
    // a cell over the cap with no segment in reach can never be repaired
    for (int v : db.movable)
        if (displacementOf(db, p, v) > D + EPS && R.targets(v).empty()) return false;

    // ---- phase 1: total lateness -> 0
    std::vector<int> bad;
    double cur = 0;
    for (double x : R.late) cur += x;
    const double start = cur;
    // temperature in sites: a move that pushes a row by about half a cell
    // width must stay acceptable early on (narrow-site designs: e06 has
    // 1-site columns and 28-site cells)
    double avgW = 0;
    for (int v : db.movable) avgW += db.sitesOf(v, db.segs[p.seg[v]]);
    avgW /= double(db.movable.size());
    const double T0 = std::max(1.0, 0.5 * avgW);
    double T = T0;
    const double t1 = wallSeconds();
    std::vector<int> over;
    long it = 0;
    Trial t;
    while (cur > EPS) {
        if ((it & 255) == 0) {
            if (wallSeconds() >= until) break;
            bad.clear();
            cur = 0;
            for (size_t s = 0; s < R.late.size(); ++s) {
                cur += R.late[s];
                if (R.late[s] > EPS) bad.push_back(int(s));
            }
            if (bad.empty()) break;
            // time-based: three cool-downs T0 -> 0.05 per slice (long rows do
            // few moves per second, an iteration-based schedule never cools)
            double frac = std::fmod(3.0 * (wallSeconds() - t1) / std::max(1e-3, until - t1), 1.0);
            T = T0 * std::pow(0.05 / T0, frac);
        }
        ++it;
        int s1 = bad[rng.below(int(bad.size()))];
        if (R.late[s1] <= EPS || R.list[s1].empty()) continue;
        // half of the time, a cell that is over the cap wherever it is now
        int v = -1;
        if (rng.u01() < 0.5) {
            over.clear();
            double lo, hi, pen;
            for (int c : R.list[s1])
                if (R.window(c, s1, lo, hi, pen) && pen > 0) over.push_back(c);
            if (!over.empty()) v = over[rng.below(int(over.size()))];
        }
        if (v < 0) v = R.list[s1][rng.below(int(R.list[s1].size()))];
        if (!propose(db, R, rng, v, t)) continue;
        double c1 = R.lateness(t.s1, t.l1), c2 = R.lateness(t.s2, t.l2);
        double d = c1 + c2 - R.late[t.s1] - R.late[t.s2];
        if (d <= 0 || rng.u01() < std::exp(-d / T)) {
            R.late[t.s1] = c1;
            R.late[t.s2] = c2;
            int s2 = t.s2;
            apply(R, t);
            cur += d;
            if (c2 > EPS) bad.push_back(s2);
        }
    }
    cur = 0;
    for (double x : R.late) cur += x;
    if (verbose)
        std::fprintf(stderr, "[cap] D %.6f: lateness %.3f -> %.3f in %ld moves  %.2fs\n", D, start, cur, it, wallSeconds());
    if (cur > EPS) return false;

    diag::member("phase1_membership",R.list);
    // ---- cleanup: phase 1 wanders (it has to climb out of full rows); send
    // every cell that does not need to be away back to its incumbent segment
    // when both segments stay on time, so that phase 2 and the placement only
    // touch what the new cap really needs.
    long back = 0;
    for (int round = 0; round < 4; ++round) {
        long got = 0;
        for (int v : db.movable) {
            if (wallSeconds() >= opt.phase2Until) break;
            int home = p.seg[v], at = R.seg[v];
            if (home == at) continue;
            double lo, hi, pen;
            if (!R.window(v, home, lo, hi, pen) || pen > 0) continue;
            t.l1 = R.list[at];
            t.l1.erase(std::find(t.l1.begin(), t.l1.end(), v));
            t.l2 = R.list[home];
            t.l2.push_back(v);
            double c1 = R.lateness(at, t.l1), c2 = R.lateness(home, t.l2);
            if (c1 > EPS || c2 > EPS) continue;
            R.list[at].swap(t.l1);
            R.list[home].swap(t.l2);
            R.seg[v] = home;
            R.late[at] = c1;
            R.late[home] = c2;
            ++got;
        }
        back += got;
        if (!got) break;
    }
    long away = 0;
    for (int v : db.movable) away += R.seg[v] != p.seg[v];
    if (verbose)
        std::fprintf(stderr, "[cap] D %.6f: cleanup sent %ld cells home, %ld stay away  %.2fs\n", D, back, away, wallSeconds());

    diag::member("cleanup_membership",R.list);
    // ---- phase 2: lower the exact L1 cost of the touched segments, on time
    std::vector<char> dirty(R.list.size(), 0), inPool(db.nodes.size(), 0);
    std::vector<int> pool;
    for (int v : db.movable)
        if (R.seg[v] != p.seg[v] || displacementOf(db, p, v) > D + EPS) dirty[R.seg[v]] = dirty[p.seg[v]] = 1;
    for (size_t s = 0; s < R.list.size(); ++s)
        if (dirty[s])
            for (int v : R.list[s])
                if (!inPool[v]) { inPool[v] = 1; pool.push_back(v); }
    std::vector<double> dc(R.list.size(), -1);
    auto segCost = [&](int s) { if (dc[s] < 0) dc[s] = R.cost(s, R.list[s]); return dc[s]; };
    double sum = 0;
    for (int v : pool) sum += displacementOf(db, p, v);
    const double T2 = pool.empty() ? 0 : opt.phase2Temp * sum / double(pool.size());
    const long n2 = opt.phase2PerCell * long(pool.size());
    long acc2 = 0;
    double gain = 0;
    for (long k = 0; k < n2; ++k) {
        if ((k & 255) == 0 && wallSeconds() >= opt.phase2Until) break;
        double temp = T2 * (1.0 - double(k) / double(n2));
        int v = pool[rng.below(int(pool.size()))];
        if (!propose(db, R, rng, v, t)) continue;
        double c1 = R.cost(t.s1, t.l1);
        if (c1 >= INF) continue;
        double c2 = R.cost(t.s2, t.l2);
        if (c2 >= INF) continue;
        double d = c1 + c2 - segCost(t.s1) - segCost(t.s2);
        if (d <= 0 || (temp > 0 && rng.u01() < std::exp(-d / temp))) {
            dc[t.s1] = c1;
            dc[t.s2] = c2;
            dirty[t.s1] = dirty[t.s2] = 1;
            int s2 = t.s2;
            apply(R, t);
            ++acc2;
            gain += d;
            for (int c : R.list[s2])
                if (!inPool[c]) { inPool[c] = 1; pool.push_back(c); }
        }
    }

    diag::member("phase2_membership",R.list);
    // ---- place the segments whose membership changed or that hold a cell
    // over the cap; every other segment keeps its incumbent positions
    std::fill(dirty.begin(), dirty.end(), 0);
    for (int v : db.movable)
        if (R.seg[v] != p.seg[v] || displacementOf(db, p, v) > D + EPS) dirty[R.seg[v]] = dirty[p.seg[v]] = 1;
    Placement q = p;
    std::vector<int> order, sites;
    for (size_t s = 0; s < R.list.size(); ++s) {
        if (!dirty[s]) continue;
        if (R.cost(int(s), R.list[s], &order, &sites) >= INF) return false;   // cannot happen: on time
        for (size_t j = 0; j < order.size(); ++j) {
            q.seg[order[j]] = int(s);
            q.site[order[j]] = sites[j];
        }
    }
    if (!checkLegal(db, q).empty()) return false;
    DispStats d = displacement(db, q);
    if (d.max > D + 1e-6) return false;
    if (verbose)
        std::fprintf(stderr, "[cap] D %.6f: phase 2 pool %zu, %ld accepted (%+.3f), placed: total %.4f max %.6f  %.2fs\n", D,
                     pool.size(), acc2, gain, d.total, d.max, wallSeconds());
    p = std::move(q);
    return true;
}

double individualMaxBound(const PlaceDB& db) {
    double bound = 0;
    for (int v : db.movable) {
        const Node& n = db.nodes[v];
        double best = INF;
        int r0 = db.nearestRow(n.gy);
        // rows outward from the nearest one until the vertical distance alone is worse
        for (int dir : {0, 1}) {
            for (int r = dir ? r0 + 1 : r0; r >= 0 && r < int(db.rows.size()); r += dir ? 1 : -1) {
                double dy = std::fabs(db.rows[r].y - n.gy);
                if (dy >= best) break;
                for (int s : db.rows[r].segs) {
                    const Segment& g = db.segs[s];
                    int w = db.sitesOf(v, g);
                    if (w > g.numSites || std::fabs(g.height - n.h) > 1e-6) continue;
                    int x = std::clamp(int(std::lround((n.gx - g.x0) / g.sp)), 0, g.numSites - w);
                    // the rounded site may not be the nearest: check its neighbours too
                    for (int k = std::max(0, x - 1); k <= std::min(g.numSites - w, x + 1); ++k)
                        best = std::min(best, std::fabs(g.x0 + k * g.sp - n.gx) + dy);
                }
            }
        }
        if (best < INF) bound = std::max(bound, best);
    }
    return bound;
}

namespace {
// One repair at cap D from `from`: the row reassignment first, then
// ChainDP's site-level repair (ordered packing, greedy rebuild, DFS, site
// congestion negotiation). They succeed on different instances: the row
// reassignment on structural ones (toy, e06, e15), the negotiation on dense
// ones that a ripple of single-cell moves resolves (e03, e12, h04, h07).
// how: 1 = rows, 2 = ChainDP's repair.
bool repairAt(const PlaceDB& db, const Placement& from, double D, double until, long phase2PerCell, double phase2Temp,
              uint64_t seed, bool verbose, Placement& out, int& how) {
    double now = wallSeconds();
    RepairOptions ro;
    ro.phase1Until = now + 0.6 * (until - now);
    ro.phase2Until = until;
    ro.phase2PerCell = phase2PerCell;
    ro.phase2Temp = phase2Temp;
    ro.seed = seed;
    ro.verbose = verbose;
    out = from;
    double rt=wallSeconds(); bool rok=rowRepair(db,out,D,ro); diag::current.rowSec=wallSeconds()-rt;
    if (rok) { how = 1; auto d=displacement(db,out);diag::current.rawTotal=d.total;diag::current.rawMax=d.max;return true; }
    out = from;
    double st=wallSeconds(); bool sok=wallSeconds()<until && repairCap(db,out,D,until,unsigned(seed));diag::current.siteSec=wallSeconds()-st;
    if (sok) {how=2;auto d=displacement(db,out);diag::current.rawTotal=d.total;diag::current.rawMax=d.max;return true;}
    out = from;
    how = 0;
    return false;
}
}  // namespace

CapSearchResult capSearch(const PlaceDB& db, Placement& p, const CapSearchOptions& opt, WindowMemo* memo) {
    diag::attempts.clear();diag::candidates.clear();
    CapSearchResult res;
    const double t0 = wallSeconds(), until = t0 + opt.seconds;
    const DispStats base = displacement(db, p);
    res.startMax = res.endMax = base.max;
    res.startTotal = res.endTotal = base.total;
    if (db.movable.empty() || opt.seconds <= 0 || base.max <= EPS) return res;
    const double lb = individualMaxBound(db);
    const double n = double(db.movable.size());
    // a probe only runs phase 1 (+ cleanup and placement); a full attempt adds
    // phase 2 and the window DP. An unresolved probe costs its whole slice.
    const double probeSlice = 0.08 + 0.5e-5 * n, fullSlice = 0.3 + 2e-5 * n;
    Placement best = p, q;
    double M = base.max, total = base.total;
    WindowMemo local;
    if (!memo) memo = &local;
    uint64_t seed = 1;
    int how = 0;
    auto left = [&] { return until - wallSeconds(); };
    int idle = 0;
    for (int round = 0; round < 12 && left() > 0; ++round) {
        // ---- A: the lowest cap the repair reaches from `best` (at most 35 % of
        // what is left; ChainDP's first try 0.9 x max, then bisection)
        const double aUntil = wallSeconds() + 0.35 * left();
        double lo = lb, hi = M, feas = M;
        double D = std::max(lb, 0.9 * M);
        for (int k = 0; k < 8 && wallSeconds() < aUntil; ++k) {
            ++res.attempts;
            diag::begin(db,best,D,round,seed,"probe");diag::current.limit=opt.totalRatio*base.total;
            bool ok = repairAt(db, best, D, std::min(aUntil, wallSeconds() + probeSlice), 0, opt.phase2Temp, seed++,
                               false, q, how);
            if (ok) {
                ++res.feasible;
                feas = std::min(feas, displacement(db, q).max);
                hi = std::min(D, feas);
            } else {
                lo = D;
            }
            diag::finish(db,q,ok,false,how);
            if (opt.verbose)
                std::fprintf(stderr, "[cap] probe D %.6f: %s  %.2fs\n", D,
                             ok ? (how == 1 ? "feasible (rows)" : "feasible (sites)") : "unresolved", wallSeconds());
            if (hi - lo <= 2e-3 * M) break;
            D = 0.5 * (lo + hi);
        }
        if (feas >= M - EPS) {
            if (++idle >= 3) break;
            continue;
        }
        // ---- B: bisection over [feas, M) for the lowest cap whose total after
        // phase 2 + DP fits the budget. It starts a quarter of the way down to
        // `feas`: the lowest reachable cap usually costs too much total (e06:
        // 23.76 vs 41.72, +1.7 %), and after the window DP many cells sit
        // exactly at the cap (h06: ~100 cells within 1 % of it), so big steps
        // rarely fit the total budget. (Tried: falling back to "the k worst
        // cells" steps when the first step fails: steps too small, 5 vs 6 of
        // 11 tuning cases at ChainDP's level, tune/t5_e6_r1005.)
        bool improved = false;
        double blo = feas, bhi = M;
        D = M - 0.25 * (M - feas);
        for (int t = 0; t < 5 && left() > 0; ++t) {
            ++res.attempts;
            // phase 2 only on small designs: on big ones its moves cost more total
            // after the window DP than they save (h06: +0.45 % vs -0.09 % without)
            long p2 = db.movable.size() <= 3000 ? opt.phase2PerCell : 0;
            diag::begin(db,best,D,round,seed,"full");diag::current.limit=opt.totalRatio*base.total;
            bool ok = repairAt(db, best, D, wallSeconds() + std::min(0.5 * left(), fullSlice), p2,
                               opt.phase2Temp, seed++, false, q, how);
            DispStats d{};
            bool accept = false;
            if (ok) {
                ++res.feasible;
                // standard windows while searching (the small designs' 5 / 4-cell
                // windows cost up to 3^12 subsets each); the full DP runs once
                // on the final placement below
                WindowOptions w = opt.dp;
                w.perRow = std::min(w.perRow, 4);
                w.perRow3 = std::min(w.perRow3, 3);
                w.threads = opt.threads;
                w.verbose = 0;
                w.timeLimit = std::max(0.05, std::min(left(), std::max(0.5, 0.4 * opt.seconds)));
                if(!diag::prefix().empty())diag::capture(best,q,*memo);
                double dt=wallSeconds();windowSweeps(db, q, w, memo);diag::current.dpSec=wallSeconds()-dt;
                d = displacement(db, q);
                accept = checkLegal(db, q).empty() && d.max < M - EPS && d.total <= opt.totalRatio * base.total + 1e-6;
            }
            diag::finish(db,q,ok,accept,how);
            if (opt.verbose)
                std::fprintf(stderr, "[cap] D %.6f: %s%s, total %.4f max %.6f  %.2fs\n", D,
                             ok ? (how == 1 ? "feasible (rows)" : "feasible (sites)") : "unresolved",
                             ok ? (accept ? ", accepted" : ", rejected (total budget)") : "", ok ? d.total : 0.0,
                             ok ? d.max : 0.0, wallSeconds());
            if (accept) {
                best = q;
                M = d.max;
                total = d.total;
                ++res.accepted;
                improved = true;
                bhi = M;
            } else {
                blo = D;
            }
            if (bhi - blo <= 1e-3 * bhi) break;
            D = 0.5 * (blo + bhi);
        }
        // a round without progress is retried with new seeds (the repair is
        // randomized); three in a row end the search
        if (improved) idle = 0;
        else if (++idle >= 3) break;
    }
    if (res.accepted && (opt.dp.perRow > 4 || opt.dp.perRow3 > 3)) {
        WindowOptions w = opt.dp;
        w.threads = opt.threads;
        w.verbose = 0;
        w.timeLimit = std::max(0.2, left());
        windowSweeps(db, best, w, memo);   // never raises total or max
        total = displacement(db, best).total;
    }
    p = std::move(best);
    res.endMax = M;
    res.endTotal = total;
    res.seconds = wallSeconds() - t0;
    if (opt.verbose)
        std::fprintf(stderr, "[cap] %d attempts, %d feasible, %d accepted: max %.6f -> %.6f, total %.4f -> %.4f (%+.3f%%)  %.2fs\n",
                     res.attempts, res.feasible, res.accepted, res.startMax, res.endMax, res.startTotal, res.endTotal,
                     100 * (res.endTotal / std::max(EPS, res.startTotal) - 1), res.seconds);
    diag::flush(db);
    return res;
}

// Materialize each recorded membership only AFTER repair timing has ended.
// These intermediate coordinates are counterfactual, not executed moves.
void diagnosticMaterialize(const PlaceDB& db,const Placement& base,double D,const std::string& out) {
 std::ofstream f(out+"_stages.csv");f<<std::setprecision(15)<<"stage,legal,total,max,away,dirty_segments\n";
 for(auto& snap:diag::memberships){Repair R(db,base,D);R.list=snap.second;std::vector<char> dirty(db.segs.size(),0);int away=0;
 for(size_t s=0;s<R.list.size();++s)for(int v:R.list[s]){R.seg[v]=s;if(int(s)!=base.seg[v]||displacementOf(db,base,v)>D+1e-9)dirty[s]=dirty[base.seg[v]]=1;away+=int(s)!=base.seg[v];}
 Placement p=base;bool ok=true;std::vector<int> order,sites;
 for(size_t s=0;s<R.list.size();++s)if(dirty[s]){if(R.cost(s,R.list[s],&order,&sites)>=1e300){ok=false;break;}for(size_t j=0;j<order.size();++j){p.seg[order[j]]=s;p.site[order[j]]=sites[j];}}
 if(ok){auto d=displacement(db,p);f<<snap.first<<','<<checkLegal(db,p).empty()<<','<<d.total<<','<<d.max<<','<<away<<','<<std::count(dirty.begin(),dirty.end(),1)<<'\n';diag::cells(db,base,p,D,out+"_"+snap.first+"_cells.csv");}
 }
}
