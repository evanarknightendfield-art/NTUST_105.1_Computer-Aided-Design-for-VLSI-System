#pragma once
#include "placedb.hpp"
#include <array>
#include <limits>

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

// the same DP for 1 - 3 rows (at most 12 cells); rows are combined over
// all ordered partitions of the cell set (3^k for three rows).
struct MultiProblem {
    int cells = 0;
    std::vector<int> length;                              // per row: sites
    std::vector<std::vector<int>> width;                  // [row][cell]
    std::vector<std::vector<std::vector<double>>> cost;   // [row][cell][site], +inf = forbidden
};
WindowSolution solveMulti(const MultiProblem&);
// Diagnostic entry for the exact legacy-kernel oracle; production defaults to 0.
WindowSolution solveMultiKernel(const MultiProblem&, int rowKernel);

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
    double timeLimit = 1e30;   // seconds for one call (checked between windows)
    double tol = 1e-5;         // a pass gaining less than this fraction counts as quiet
    int rows = 2;              // rows per window in displacement mode (2 or 3)
    int perRow3 = 3;           // cells per row in a three-row window
    int threads = 1;           // displacement sweeps in parallel row bands
    int verbose = 0;
    bool guardHPWL = false;        // stop displacement sweeps when marginal dHPWL/dDisp is excessive
    double lowDispGainTol = 0.01;  // pass-0 relative disp gain threshold for slow-diffusion regime
    double maxHpwlRate = 0.52;     // max 2-pass average dHPWL / (-dDisp) before stopping
    int minGuardPasses = 6;        // minimum passes before 2-pass average guardrail stops
    // : every pass is
    // scored value = (relative total gain) - hpwlWeight x (relative HPWL rise); a pass with value < 0
    // is rolled back and ends the sweeps; they also end after two passes in a row whose value per
    // second is below rateStop (what a second of the later steps is worth).
    double rateStop = 0;
    double hpwlWeight = 0;
    // experiment: a rate stop ends only the current 2- or 3-row neighbourhood.
    // Both use the same remaining time and unchanged pass value / rollback rule.
    bool ratePerNeighborhood = false;
    // experiment: reuse DP/cost-table capacities per worker; refill all
    // logical values every window. No cache of placement-dependent results.
    bool reuseScratch = false;
    // 0: original site-then-cell recurrence; 1: cell-then-site candidates
    // followed by an exact skip-first prefix minimum; 2: same recurrence with
    // disjoint-pointer candidate updates. Independent of storage; default off.
    int rowKernel = 0;
    // per-pass HPWL of the rate rule from a per-net array updated for the nets of stamped
    // cells, summed in net order: bit-identical to totalHPWL (checkHpwl compares every pass).
    bool incrementalHpwl = false;
    bool checkHpwl = false;
    // skip the DP of a window whose cells already sit at their own cheapest spots (current
    // cost within rounding of the sum of per-cell minima): its DP cannot pass the improvement test.
    bool lowerBoundSkip = false;
    // after its first pass the sweep ends reserveFactor x that pass's time earlier (the time
    // of one neutral-swap pass, which also visits every cell, on the 200k-cell design)
    double reserveFactor = 0;
    // maximum seconds withheld from this call for neutral swaps.
    // Infinity preserves the existing factor-only policy unless main supplies
    // its resolved annealed nswap duration/deadline cap.
    double reserveCap = std::numeric_limits<double>::infinity();
    // Optional repair footprint. Windows touching this set are solved; the
    // local copy grows with accepted moves during each sweep.
    std::vector<char>* active = nullptr;
};
// Displacement mode: exact inside each selected window under the global
// maximum displacement cap, swept until two passes in a row gain nothing.
// Returns the number of improved windows.
// which windows were already solved without gain, kept between calls
// (positions changed in between, by any algorithm, invalidate them).
// last call's HPWL diagnostics; populated only when verbose is enabled.
// These are complete serial sections / dispatch wall times, not worker samples.
// init and oracle checks are separate from the per-pass update; no reduction is
// reordered for profiling. Counters include passes subsequently rolled back.
struct WindowHpwlProfile {
    unsigned long long incrementalCalls = 0, fullCalls = 0;
    unsigned long long serialUpdates = 0, parallelUpdates = 0;
    unsigned long long scannedCells = 0, stampedCells = 0, nodeNetVisits = 0;
    unsigned long long touchedNets = 0, touchedPins = 0, checkCalls = 0;
    double initNetSeconds = 0, initSumSeconds = 0;
    double collectSeconds = 0, saveSeconds = 0, recomputeSeconds = 0, sumSeconds = 0;
    double fullSeconds = 0, checkSeconds = 0;
};
struct WindowMemo {
    std::vector<long> mod;                                  // per cell: stamp of its last move
    std::array<std::array<std::vector<long>, 2>, 2> seen;   // [2 or 3 rows][pass parity][anchor]
    std::vector<int> seg, site;                             // placement the memo describes
    long clock = 1;
    std::array<int, 2> perRow{{0, 0}}, maxSites{{0, 0}};
    std::array<double, 2> cap{{0, 0}};
    bool hpwlGuarded = false;                               // set when HPWL guardrail stopped sweeps
    bool rateStopped = false;                               // the main DP ended by its rate rule
    // the last displacement sweep: passes, seconds, why it ended, windows skipped by W1's bound
    int lastPasses = 0;
    double lastSeconds = 0;
    const char* lastStop = "none";
    long lastBoundSkips = 0;
    double lastRate = -1;                                   // value per second of its last scored pass
    double firstPassSeconds = 0;                            // its first pass
    long hpwlCheckFailures = 0;                             // (checkHpwl): passes whose two HPWLs differ
    WindowHpwlProfile hpwlProfile;                         // reset for each displacement sweep
    void sync(const PlaceDB&, const Placement&);
};
long optimizeWindowsDisp(const PlaceDB&, Placement&, const WindowOptions&, WindowMemo* memo = nullptr);
// two-row and three-row sweeps alternated until neither improves.
long windowSweeps(const PlaceDB&, Placement&, const WindowOptions&, WindowMemo* memo = nullptr);
// 1 = every displacement-DP pass prints its total (measurement only; --trace-dp)
extern int windowTrace;
// 1 = those pass lines also print the HPWL (measurement only; --stage-hpwl)
extern int windowTraceHPWL;
// pair the current neighbourhood with the global target's
// neighbourhood, including distant rows and disjoint intervals in the same row.
// The inherited subset kernel is exact only inside these selected regions.
long targetedWindowsDisp(const PlaceDB&, Placement&, const WindowOptions&, int maxRoots = 2000);
// lowers the maximum displacement with windows around the worst cell
// (opt.passes = iteration limit). Returns the number of applied windows.
long reduceMaxWindows(const PlaceDB&, Placement&, const WindowOptions&);
struct CapWindowResult {
    long solved = 0, applied = 0;
    int remaining = 0, moved = 0;
    double seconds = 0;
};
// Solve only windows containing a cap violation. Each accepted local solution
// minimizes total displacement under D; cells outside these windows stay put.
// A timeout or unresolved cap leaves p unchanged, including on exceptions.
// If requested, partial receives the legal intermediate layout for a fallback.
bool repairCapWindows(const PlaceDB&, Placement&, double D, const WindowOptions&, CapWindowResult* = nullptr, Placement* partial = nullptr);
// HPWL mode: proposes with separable frozen-pin costs, then
// accepts ONLY after recomputing the actual affected-net union.
void optimizeWindowsHPWL(const PlaceDB&, Placement&, const WindowOptions&);
