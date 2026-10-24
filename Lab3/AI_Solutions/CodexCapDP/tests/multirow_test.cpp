// v4: the 1- to 3-row window DP (solveMulti) against exhaustive enumeration.
//   g++ -std=c++17 -O2 tests/multirow_test.cpp src/window.cpp src/placedb.cpp -o /tmp/multirow_test
#include "../src/window.hpp"
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <random>

int main() {
    std::mt19937 rng(20261019);
    const double inf = std::numeric_limits<double>::infinity();
    int feasible = 0;
    for (int trial = 0; trial < 1500; ++trial) {
        MultiProblem p;
        int R = 1 + trial % 3;
        p.cells = 1 + rng() % 7;
        p.length.resize(R);
        p.width.resize(R);
        p.cost.resize(R);
        for (int r = 0; r < R; ++r) {
            p.length[r] = 2 + rng() % 7;
            p.width[r].resize(p.cells);
            p.cost[r].resize(p.cells);
            for (int i = 0; i < p.cells; ++i) {
                p.width[r][i] = 1 + rng() % 3;
                p.cost[r][i].resize(p.length[r] + 1);
                for (double& c : p.cost[r][i]) c = rng() % 5 ? (int(rng() % 101) - 50) * 0.1 : inf;
            }
        }
        // brute force: every cell to every (row, site), no overlap
        double exact = inf;
        std::vector<unsigned> occupied(R, 0);
        std::function<void(int, double)> enumerate = [&](int i, double value) {
            if (i == p.cells) { exact = std::min(exact, value); return; }
            for (int r = 0; r < R; ++r)
                for (int x = 0; x + p.width[r][i] <= p.length[r]; ++x) {
                    unsigned bits = ((1u << p.width[r][i]) - 1) << x;
                    if ((occupied[r] & bits) || !std::isfinite(p.cost[r][i][x])) continue;
                    occupied[r] |= bits;
                    enumerate(i + 1, value + p.cost[r][i][x]);
                    occupied[r] ^= bits;
                }
        };
        enumerate(0, 0);
        WindowSolution s = solveMulti(p);
        assert(s.feasible == std::isfinite(exact));
        if (!s.feasible) continue;
        ++feasible;
        assert(std::fabs(s.cost - exact) < 1e-8);
        std::fill(occupied.begin(), occupied.end(), 0u);
        double sum = 0;
        for (int i = 0; i < p.cells; ++i) {
            int r = s.row[i], x = s.site[i];
            assert(r >= 0 && r < R && x >= 0 && x + p.width[r][i] <= p.length[r]);
            unsigned bits = ((1u << p.width[r][i]) - 1) << x;
            assert(!(occupied[r] & bits));
            occupied[r] |= bits;
            sum += p.cost[r][i][x];
        }
        assert(std::fabs(sum - exact) < 1e-8);
    }
    // A chain: three full rows, every cell wants the row below its own
    // (cyclically). Only a joint move of all of them reaches cost 0.
    MultiProblem c;
    c.cells = 6;
    c.length = {4, 4, 4};
    c.width.assign(3, std::vector<int>(6, 2));
    c.cost.assign(3, std::vector<std::vector<double>>(6, std::vector<double>(5, 10)));
    for (int i = 0; i < 6; ++i)
        for (double& v : c.cost[(i / 2 + 1) % 3][i]) v = 0;   // cells 0,1 -> row 1, 2,3 -> row 2, 4,5 -> row 0
    WindowSolution s = solveMulti(c);
    assert(s.feasible && s.cost == 0);
    std::cout << "PASS: 1500 exhaustive 1/2/3-row comparisons (" << feasible
              << " feasible) and a three-row chain fixture\n";
}
