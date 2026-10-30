// legalizer.cpp — Stage 1
//
//   M3：Tetris（講義 L07 Greedy Legalization）  ← v1（比較用）
//   M4：Abacus [Spindler et al., ISPD'08]        ← v1（v3 加速大設計的熱點）
//   M5（進階）：合法化之後的位移微調             ← v3
#include "legalizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <queue>

#include "rowmap.hpp"

namespace {

constexpr double INF = std::numeric_limits<double>::infinity();

// ---- 寫好的小工具 ----

// movable cells 依 global placement 的 x 排序
std::vector<int> xOrder(const PlaceDB& db) {
    std::vector<int> order = db.movable;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const Node &na = db.nodes[a], &nb = db.nodes[b];
        return na.gx != nb.gx ? na.gx < nb.gx : a < b;
    });
    return order;
}

// 從離 y 最近的 row 開始，上下交替往外走，對每個 row 呼叫 visit(row, |dy|)；
// 一旦「光是垂直距離 |dy| 就 >= best()」就停（再遠的 row 不可能更好）。
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

// 寬 w 個 sites、左緣想放在 gx 的 cell，放進 segment s 至少要水平移動多少
double dxBound(const Segment& s, int w, double gx) {
    double lo = s.x0, hi = s.x0 + (s.numSites - w) * s.sp;
    return gx < lo ? lo - gx : gx > hi ? gx - hi : 0.0;
}

// 實數位置 x（單位：site）→ 最近的整數 site，並夾在 [0, n - w]
int clampSite(double x, int w, int n) {
    long v = std::lround(x);
    return (int)std::max(0L, std::min((long)(n - w), v));
}

