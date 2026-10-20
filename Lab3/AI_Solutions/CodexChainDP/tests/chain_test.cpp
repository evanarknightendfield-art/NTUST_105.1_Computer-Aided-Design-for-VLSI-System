#include "../src/chain.hpp"
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void row(PlaceDB& db, double y, double h, double x, double spacing, int sites) {
    int r = int(db.rows.size()), s = int(db.segs.size());
    db.rows.push_back({y, h, {s}});
    db.segs.push_back({r, y, h, x, spacing, sites});
}
void cell(PlaceDB& db, Placement& p, int s, int site, double width, double gx, double gy) {
    Node n;
    n.name = "c" + std::to_string(db.nodes.size());
    n.w = width; n.h = db.segs[s].height; n.gx = gx; n.gy = gy;
    db.movable.push_back(int(db.nodes.size())); db.nodes.push_back(n);
    p.seg.push_back(s); p.site.push_back(site);
}
void pareto(const PlaceDB& db, const Placement& before, const Placement& after) {
    require(checkLegal(db, after).empty(), "chain produced an illegal placement");
    const auto a = displacement(db, before), b = displacement(db, after);
    require(b.total <= a.total + 1e-7, "chain increased total displacement");
    require(b.max <= a.max + 1e-7, "chain increased maximum displacement");
}
bool same(const Placement& a, const Placement& b) { return a.seg == b.seg && a.site == b.site; }
}

