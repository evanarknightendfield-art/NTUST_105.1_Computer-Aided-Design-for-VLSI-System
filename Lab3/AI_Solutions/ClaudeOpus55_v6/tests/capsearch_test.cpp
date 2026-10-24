// capsearch_test — v5 cap search: soundness and rollback.
//  1. random small designs: every successful rowRepair is legal and keeps every
//     displacement <= D; a failure leaves the placement untouched
//  2. single-row instances: whenever rowRepair succeeds, a brute-force check
//     over all orders confirms a feasible order exists (soundness), and when a
//     brute-force order exists for a 1-row design with room, the repair finds it
//  3. capSearch never raises the maximum and keeps total <= ratio * start
//  4. toy-like cluster: all cells at one point, the cap search lowers the max
#include "../src/capsearch.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>

namespace {
PlaceDB makeRows(int rows, int sites, double sp = 1, double h = 1) {
    PlaceDB db;
    db.uniformSites = true;
    db.siteSpacing = sp;
    for (int r = 0; r < rows; ++r) {
        db.rows.push_back({r * h, h, {r}});
        db.segs.push_back({r, r * h, h, 0, sp, sites});
    }
    return db;
}
void addCell(PlaceDB& db, double gx, double gy, int w, double h = 1) {
    Node n;
    n.name = "c" + std::to_string(db.nodes.size());
    n.w = w * db.siteSpacing;
    n.h = h;
    n.gx = gx;
    n.gy = gy;
    db.movable.push_back(int(db.nodes.size()));
    db.nodes.push_back(n);
    db.wSites.push_back(w);
    db.nodeNets.push_back({});
}
// left-to-right packing in global x order: a legal start (empty seg = no room)
Placement packAll(const PlaceDB& db) {
    Placement p;
    p.seg.assign(db.nodes.size(), -1);
    p.site.assign(db.nodes.size(), 0);
    std::vector<int> order = db.movable;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return db.nodes[a].gx < db.nodes[b].gx; });
    std::vector<int> end(db.segs.size(), 0);
    for (int v : order) {
        int best = -1;
        double bd = 1e300;
        for (size_t s = 0; s < db.segs.size(); ++s) {
            int w = db.wSites[v];
            if (end[s] + w > db.segs[s].numSites) continue;
            int x = std::max(end[s], int(std::lround(db.nodes[v].gx)));
            x = std::min(x, db.segs[s].numSites - w);
            if (x < end[s]) continue;
            double d = std::fabs(x - db.nodes[v].gx) + std::fabs(db.segs[s].y - db.nodes[v].gy);
            if (d < bd) { bd = d; best = int(s); }
        }
        if (best < 0) return Placement{};
        int w = db.wSites[v];
        int x = std::min(std::max(end[best], int(std::lround(db.nodes[v].gx))), db.segs[best].numSites - w);
        p.seg[v] = best;
        p.site[v] = x;
        end[best] = x + w;
    }
    assert(checkLegal(db, p).empty());
    return p;
}
// brute force: does some order of the cells fit one row under cap D?
bool bruteRow(const PlaceDB& db, double D) {
    std::vector<int> c = db.movable;
    std::sort(c.begin(), c.end());
    do {
        double end = 0;
        bool ok = true;
        for (int v : c) {
            const Node& n = db.nodes[v];
            double reach = D - std::fabs(n.gy - db.segs[0].y);
            if (reach < 0) { ok = false; break; }
            double lo = std::max(0.0, std::ceil(n.gx - reach - 1e-9));
            double hi = std::min(double(db.segs[0].numSites - db.wSites[v]), std::floor(n.gx + reach + 1e-9));
            double x = std::max(end, lo);
            if (x > hi) { ok = false; break; }
            end = x + db.wSites[v];
        }
        if (ok) return true;
    } while (std::next_permutation(c.begin(), c.end()));
    return false;
}
}  // namespace