// ---------------------------------------------------------------- M3 Tetris
// 每個 segment 記一個 edge = 已經用到哪個 site。cell 依 x 排序，一個一個放：
//   對附近每個 row 的每個 segment：
//     site = max(edge, 自己想去的 site)，放得下的話 cost = |dx| + |dy|
//   挑 cost 最小的放，更新那個 segment 的 edge
bool tetris(const PlaceDB& db, Placement& P, std::string& err) {
    std::vector<int> edge(db.segs.size(), 0);   // 每個 segment 已經用到第幾個 site
    for (int v : xOrder(db)) {
        const Node& n = db.nodes[v];
        double best = INF;
        int bestSeg = -1, bestSite = 0;
        forRowsNear(db, n.gy, [&](int r, double dy) {
            for (int s : db.rows[r].segs) {
                const Segment& sg = db.segs[s];
                int w = db.sitesOf(v, sg);
                // 放不下，或光是「水平至少要移多少」就輸了 → 跳過
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

// ---------------------------------------------------------------- M4 Abacus
// 每個 segment 存「cluster」：一串緊貼在一起的 cells。新 cell 加在最右邊，
// 如果跟左邊的 cluster 重疊就合併，合併後整串移到最佳位置（每個 cell 離自己
// 想去的位置的平方和最小 → 加權平均），可能又跟更左邊的重疊，繼續合併。
class Abacus {
public:
    Abacus(const PlaceDB& db, Placement& P, bool totalCost, double exactWork, int exactCells)
        : db_(db), P_(P), totalCost_(totalCost), exactWork_(exactWork), exactCells_(exactCells), st_(db.segs.size()) {}

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
        // v3：位置最後才一次寫出去（v1 的 commit 每次都重寫整個最後一個 cluster，
        //     熱點裡 cluster 有上萬個 cells 時是 O(n²)）
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

private:
    // first：cluster 第一個 cell 在 SegState::cells 的 index，n：cell 數
    // e：權重總和、q：Σ e_i (x'_i - offset_i)、w：總寬度（sites）、x：cluster 左緣（site）
    // → cluster 的最佳位置 = q / e
    struct Cluster { int first; double e, q; int w, x; int n; };
    struct SegState { std::vector<int> cells; std::vector<Cluster> cl; int used = 0; };

    // 想去的位置，換算成 segment s 的 site 單位（實數）
    double desired(int v, const Segment& s) const { return (db_.nodes[v].gx - s.x0) / s.sp; }

    // 試放：把 v 接在 segment s 最右邊，模擬 cluster 合併，回傳 cost（不改 st_）
    double trial(int v, int s, double dy) const {
        const Segment& sg = db_.segs[s];
        const SegState& S = st_[s];
        int w = db_.sitesOf(v, sg);
        if (S.used + w > sg.numSites) return INF;
        double e = 1, q = desired(v, sg);
        int W = w, k = (int)S.cl.size() - 1;
        int x = clampSite(q / e, W, sg.numSites);
        while (k >= 0 && S.cl[k].x + S.cl[k].w > x) {   // 跟左邊的 cluster 重疊 → 合併
            const Cluster& c = S.cl[k];
            q = c.q + q - e * c.w;   // 新的 cells 在合併後的 offset 都多了 c.w
            e = c.e + e;
            W = c.w + W;
            x = clampSite(q / e, W, sg.numSites);
            --k;
        }
        double own = std::fabs((x + W - w) - desired(v, sg)) * sg.sp + dy;   // v 在合併後的最右邊
        if (!totalCost_ || k + 1 == (int)S.cl.size()) return own;
        // (b) 被合併的 cells 也會跟著動：加上它們位移的變化量
        double delta = 0;
        int pos = x;
        int cells = (int)S.cells.size() - S.cl[k + 1].first;
        if (work_ < exactWork_ || cells <= exactCells_) {
            work_ += cells;
            for (size_t j = k + 1; j < S.cl.size(); ++j) {
                int old = S.cl[j].x;   // v3：舊位置從 cluster 算（還沒寫進 P_）
                for (int i = S.cl[j].first; i < S.cl[j].first + S.cl[j].n; ++i) {
                    int c = S.cells[i], wc = db_.sitesOf(c, sg);
                    double u = desired(c, sg);
                    delta += std::fabs(pos - u) - std::fabs(old - u);
                    pos += wc;
                    old += wc;
                }
            }
        } else {
            // v3：大合併（熱點）：每個 cluster 整串平移 |shift|，位移變化不會超過 |shift| × cell 數
            for (size_t j = k + 1; j < S.cl.size(); ++j) {
                delta += std::fabs(pos - S.cl[j].x) * S.cl[j].n;
                pos += S.cl[j].w;
            }
        }
        return own + delta * sg.sp;
    }

    // 真的放進去：加一個新 cluster，跟左邊重疊就合併（位置等 run() 最後再寫）
    void commit(int v, int s) {
        const Segment& sg = db_.segs[s];
        SegState& S = st_[s];
        int w = db_.sitesOf(v, sg);
        S.cells.push_back(v);
        S.used += w;
        double u = desired(v, sg);
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
    }

    const PlaceDB& db_;
    Placement& P_;
    bool totalCost_;
    double exactWork_;          // v3：照算的 cell 數用完之後，
    int exactCells_;            //     合併超過 exactCells_ 個 cell 就改用上界（數 cell 不看時間，結果和機器快慢無關）
    mutable double work_ = 0;   //     到目前為止照算走過幾個 cell（ibm01 約 3e6、20 萬 cell 的 L1 約 3e9）
    std::vector<SegState> st_;
};

// ---------------------------------------------------------------- M5 微調（v3）
// 合法之後再降位移。兩種 local move，每次都檢查「動到的 cell 位移不能超過上限 bound」：
//   insert：把 c 拿起來插到某個 segment 的某個 site，左右鄰居各自往外推「剛好夠」的距離
//   swap：  c 和 b 交換（各自放得進對方的空間）
//   evict： 最大位移的 c 卡住時，把附近一個位移很小的 b 先拿出來，c 放進 b 的位置，b 再去找別的地方
// v3 為了 60 秒規定：位移最大的 cell 用 heap 找（不是每次掃全部）、有時間上限、evict 每個 row 只看附近幾個 cell。
class Refiner {
public:
    Refiner(const PlaceDB& db, Placement& P, bool evict, std::chrono::steady_clock::time_point deadline)
        : db_(db), P_(P), rm_(db, P), d_(db.nodes.size(), 0), evict_(evict), deadline_(deadline) {
        for (int v : db.movable) {
            d_[v] = displacementOf(db, P, v);
            heap_.push({d_[v], v});
        }
    }

    bool timeUp() const { return std::chrono::steady_clock::now() > deadline_; }

    // 反覆拿位移最大的 cell c，上限設成「目前最大值 − 1 個 site」，找得到 insert / swap 就做，
    // 卡住就試 evict → 最大值只會下降。c 真的動不了就結束。
    void reduceMax(bool verbose) {
        int moves = 0;
        while (!timeUp()) {
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

    // 每個 cell（位移大的先）試 insert / swap，總位移變小、而且不超過目前最大值就做
    void reduceTotal(bool verbose) {
        double bound = d_[worst()];
        for (int pass = 0; pass < 20 && !timeUp(); ++pass) {
            std::vector<int> order = db_.movable;
            std::sort(order.begin(), order.end(), [&](int a, int b) { return d_[a] > d_[b]; });
            double gain = 0;
            int moves = 0, seen = 0;
            for (int c : order) {
                if (d_[c] <= 0) break;
                if ((++seen & 63) == 0 && timeUp()) break;
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
        double delta = INF;                        // 總位移的變化
        int seg = -1, site = 0;                    // c 的新位置
        int swapWith = -1, swapSeg = -1, swapSite = 0;
        std::vector<std::pair<int, int>> pushes;   // 被推的 (cell, 新 site)，都在 seg 裡
    };

    double minStep() const { return db_.segs[0].sp; }

    // 位移最大的 cell：heap 裡過期的（位移已經變了）直接丟掉
    int worst() {
        while (heap_.top().first != d_[heap_.top().second]) heap_.pop();
        return heap_.top().second;
    }

    double dispAt(int v, const Segment& s, int site) const {
        const Node& n = db_.nodes[v];
        return std::fabs(s.x0 + site * s.sp - n.gx) + std::fabs(s.y - n.gy);
    }

    // 兩步的 move：c 自己動不了時，把 c 目標附近一個位移很小的 b 拿出來，c 放進去，b 再找別的地方。
    // roomFirst：先試「本來的空間就放得下 c」的 b，再試位移最小的
    bool evict(int c, double bound, bool roomFirst) {
        const Node& n = db_.nodes[c];
        std::vector<std::pair<double, int>> cand;
        forRowsNear(db_, n.gy, [&](int r, double dy) {
            for (int s : db_.rows[r].segs) {
                const Segment& sg = db_.segs[s];
                double rr = (bound - dy) / sg.sp, uc = (n.gx - sg.x0) / sg.sp;
                int wc = db_.sitesOf(c, sg);
                const auto& cs = rm_.cells(s);
                // v3：每個 segment 只看 c 目標兩邊各 16 個 cell（參考解看 window 裡全部，大設計太慢）
                int mid = (int)(std::lower_bound(cs.begin(), cs.end(), uc,
                                                 [&](int v, double x) { return P_.site[v] < x; }) - cs.begin());
                for (int i = std::max(0, mid - 16); i < std::min((int)cs.size(), mid + 16); ++i) {
                    int b = cs[i];
                    if (b == c || P_.site[b] < uc - rr - wc || P_.site[b] > uc + rr) continue;
                    cand.push_back({d_[b] + (roomFirst && rm_.freeAround(b).width() < wc ? 1e18 : 0.0), b});
                }
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
                // b 找不到地方：全部還原
                rm_.remove(c);
                for (auto [v, x] : undo) setSite(v, x);
                if (oc >= 0) rm_.insert(c, oc, ox);
                else P_.seg[c] = -1;
                setDisp(c);
            }
            rm_.insert(b, sb, xb);
        }
        return false;
    }

    // c 的最佳 move（所有動到的 cell 位移都 <= bound）。只看垂直距離 < reach 的 rows。
    // c 被拿出來（seg = -1）的時候只能 insert。
    void searchFor(int c, double bound, double reach, Plan& best) {
        const Node& n = db_.nodes[c];
        forRowsNear(db_, n.gy, [&](int r, double dy) {
            if (dy > bound) return;
            for (int s : db_.rows[r].segs) tryInsert(c, s, bound, best);
            if (P_.seg[c] < 0) return;
            // swap 的對象：這個 row 裡蓋住 c 的 GP 中心的那個 cell
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
        double r = (bound - dy) / sg.sp;   // 水平最多還能差幾個 site
        if (r < 0) return;
        double uc = (nc.gx - sg.x0) / sg.sp;
        int cLo = std::max(0, (int)std::ceil(uc - r - 1e-9));
        int cHi = std::min(sg.numSites - wc, (int)std::floor(uc + r + 1e-9));
        if (cLo > cHi) return;

        // segment s 去掉 c 之後的 cells：L(j)
        const std::vector<int>& all = rm_.cells(s);
        int ic = P_.seg[c] == s ? rm_.indexOf(c) : -1;
        int m = (int)all.size() - (ic >= 0 ? 1 : 0);
        auto L = [&](int j) { return all[ic >= 0 && j >= ic ? j + 1 : j]; };
        // 依中心找插入的位置 k0，試 k0 - 1 ~ k0 + 1
        double cc = uc + wc / 2.0;
        int k0 = (int)(std::lower_bound(all.begin(), all.end(), cc, [&](int v, double x) {
                           return P_.site[v] + db_.sitesOf(v, sg) / 2.0 < x;
                       }) - all.begin());
        if (ic >= 0 && ic < k0) --k0;
        double oldC = d_[c];
        for (int k = std::max(0, k0 - 1); k <= std::min(m, k0 + 1); ++k) {
            int gLo = k > 0 ? P_.site[L(k - 1)] + db_.sitesOf(L(k - 1), sg) : 0;
            int gHi = k < m ? P_.site[L(k)] : sg.numSites;
            // 候選位置：c 想去的地方、空隙的兩端、想去的地方夾進空隙
            long xs[4] = {std::lround(uc), gLo, gHi - wc,
                          std::max<long>(gLo, std::min<long>(gHi - wc, std::lround(uc)))};
            for (long& x : xs) x = std::max<long>(cLo, std::min<long>(cHi, x));
            std::sort(xs, xs + 4);
            for (int i = 0; i < 4; ++i) {
                if (i > 0 && xs[i] == xs[i - 1]) continue;
                int x = (int)xs[i];
                double delta = dispAt(c, sg, x) - oldC;
                pushes_.clear();
                // 左邊：往左推剛好夠
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
                // 右邊：往右推剛好夠
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

    // 被推的 cell：往自己的目標靠近隨便推；遠離的話不能超過 bound
    bool allowed(int v, const Segment& s, int nx, double bound) const {
        double nd = dispAt(v, s, nx);
        return nd <= bound + 1e-9 || nd <= d_[v] + 1e-9;
    }

    void trySwap(int c, int b, double bound, Plan& best) {
        int sc = P_.seg[c], sb = P_.seg[b];
        const Segment &gc = db_.segs[sc], &gb = db_.segs[sb];
        int wc = db_.sitesOf(c, gb), wb = db_.sitesOf(b, gc);
        if (sc == sb && std::abs(rm_.indexOf(c) - rm_.indexOf(b)) == 1) return;   // 相鄰的 insert 已經涵蓋
        Gap roomC = rm_.freeAround(c, b), roomB = rm_.freeAround(b, c);
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

    void setDisp(int v) {
        d_[v] = displacementOf(db_, P_, v);
        heap_.push({d_[v], v});
    }

    void setSite(int v, int x) {
        rm_.slide(v, x);
        setDisp(v);
    }

    void apply(int c, const Plan& p) {
        if (p.swapWith >= 0) {
            rm_.exchange(c, p.swapWith, p.seg, p.site, p.swapSeg, p.swapSite);
            setDisp(p.swapWith);
        } else {
            if (P_.seg[c] >= 0) rm_.remove(c);
            for (auto [v, x] : p.pushes) setSite(v, x);
            rm_.insert(c, p.seg, p.site);
        }
        setDisp(c);
    }

    const PlaceDB& db_;
    Placement& P_;
    RowMap rm_;
    std::vector<double> d_;                           // 每個 cell 目前的位移
    std::priority_queue<std::pair<double, int>> heap_;   // (位移, cell)，可能有過期的
    std::vector<std::pair<int, int>> pushes_;
    bool evict_;
    std::chrono::steady_clock::time_point deadline_;
};

}  // namespace

bool legalize(const PlaceDB& db, Placement& P, const LegalizeOptions& opt, std::string& err) {
    auto t0 = std::chrono::steady_clock::now();
    P.seg.assign(db.nodes.size(), -1);
    P.site.assign(db.nodes.size(), 0);
    bool ok = opt.method == "tetris" ? tetris(db, P, err) : Abacus(db, P, opt.totalCost, opt.exactWork, opt.exactCells).run(err);
    if (!ok) return false;
    if (opt.verbose) {
        DispStats d = displacement(db, P);
        std::fprintf(stderr, "[legal] %-7s total %.1f  max %.1f  %.2fs\n", opt.method.c_str(), d.total, d.max,
                     std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    if ((opt.refineMax || opt.refineTotal) && !db.movable.empty()) {
        auto deadline = std::chrono::steady_clock::time_point::max();
        if (opt.runEnd != deadline) {
            auto now = std::chrono::steady_clock::now();
            deadline = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                 std::max(std::chrono::steady_clock::duration::zero(), opt.runEnd - now) * opt.refineShare);
        }
        Refiner ref(db, P, opt.evict, deadline);
        // 總位移那一步會空出位置，最大位移那一步又能再用，所以交替做
        double prevMax = INF;
        for (int round = 0; round < 4 && !ref.timeUp(); ++round) {
            if (opt.refineMax) ref.reduceMax(opt.verbose);
            if (opt.refineTotal) ref.reduceTotal(opt.verbose);
            double mx = displacement(db, P).max;
            if (!opt.refineMax || mx > prevMax - 1e-9) break;
            prevMax = mx;
        }
        if (ref.timeUp() && opt.verbose) std::fprintf(stderr, "[legal] refine stopped at its time budget\n");
    }
    return true;
}
