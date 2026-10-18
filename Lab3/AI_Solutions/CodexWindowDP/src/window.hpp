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
void reorderWindows(const PlaceDB&, Placement&, int passes, bool verbose=false);

struct WindowOptions {
    int passes = 2;
    int perRow = 4;       // At most 8 cells; exhaustive subset state, not fixed order.
    int maxSites = 256;   // Hard span bound to control work on sparse rows.
    bool verbose = false;
};
// Displacement mode is exact within each selected window under the global
// maximum displacement cap. HPWL mode proposes with separable frozen-pin
// costs, then accepts ONLY after recomputing the actual affected-net union.
void optimizeWindows(const PlaceDB&, Placement&, const WindowOptions&, bool hpwl = false);
