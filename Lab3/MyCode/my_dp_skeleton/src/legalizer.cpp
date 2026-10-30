// legalizer.cpp — Stage 1
//
//   M3：Tetris（講義 L07 Greedy Legalization）
//   M4：Abacus [Spindler et al., ISPD'08]
//   M5（進階）：合法化之後的位移微調
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
[[maybe_unused]] std::vector<int> xOrder(const PlaceDB& db) {
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
[[maybe_unused]] double dxBound(const Segment& s, int w, double gx) {
    double lo = s.x0, hi = s.x0 + (s.numSites - w) * s.sp;
    return gx < lo ? lo - gx : gx > hi ? gx - hi : 0.0;
}

// 實數位置 x（單位：site）→ 最近的整數 site，並夾在 [0, n - w]
[[maybe_unused]] int clampSite(double x, int w, int n) {
    long v = std::lround(x);
    return (int)std::max(0L, std::min((long)(n - w), v));
}

// ---------------------------------------------------------------- M3 Tetris
// 每個 segment 記一個 edge = 已經用到哪個 site。cell 依 x 排序，一個一個放：
//   對附近每個 row 的每個 segment：
//     site = max(edge, 自己想去的 site)，放得下的話 cost = |dx| + |dy|
//   挑 cost 最小的放，更新那個 segment 的 edge
bool tetris(const PlaceDB& db, Placement& P, std::string& err) {
    // TODO(M3)
    //   std::vector<int> edge(db.segs.size(), 0);
    //   for (int v : xOrder(db)) {
    //       double best = INF; int bestSeg = -1, bestSite = 0;
    //       forRowsNear(db, gy, [&](int r, double dy) { ... }, [&] { return best; });
    //       ...
    //   }
    (void)db; (void)P;
    err = "TODO(M3): tetris";
    return false;
}

// ---------------------------------------------------------------- M4 Abacus
// 每個 segment 存「cluster」：一串緊貼在一起的 cells。新 cell 加在最右邊，
// 如果跟左邊的 cluster 重疊就合併，合併後整串移到最佳位置（每個 cell 離自己
// 想去的位置的平方和最小 → 加權平均），可能又跟更左邊的重疊，繼續合併。
class Abacus {
public:
    Abacus(const PlaceDB& db, Placement& P) : db_(db), P_(P), st_(db.segs.size()) {}

    bool run(std::string& err) {
        // TODO(M4)
        //   for (int v : xOrder(db_)) {
        //       對附近的 row / segment 呼叫 trial(v, s, dy)，挑 cost 最小的
        //       commit(v, bestSeg)
        //   }
        err = "TODO(M4): abacus";
        return false;
    }

private:
    // first：cluster 第一個 cell 在 SegState::cells 的 index
    // e：權重總和、q：Σ e_i (x'_i - offset_i)、w：總寬度（sites）、x：cluster 左緣（site）
    // → cluster 的最佳位置 = q / e
    struct Cluster { int first; double e, q; int w, x; };
    struct SegState { std::vector<int> cells; std::vector<Cluster> cl; int used = 0; };

    // 想去的位置，換算成 segment s 的 site 單位（實數）
    double desired(int v, const Segment& s) const { return (db_.nodes[v].gx - s.x0) / s.sp; }

    // TODO(M4) 試放：把 v 接在 segment s 最右邊，模擬 cluster 合併，回傳 cost
    //   不能改到 st_！（只是試試看）
    //   cost 有兩種選法，報告可以比較：
    //     (a) 只算 v 自己的位移 |dx| + |dy|
    //     (b) v 的位移 + 被合併的 cells 位移的變化量（參考解用這個，總位移比較小）
    double trial(int v, int s, double dy) const {
        (void)v; (void)s; (void)dy;
        return INF;
    }

    // TODO(M4) 真的放進去：加一個新 cluster，跟左邊重疊就合併，最後更新最後一個 cluster 裡所有 cell 的 P_
    void commit(int v, int s) {
        (void)v; (void)s;
    }

    const PlaceDB& db_;
    Placement& P_;
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
    bool ok = opt.method == "tetris" ? tetris(db, P, err) : Abacus(db, P).run(err);
    if (!ok) return false;
    if (opt.verbose) {
        DispStats d = displacement(db, P);
        std::fprintf(stderr, "[legal] %-7s total %.1f  max %.1f\n", opt.method.c_str(), d.total, d.max);
    }
    if (opt.refine) refine(db, P, opt.verbose);
    return true;
}
