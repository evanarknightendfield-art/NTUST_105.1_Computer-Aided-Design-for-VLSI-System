#include "../src/annealer.hpp"
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
    SAOptions options;
    options.threads = 2;
    options.minMoves = 0;
    options.movesPerCell = 10;
    options.steps = 3;
    options.syncMoves = 0.1;
    options.timeLimit = 30;
    require(saThreads(db, options) == 2, "fixture did not enable parallel SA");
    Placement a = original, b = original;
    auto first = anneal(db, a, options);
    auto second = anneal(db, b, options);
    require(first.attempts > 0 && first.attempts == second.attempts, "inconsistent work budget");
    require(a.seg == b.seg && a.site == b.site, "fixed-work parallel SA was not deterministic");
    require(checkLegal(db, a).empty(), "parallel merge produced illegal placement");
    require(first.best <= first.initial + 1e-7, "parallel SA lost its incumbent");
    require(std::abs(totalHPWL(db, a) - first.best) < 1e-7, "merged HPWL cache disagrees with actual placement");

    options.timeLimit = 0;
    Placement stopped = original;
    auto expired = anneal(db, stopped, options);
    require(expired.attempts == 0, "parallel SA started a batch after its deadline");
    require(stopped.seg == original.seg && stopped.site == original.site, "zero-budget run changed the placement");
    std::cout << "PASS: parallel legality, HPWL cache, fixed-work determinism, expired budget\n";
}