int main() {
    ChainOptions opt;
    opt.seconds = 60; opt.passes = 1; opt.depth = 10; opt.branches = 12;
    opt.expansions = 160; opt.maxBlockers = 1;

    // Closing the root's hole takes an eight-row ejection path. No site is
    // initially empty; intermediate missing cells must not appear as obstacles.
    PlaceDB cycle;
    Placement original;
    for (int r = 0; r < 8; ++r) row(cycle, r, 1, 0, 1, 1);
    for (int r = 0; r < 8; ++r) cell(cycle, original, r, 0, 1, 0, (r + 1) % 8);
    Placement p = original;
    auto result = chainDisplacement(cycle, p, opt);
    pareto(cycle, original, p);
    require(displacement(cycle, p).total == 0, "eight-row ejection cycle did not reach its known optimum");
    require(result.longest == 8 && result.accepted == 1 && result.refreshed == 8,
            "eight-row chain did not record and refresh every moved cell");

    // Cache-refresh regression: two exchanges reach zero cost. The partners
    // are still later roots in this SAME pass and must now be skipped. A stale
    // pass-level cache accepts extra chains that leave/revisit the optimum.
    PlaceDB revisited;
    Placement revisitStart;
    for (int r = 0; r < 4; ++r) row(revisited, r, 1, 0, 1, 1);
    for (int r = 0; r < 4; ++r) cell(revisited, revisitStart, r, 0, 1, 0, (r + 2) % 4);
    p = revisitStart;
    auto refreshed = chainDisplacement(revisited, p, opt);
    require(displacement(revisited, p).total == 0 && refreshed.accepted == 2 && refreshed.refreshed == 4,
            "successful chains left stale distances for later roots in the same pass");

    // Search-node and depth exhaustion must roll back a partially assembled
    // path, including its RowMap entries. A repeated full search must still work.
    for (int budget : {1, 2, 4}) {
        p = original;
        auto small = opt; small.expansions = budget; small.roots = 1;
        auto limited = chainDisplacement(cycle, p, small);
        require(limited.nodes <= budget, "per-root expansion limit exceeded");
        pareto(cycle, original, p);
        auto full = chainDisplacement(cycle, p, opt);
        (void)full;
        require(displacement(cycle, p).total == 0, "failed search corrupted a subsequent full search");
    }
    p = original;
    auto one = opt; one.depth = 1;
    chainDisplacement(cycle, p, one);
    require(same(p, original), "depth-one search could not restore its rejected occupied candidates");

    // A wide root needs to remove TWO narrow blockers at once. Restrict roots
    // to that root so starting the reverse path from a narrow cell cannot hide
    // the difference between a single-blocker path and a branching ejection.
    PlaceDB split;
    Placement splitStart;
    row(split, 0, 1, 0, 1, 3); row(split, 1, 1, 0, 1, 3);
    cell(split, splitStart, 0, 0, 3, 0, 1);
    cell(split, splitStart, 1, 0, 1, 0, 0);
    cell(split, splitStart, 1, 1, 2, 1, 0);
    auto branch = opt; branch.roots = 1;
    p = splitStart;
    chainDisplacement(split, p, branch);
    require(same(p, splitStart), "single-blocker fixture unexpectedly found a root move");
    branch.maxBlockers = 2;
    p = splitStart;
    auto two = chainDisplacement(split, p, branch);
    require(displacement(split, p).total == 0 && two.longest == 3 && two.multiBlocker == 1,
            "two-blocker chain failed its known optimum");
    pareto(split, splitStart, p);
    branch.expansions = 2;
    p = splitStart;
    auto exhausted = chainDisplacement(split, p, branch);
    require(exhausted.nodes == 2 && exhausted.accepted == 0 && same(p, splitStart),
            "two-blocker partial-search rollback failed");

    // Cancellation and validation must preserve the exact original placement.
    p = splitStart;
    auto stopped = branch; stopped.seconds = 0;
    require(chainDisplacement(split, p, stopped).nodes == 0 && same(p, splitStart), "zero-time search changed input");
    stopped.seconds = 1e-12;
    auto deadline = chainDisplacement(split, p, stopped);
    require(deadline.timedOut && deadline.nodes == 0 && same(p, splitStart), "expired search changed input");
    bool threw = false;
    try { auto invalid = opt; invalid.maxBlockers = 3; chainDisplacement(split, p, invalid); }
    catch (const std::runtime_error&) { threw = true; }
    require(threw && same(p, splitStart), "invalid options did not preserve the placement");

    // Multiple accepted chains revisit cells within the same pass. These
    // fixtures catch stale pass-level displacement caches, failed DFS undo,
    // cap violations, unequal widths, different site spacings/heights, and
    // fixed-object mutation. Every run is checked against the original data.
    std::mt19937 random(20261019);
    long totalChains = 0, totalMulti = 0;
    for (int trial = 0; trial < 300; ++trial) {
        PlaceDB db;
        Placement start;
        int nr = 2 + random() % 6;
        for (int r = 0; r < nr; ++r) {
            double sp = (r + trial) % 2 ? 0.5 : 1;
            row(db, r * 3.0, r % 2 ? 2 : 1, -2, sp, 24);
            int site = int(random() % 2);
            while (site + 5 <= 24) {
                double w = (1 + random() % 5) * 0.5;
                double gx = (int(random() % 64) - 10) * 0.25;
                double gy = (int(random() % (12 * nr)) - 3) * 0.25;
                cell(db, start, r, site, w, gx, gy);
                site += db.sitesOf(int(db.nodes.size()) - 1, db.segs[r]) + int(random() % 3);
            }
        }
        Node fixed;
        fixed.name = "fixed"; fixed.fixed = true; fixed.gx = -10; fixed.gy = -10; fixed.orient = "FS";
        db.nodes.push_back(fixed); start.seg.push_back(-1); start.site.push_back(7);
        require(checkLegal(db, start).empty(), "random fixture is illegal");
        auto options = opt;
        options.passes = 2; options.depth = 7; options.branches = 6;
        options.expansions = 50; options.maxBlockers = 1 + trial % 2;
        p = start;
        auto found = chainDisplacement(db, p, options);
        pareto(db, start, p);
        require(p.seg.back() == -1 && p.site.back() == 7, "fixed node coordinates changed");
        require(found.nodes <= found.roots * options.expansions, "search exceeded its node bound");
        require(found.longest <= options.depth && found.refreshed >= found.accepted,
                "chain length or refreshed-cell accounting is wrong");
        require(!found.timedOut, "random test unexpectedly hit its generous time limit");
        totalChains += found.accepted; totalMulti += found.multiBlocker;
        // With inactive time limits, work bounds and tie-breaking are deterministic.
        if (trial % 30 == 0) {
            Placement again = start;
            auto repeat = chainDisplacement(db, again, options);
            require(same(p, again) && found.nodes == repeat.nodes && found.accepted == repeat.accepted,
                    "fixed-work chain search is not deterministic");
        }
    }
    require(totalChains > 300 && totalMulti > 0, "random tests did not exercise enough accepted/revisited chains");
    std::cout << "PASS: eight-row cycle, two-blocker split, partial rollback, budgets, determinism; 300 random "
              << "placements (" << totalChains << " accepted chains, " << totalMulti << " multi-blocker steps)\n";
}
