// annealer.cpp — Stage 2（Milestone 6-3：讓 SA 變好）
//
// 狀態：一個合法的 placement。每個 move 都設計成「做完一定還是合法」，
// 所以 cost 只有 HPWL，不需要 overlap penalty。
//
// 基本的 SA：move / swap、只重算受影響的 nets、T0 = -Δ̄ / ln(p0)、100 個溫度等比下降。
// 讓它變好（TUTORIAL 6-3）：
//   (A) optimal region：一半的 move 把目標點設在 a 的最佳區域（各 net 去掉 a 之後
//       bounding box 的中位數區間），另一半才是 window 裡的隨機點
//   (B) range limiter [VPR]：window 依接受率放大縮小，讓接受率接近 0.44
//   (C) 時間控制：跑完一個溫度就量速度，時間不夠就每個溫度少做幾步，溫度一樣降到底
//   (D) undo journal：記「上次最好之後，每個 cell 第一次被動到時的位置」，
//       結束時倒回去 → 拿到真正的最佳解，不用整份複製
//   (E) CSR：net → pins、cell → nets 攤平成一個陣列，並去掉不到 2 個 pin 的 net
#include "annealer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>

#include "rowmap.hpp"

namespace {

struct Rng {   // xoshiro256**：比 std::mt19937 快，SA 會呼叫上億次
    uint64_t s[4];
    explicit Rng(uint64_t seed) {
        for (auto& x : s) {
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
    double u01() { return (next() >> 11) * 0x1.0p-53; }             // [0, 1)
    double uniform(double a, double b) { return a + (b - a) * u01(); }
    int below(int n) { return (int)((next() >> 33) % (uint64_t)n); }  // [0, n)
};

// (E) 壓縮的鄰接表：所有清單接成一個陣列，start[i] ~ start[i+1] 是第 i 個清單。
// 比 vector<vector<int>> 少很多次 cache miss（SA 的內圈全部都在查這個）。
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

// 一個 move：1 個 cell 搬家，或 2 個 cell 交換。記的是「新位置」。
struct Move {
    int n = 0;
    int v[2], seg[2], site[2];
    bool reorder = false;   // 搬家而且離開自己的空位 → RowMap 要 remove + insert（不然 slide 就好）
};

class Annealer {
public:
    Annealer(const PlaceDB& db, Placement& P, const SAOptions& opt)
        : db_(db), P_(P), opt_(opt), rm_(db, P), rng_(opt.seed) {
        int N = (int)db.nodes.size();
        c_.resize(N);
        for (int v = 0; v < N; ++v) c_[v] = {P.x(db, v) + db.nodes[v].w / 2, P.y(db, v) + db.nodes[v].h / 2};
        for (const auto& net : db.nets) pins_.add(net);
        for (int v = 0; v < N; ++v) {   // 不到 2 個 pin 的 net 永遠是 0，不用算
            std::vector<int> keep;
            for (int n : db.nodeNets[v])
                if (db.nets[n].size() >= 2) keep.push_back(n);
            nets_.add(keep);
        }
        netH_.resize(db.nets.size());
        netStamp_.assign(db.nets.size(), 0);
        cellEpoch_.assign(N, -1);
        for (size_t n = 0; n < db.nets.size(); ++n) netH_[n] = netHPWL((int)n);
        resum();

        // 晶片範圍、最小 window
        xLo_ = 1e300; xHi_ = -1e300;
        for (const Segment& s : db.segs) {
            xLo_ = std::min(xLo_, s.x0);
            xHi_ = std::max(xHi_, s.x0 + s.numSites * s.sp);
        }
        yLo_ = db.rows.front().y;
        yHi_ = db.rows.back().y + db.rows.back().height;
        double avgW = 0;
        for (int v : db.movable) avgW += db.nodes[v].w;
        avgW /= std::max<size_t>(1, db.movable.size());
        minWinX_ = std::min(xHi_ - xLo_, opt.minWinCells * avgW);
        minWinY_ = std::min(yHi_ - yLo_, opt.minWinRows * db.rows.front().height);
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
        int steps = std::max(1, opt_.steps);
        double alpha = std::pow(opt_.tEndRatio, 1.0 / steps);
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
            // (C) 照目前的速度，剩下的時間還能做幾步？不夠就每個溫度少做一點，但溫度一樣降到底
            int left = steps - step - 1;
            double secs = elapsed();
            if (left > 0 && secs > 0) {
                double movesLeft = (opt_.timeLimit - secs) * (it / secs);
                perStep = std::max(1LL, std::min(perStep, (long long)(movesLeft / left)));
            }
            // (B) range limiter：接受率高 → window 放大；低 → 縮小 [Betz & Rose, VPR]
            double f = std::min(2.0, std::max(0.5, 1 - 0.44 + rate));
            winX_ = std::min(xHi_ - xLo_, std::max(minWinX_, winX_ * f));
            winY_ = std::min(yHi_ - yLo_, std::max(minWinY_, winY_ * f));
            resum();   // cur_ 一直 += d 會累積浮點誤差，每個溫度重新加總一次
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

    // 先把 cell 中心改到新位置，只重算它們的 nets，回傳 HPWL 變化
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
        for (int i = 0; i < m.n; ++i) {   // (D) undo journal：上次最好之後第一次被動到 → 記下原本的位置
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
        if (cur_ < best_ - 1e-6) {   // 新的最好：清空 journal，從這裡重新記
            best_ = cur_;
            ++epoch_;
            journal_.clear();
        }
    }

    // journal 從後往前倒回去，就回到 best 那一刻的 placement
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
    // (A) v 的 optimal region：v 的每條 net 去掉 v 之後的 bounding box，
    // 所有 box 的左右邊界取中位數區間（x、y 分開）。區間裡任何一點都讓 v 自己的 HPWL 最小。
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
        size_t k = xs_.size() / 2;   // 2m 個數的中位數區間 = 第 m 小 ~ 第 m+1 小
        std::nth_element(xs_.begin(), xs_.begin() + k, xs_.end());
        xh = xs_[k];
        xl = *std::max_element(xs_.begin(), xs_.begin() + k);
        std::nth_element(ys_.begin(), ys_.begin() + k, ys_.end());
        yh = ys_[k];
        yl = *std::max_element(ys_.begin(), ys_.begin() + k);
        return true;
    }

    // row r 裡離 x 最近的 segment
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
        int r = db_.nearestRow(ty - na.h / 2);
        if (db_.rows[r].segs.empty()) return false;
        int s = segmentAt(r, tx);
        const Segment& sg = db_.segs[s];
        int w = db_.sitesOf(a, sg);
        if (w > sg.numSites) return false;
        int tSite = std::min(sg.numSites - 1, std::max(0, (int)std::floor((tx - sg.x0) / sg.sp)));
        long want = std::lround((tx - na.w / 2 - sg.x0) / sg.sp);   // 讓 a 的中心落在 tx 的左緣 site

        Gap g;
        int b = rm_.cellAt(s, tSite, g, a);
        if (b < 0) {   // 空位（可能包含 a 自己原本的位置）
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
        if (sa == s && std::abs(rm_.indexOf(a) - rm_.indexOf(b)) == 1) {   // 相鄰：直接對調順序
            int l = P_.site[a] < P_.site[b] ? a : b, rr = l == a ? b : a;
            int lSite = P_.site[l], rEnd = P_.site[rr] + db_.sitesOf(rr, sg);
            int newR = lSite, newL = rEnd - db_.sitesOf(l, sg);
            m.seg[0] = m.seg[1] = s;
            m.site[0] = a == l ? newL : newR;
            m.site[1] = b == l ? newL : newR;
            return true;
        }
        Gap ga = rm_.freeAround(a), gb = rm_.freeAround(b);
        if (gb.width() < w || ga.width() < wb) return false;   // 放不進對方的空間
        long bWant = P_.site[a] + std::lround((db_.nodes[a].w - db_.nodes[b].w) / 2 / db_.segs[sa].sp);
        m.seg[0] = s;
        m.site[0] = (int)std::max<long>(gb.lo, std::min<long>(gb.hi - w, want));
        m.seg[1] = sa;
        m.site[1] = (int)std::max<long>(ga.lo, std::min<long>(ga.hi - wb, bWant));
        return true;
    }

    // T0：試做幾千個 move，變差的平均變差 avg，讓它被接受的機率是 p0 → T0 = -avg / ln(p0)
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
    std::vector<Pt> c_;            // cell 中心
    Csr pins_, nets_;              // net → nodes、node → nets（只留 >= 2 pin 的）
    std::vector<double> netH_;
    std::vector<int> netStamp_;
    int stampId_ = 0;
    std::vector<std::pair<int, double>> touched_;
    Pt old_[2];
    double cur_ = 0, best_ = 0;
    std::vector<int> cellEpoch_;   // cell 上次被記進 journal 是哪個 epoch
    int epoch_ = 0;
    std::vector<Undo> journal_;
    std::vector<double> xs_, ys_;
    double xLo_, xHi_, yLo_, yHi_, winX_ = 0, winY_ = 0, minWinX_ = 0, minWinY_ = 0;
};

}  // namespace

SAResult anneal(const PlaceDB& db, Placement& P, const SAOptions& opt) {
    return Annealer(db, P, opt).run();
}
