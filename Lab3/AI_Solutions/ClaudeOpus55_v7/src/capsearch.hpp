// capsearch.hpp — v5: lower the maximum displacement by reassigning rows.
#pragma once
#include <cstdint>

#include "placedb.hpp"
#include "window.hpp"

struct RepairOptions {
    double phase1Until = 0;      // wall time (wallSeconds) to give up reaching lateness 0
    double phase2Until = 0;      // wall time limit of phase 2
    long phase2PerCell = 2000;   // phase 2 moves per cell of the touched segments
    double phase2Temp = 0.05;    // phase 2 start temperature / average displacement
    uint64_t seed = 1;
    bool verbose = false;
};

struct CapSearchOptions {
    double seconds = 6;          // budget of the whole descent
    long phase2PerCell = 2000;
    double phase2Temp = 0.05;
    double totalRatio = 1.0048;   // accepted total / total before the search
    int threads = 1;             // window DP threads
    WindowOptions dp;            // window DP settings used after every repair
    bool verbose = false;
};

struct CapSearchResult {
    int attempts = 0, feasible = 0, accepted = 0;
    double startMax = 0, endMax = 0, startTotal = 0, endTotal = 0, seconds = 0;
};

// One cap D: find a legal placement with every displacement <= D, starting
// from p. State = which segment each cell is in; a segment is "on time" when
// its cells, in Schrage's order (non-delay earliest-deadline with release
// dates), fit their cap intervals. Phase 1 anneals the total lateness to 0 with
// relocate / swap moves between segments; phase 2 anneals the segments' exact
// L1 cost while keeping them on time. Changed segments are then placed by L1
// clumping under the cap intervals. Returns false (p unchanged) when no legal
// placement under D was found before `until` — that is NOT a proof that none
// exists. On success p is legal and its maximum is <= D.
bool rowRepair(const PlaceDB& db, Placement& p, double D, const RepairOptions& opt);

// Descent: D = max * (1 - step); a success (after the window DP has won back
// total displacement under the new cap) is kept when the total stays within
// totalRatio of the starting total, and the step grows; a failure halves it.
// p only changes to a legal placement with a lower maximum.
CapSearchResult capSearch(const PlaceDB& db, Placement& p, const CapSearchOptions& opt, WindowMemo* memo = nullptr);

// Largest over cells of the distance to the nearest legal site of the cell
// (ignoring all other cells): a lower bound on the maximum displacement.
double individualMaxBound(const PlaceDB& db);
