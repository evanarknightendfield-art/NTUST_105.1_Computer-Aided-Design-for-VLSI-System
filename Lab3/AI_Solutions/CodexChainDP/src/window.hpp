#pragma once
#include "placedb.hpp"
#include <array>

// Exact two-row subset DP. Costs[r][i][x] may be +infinity (forbidden).
// Positions use row-local sites; widths may differ between rows.
struct WindowProblem {
    int cells = 0;
    std::array<int, 2> length{};
    std::array<std::vector<int>, 2> width;
    std::array<std::vector<std::vector<double>>, 2> cost;
};
struct WindowSolution {
    double cost = 0;
    bool feasible = false;
    std::vector<int> row, site;
};
WindowSolution solveWindow(const WindowProblem&);

// v4: the same DP for 1 - 3 rows (at most 12 cells); rows are combined over
// all ordered partitions of the cell set (3^k for three rows).
struct MultiProblem {
    int cells = 0;
    std::vector<int> length;                              // per row: sites
    std::vector<std::vector<int>> width;                  // [row][cell]
    std::vector<std::vector<std::vector<double>>> cost;   // [row][cell][site], +inf = forbidden
};
WindowSolution solveMulti(const MultiProblem&);

// Exact HPWL-x ordering in one row with the incumbent gaps kept by rank.
// Every touched net supplies its window-member mask and outside x extrema.
struct OrderNet {unsigned members;double outsideLow, outsideHigh;};
struct OrderProblem {
    double origin=0, spacing=1;
    std::vector<int> widths, gaps;
    std::vector<double> centerOffsets;
    std::vector<OrderNet> nets;
};
struct OrderSolution {double cost=0;std::vector<int> order;};
OrderSolution solveOrder(const OrderProblem&);
void reorderWindows(const PlaceDB&, Placement&, int passes, bool verbose=false, double until=1e30);

struct WindowOptions {
    int passes = 2;
    int perRow = 4;       // At most 8 cells; exhaustive subset state, not fixed order.
    int maxSites = 256;   // Hard span bound to control work on sparse rows.
    double timeLimit = 1e30;   // v4: seconds for one call (checked between windows)
    double tol = 1e-5;         // v4: a pass gaining less than this fraction counts as quiet
    int rows = 2;              // v4: rows per window in displacement mode (2 or 3)
    int perRow3 = 3;           // v4: cells per row in a three-row window
    int threads = 1;           // v4: displacement sweeps in parallel row bands
    int verbose = 0;
};
// Displacement mode (v4): exact inside each selected window under the global
// maximum displacement cap, swept until two passes in a row gain nothing.
// Returns the number of improved windows.
// v4: which windows were already solved without gain, kept between calls
// (positions changed in between, by any algorithm, invalidate them).
struct WindowMemo {
    std::vector<long> mod;                                  // per cell: stamp of its last move
    std::array<std::array<std::vector<long>, 2>, 2> seen;   // [2 or 3 rows][pass parity][anchor]
    std::vector<int> seg, site;                             // placement the memo describes
    long clock = 1;
    void sync(const PlaceDB&, const Placement&);
};
long optimizeWindowsDisp(const PlaceDB&, Placement&, const WindowOptions&, WindowMemo* memo = nullptr);
// v4: two-row and three-row sweeps alternated until neither improves.
long windowSweeps(const PlaceDB&, Placement&, const WindowOptions&, WindowMemo* memo = nullptr);
// CodexChainDP: pair the current neighbourhood with the global target's
// neighbourhood, including distant rows and disjoint intervals in the same row.
// The inherited subset kernel is exact only inside these selected regions.
long targetedWindowsDisp(const PlaceDB&, Placement&, const WindowOptions&, int maxRoots = 2000);
// v4: lowers the maximum displacement with windows around the worst cell
// (opt.passes = iteration limit). Returns the number of applied windows.
long reduceMaxWindows(const PlaceDB&, Placement&, const WindowOptions&);
// HPWL mode (CodexWindowDP): proposes with separable frozen-pin costs, then
// accepts ONLY after recomputing the actual affected-net union.
void optimizeWindowsHPWL(const PlaceDB&, Placement&, const WindowOptions&);
