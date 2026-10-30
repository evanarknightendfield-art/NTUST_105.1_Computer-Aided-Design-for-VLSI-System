// legalizer.cpp — Stage 1
//
//   M3：Tetris（講義 L07 Greedy Legalization）  ← v1（比較用）
//   M4：Abacus [Spindler et al., ISPD'08]        ← v1 做到這裡
//   M5（進階）：合法化之後的位移微調             ← v3
#include "legalizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

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
    // first：cluster 第一個 cell 在 SegState::cells 的 index
    // e：權重總和、q：Σ e_i (x'_i - offset_i)、w：總寬度（sites）、x：cluster 左緣（site）
    // → cluster 的最佳位置 = q / e
    struct Cluster { int first; double e, q; int w, x; };
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
        for (size_t i = S.cl[k + 1].first; i < S.cells.size(); ++i) {
            int c = S.cells[i];
            double u = desired(c, sg);
            delta += std::fabs(pos - u) - std::fabs(P_.site[c] - u);
            pos += db_.sitesOf(c, sg);
        }
        return own + delta * sg.sp;
    }

    // 真的放進去：加一個新 cluster，跟左邊重疊就合併，最後一個 cluster 的 cells 重新排位置
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
        int pos = S.cl.back().x;   // 只有最後一個 cluster 的位置會變
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

// ---------------------------------------------------------------- M5 微調（進階）
// 合法之後再改善位移，例如：
//   * 最大位移那個 cell：找一個更近的位置插進去，左右鄰居推一推讓出空間
//     （被推的 cell 也不能超過目前的最大位移）
//   * 每個 cell：如果換到別的位置（或跟別人交換）總位移會變小，就換
void refine(const PlaceDB& db, Placement& P, bool verbose) {
    // TODO(M5)
    (void)db; (void)P; (void)verbose;
}

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
    if (opt.refine) refine(db, P, opt.verbose);
    return true;
}