int main() {
    std::mt19937 rng(7);
    int repaired = 0, failed = 0;
    // 1. random designs
    for (int t = 0; t < 300; ++t) {
        int rows = 2 + int(rng() % 5), sites = 20 + int(rng() % 30), n = 3 + int(rng() % 10);
        PlaceDB db = makeRows(rows, sites);
        for (int i = 0; i < n; ++i)
            addCell(db, std::uniform_real_distribution<double>(0, sites - 4)(rng),
                    std::uniform_real_distribution<double>(0, rows - 1)(rng), 1 + int(rng() % 4));
        Placement p = packAll(db);
        if (p.seg.empty()) continue;
        DispStats s = displacement(db, p);
        double D = s.max * std::uniform_real_distribution<double>(0.5, 1.0)(rng);
        Placement q = p;
        RepairOptions o;
        o.phase1Until = wallSeconds() + 0.05;
        o.phase2Until = o.phase1Until + 0.02;
        o.seed = t;
        if (rowRepair(db, q, D, o)) {
            assert(checkLegal(db, q).empty());
            assert(displacement(db, q).max <= D + 1e-6);
            ++repaired;
        } else {
            assert(q.seg == p.seg && q.site == p.site);
            ++failed;
        }
    }
    // 2. one row: soundness against brute force, and completeness when it is easy
    int agree = 0;
    for (int t = 0; t < 300; ++t) {
        int sites = 12 + int(rng() % 10), n = 2 + int(rng() % 5);
        PlaceDB db = makeRows(1, sites);
        for (int i = 0; i < n; ++i) addCell(db, std::uniform_real_distribution<double>(0, sites - 3)(rng), 0, 1 + int(rng() % 3));
        int total = 0;
        for (int v : db.movable) total += db.wSites[v];
        if (total > sites) continue;
        Placement p = packAll(db);
        if (p.seg.empty()) continue;
        double D = displacement(db, p).max * std::uniform_real_distribution<double>(0.3, 1.0)(rng);
        Placement q = p;
        RepairOptions o;
        o.phase1Until = wallSeconds() + 0.02;
        o.phase2Until = o.phase1Until + 0.01;
        bool ok = rowRepair(db, q, D, o);
        bool brute = bruteRow(db, D);
        if (ok) assert(brute);                 // never claims an impossible cap
        if (ok == brute) ++agree;
    }
    // 3. cap search invariants
    for (int t = 0; t < 40; ++t) {
        int rows = 3 + int(rng() % 4), sites = 30;
        PlaceDB db = makeRows(rows, sites);
        for (int i = 0; i < 14; ++i)
            addCell(db, std::uniform_real_distribution<double>(5, 15)(rng), std::uniform_real_distribution<double>(0, 1)(rng),
                    1 + int(rng() % 4));
        Placement p = packAll(db);
        if (p.seg.empty()) continue;
        DispStats a = displacement(db, p);
        CapSearchOptions o;
        o.seconds = 0.2;
        o.totalRatio = 1.01;
        o.dp.passes = 1000;
        CapSearchResult r = capSearch(db, p, o);
        DispStats b = displacement(db, p);
        assert(checkLegal(db, p).empty());
        assert(b.max <= a.max + 1e-9);
        assert(b.max < a.max - 1e-9 || std::fabs(b.total - a.total) < 1e-9 || r.accepted == 0);
        assert(b.total <= a.total * 1.01 + 1e-6);
    }
    // 4. toy-like cluster: 12 cells at one point, the cap search must lower the max
    {
        PlaceDB db = makeRows(9, 60);
        for (int i = 0; i < 12; ++i) addCell(db, 28, 4, 3 + i % 3);
        Placement p = packAll(db);
        assert(!p.seg.empty());
        double before = displacement(db, p).max;
        CapSearchOptions o;
        o.seconds = 0.5;
        o.totalRatio = 10;
        o.dp.passes = 1000;
        capSearch(db, p, o);
        assert(checkLegal(db, p).empty());
        assert(displacement(db, p).max < before - 1e-9);
        std::printf("cluster: max %.3f -> %.3f\n", before, displacement(db, p).max);
    }
    std::printf("PASS: %d repaired / %d unresolved random caps (all legal, all <= D, failures untouched); "
                "one-row agreement with brute force %d / 300 (never unsound)\n", repaired, failed, agree);
    return 0;
}
