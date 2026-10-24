#include "../src/annealer.hpp"
#include "../src/sa_bands.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    PlaceDB db;
    db.uniformSites = true;
    db.siteSpacing = 1;
    Placement original;
    constexpr int count = 2000, rows = 40, perRow = count / rows;
    for (int row = 0; row < rows; ++row) {
        db.rows.push_back({double(row), 1, {row}});
        db.segs.push_back({row, double(row), 1, 0, 1, 120});
    }
    for (int v = 0; v < count; ++v) {
        Node cell;
        cell.name = "c" + std::to_string(v);
        cell.w = cell.h = 1;
        cell.gx = 2 * (v % perRow);
        cell.gy = v / perRow;
        db.nodes.push_back(cell);
        db.movable.push_back(v);
        db.wSites.push_back(1);
        original.seg.push_back(v / perRow);
        original.site.push_back(2 * (v % perRow));
    }
    db.nodeNets.resize(count);
    for (int v = 0; v < count; ++v) {
        int other = (37 * v + 19) % count;
        db.nets.push_back({v, other});
        db.nodeNets[v].push_back(v);
        db.nodeNets[other].push_back(v);
    }
    require(checkLegal(db, original).empty(), "invalid test fixture");
    // Equal row populations make each expected boundary exact. Exercise all
    // four layouts, including the new rightward shift, and their full cycle.
    const std::vector<std::vector<int>> expected{
        {0, 10, 20, 30, 40}, {0, 5, 15, 25, 40},
        {0, 13, 23, 33, 40}, {0, 8, 18, 28, 40}};
    for (int round = 0; round < 8; ++round) {
        auto edge = sa_detail::bandEdges(db, original, 4, round, 3);
        require(edge == expected[round % 4], "incorrect deterministic boundary sweep");
        require(sa_detail::bandEdges(db, original, 1, round, 3) ==
                    std::vector<int>({0, rows}), "single band was split");
    }
    // Sparse rows and minimum-band coalescing must still partition every row
    // exactly once, including the thin tail of a right-shifted layout.
    PlaceDB sparse = db;
    sparse.movable.resize(100);
    for (int round = 0; round < 4; ++round) {
        auto edge = sa_detail::bandEdges(sparse, original, 8, round, 6);
        require(edge.front() == 0 && edge.back() == rows && edge.size() <= 9,
                "sparse partition lost rows or exceeded worker count");
        for (size_t i = 1; i < edge.size(); ++i)
            require(edge[i] - edge[i - 1] >= 6, "sparse partition has a thin band");
    }
    SAOptions options;
    options.threads = 2;
    options.minMoves = 0;
    options.movesPerCell = 10;
    options.steps = 3;
    options.syncMoves = 0.1;
    options.timeLimit = 30;
    // Disable the production wall-clock starvation heuristic for fixed-work
    // determinism; its behaviour is tested separately below.
    options.starvedAttemptsPerCell = 0;
    require(saThreads(db, options) == 2, "fixture did not enable parallel SA");
    Placement a = original, b = original;
    auto first = anneal(db, a, options);
    auto second = anneal(db, b, options);
    require(first.attempts > 0 && first.attempts == second.attempts, "inconsistent work budget");
    require(a.seg == b.seg && a.site == b.site, "fixed-work parallel SA was not deterministic");
    require(first.best == second.best && first.accepted == second.accepted,
            "fixed-work parallel statistics were not bitwise deterministic");
    require(checkLegal(db, a).empty(), "parallel merge produced illegal placement");
    require(first.best <= first.initial + 1e-7, "parallel SA lost its incumbent");
    require(std::abs(totalHPWL(db, a) - first.best) < 1e-7, "merged HPWL cache disagrees with actual placement");

    require(first.limitCut == 0, "a well-fed SA cut its own time limit");

    // v6: a starved SA keeps only starvedMinFraction of its limit
    SAOptions starved = options;
    starved.starvedAttemptsPerCell = 1e15;
    starved.starvedMinFraction = 0.1;
    Placement s = original;
    auto cut = anneal(db, s, starved);
    require(cut.limitCut > 26.9 && cut.limitCut < 27.0 + 1e-9, "starved SA did not keep 10 % of its limit");
    require(cut.seconds < 3.5, "starved SA ran past its cut limit");
    require(checkLegal(db, s).empty() && cut.best <= cut.initial + 1e-7, "starved SA broke legality or its incumbent");

    options.timeLimit = 0;
    Placement stopped = original;
    auto expired = anneal(db, stopped, options);
    require(expired.attempts == 0, "parallel SA started a batch after its deadline");
    require(stopped.seg == original.seg && stopped.site == original.site, "zero-budget run changed the placement");
    std::cout << "PASS: parallel legality, HPWL cache, fixed-work determinism, starved cut, expired budget\n";
}
