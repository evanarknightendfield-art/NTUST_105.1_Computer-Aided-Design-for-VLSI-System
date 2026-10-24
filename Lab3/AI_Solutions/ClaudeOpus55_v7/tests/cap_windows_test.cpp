#include "../src/window.hpp"
#include "../src/global.hpp"
#include <cassert>
#include <random>

static PlaceDB makeDB(int R, int N) {
    PlaceDB db; db.uniformSites = true; db.siteSpacing = 1;
    for (int r = 0; r < R; ++r) {
        db.rows.push_back({double(r), 1, {r}});
        db.segs.push_back({r, double(r), 1, 0, 1, 64});
    }
    for (int v = 0; v < N; ++v) {
        Node n; n.name = std::to_string(v); n.w = 1 + v % 3; n.h = 1;
        n.gx = 3 * (v / R); n.gy = v % R;
        db.nodes.push_back(n); db.movable.push_back(v); db.wSites.push_back(int(n.w)); db.nodeNets.push_back({});
    }
    return db;
}
int main() {
    // External moves invalidate just the old/new bounding neighbours, without
    // using a neighbour's newly assigned stamp as another movement seed.
    auto db = makeDB(2, 20);
    Placement p; p.seg.resize(20); p.site.resize(20);
    for (int v = 0; v < 20; ++v) { p.seg[v] = v % 2; p.site[v] = 5 * (v / 2); }
    WindowMemo m; m.sync(db, p);
    p.site[4] += 1; m.sync(db, p);
    for (int v = 0; v < 20; ++v) assert((m.mod[v] == m.clock) == (v == 2 || v == 4 || v == 6));
    // Fixed-order packing preserves row membership and cannot worsen L1
    // when the incumbent already satisfies the cap.
    auto keep = p;
    assert(repairCapOrderedRows(db, p, displacement(db, p).max));
    assert(p.seg == keep.seg);
    assert(checkLegal(db, p).empty());
    assert(displacement(db, p).total <= displacement(db, keep).total + 1e-6);
    // Soundness and transactional rollback for varied caps / row counts.
    std::mt19937 rng(421);
    int success = 0, failure = 0;
    for (int t = 0; t < 80; ++t) {
        int R = 2 + t % 3, N = 8 + t % 6;
        db = makeDB(R, N); p.seg.resize(N); p.site.resize(N);
        for (int v = 0; v < N; ++v) {
            p.seg[v] = v % R; p.site[v] = 5 * (v / R) + 6;
            db.nodes[v].gx += double(rng() % 60) / 10;
        }
        assert(checkLegal(db, p).empty());
        auto q = p; double D = displacement(db, p).max * (t % 2 ? .9 : .1);
        WindowOptions o; o.timeLimit = .2; o.passes = 2;
        if (repairCapWindows(db, q, D, o)) {
            ++success; assert(checkLegal(db, q).empty()); assert(displacement(db, q).max <= D + 1e-6);
        } else { ++failure; assert(q.seg == p.seg && q.site == p.site); }
        q = p; o.timeLimit = 0;
        assert(!repairCapWindows(db, q, -1, o));
        assert(q.seg == p.seg && q.site == p.site);
    }
    assert(success > 0 && failure > 0);
}
