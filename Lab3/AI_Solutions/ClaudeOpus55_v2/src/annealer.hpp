// annealer.hpp — Stage 2: simulated annealing on total HPWL with legality-preserving moves.
#pragma once
#include <cstdint>
#include <string>

#include "placedb.hpp"

struct SAOptions {
    uint64_t seed = 1;
    double timeLimit = 60;        // seconds; moves per step shrink to fit
    double movesPerCell = 2000;   // attempted moves = movesPerCell * #movable cells ...
    double minMoves = 5e6;        // ... but at least this many (small designs anneal properly)
    int steps = 100;              // temperature steps (T0 -> T0 * tEndRatio, geometric)
    double p0 = 0.8;              // acceptance probability of an average uphill move at T0
    double tEndRatio = 1e-3;
    double pTarget = 0.5;         // chance a move aims at the cell's optimal region
    double win0 = 0.05;           // initial range window, as a fraction of the chip
    double maxWin = 0.1;          // v2: largest range window, as a fraction of the chip
    double minWinCells = 4;       // smallest window: this many average cell widths ...
    double minWinRows = 1.5;      // ... and this many rows
    bool clampTarget = false;     // optimal-region moves also obey the window (worse in tests)
    double pReorder = 0.1;        // v2: chance of a local-reordering move (3 neighbours)
    int nearTries = -1;           // v2: if the target fails, cells/gaps tried on each side of it;
                                  //     -1 = auto: 3 when utilization >= 0.72, else 0
    std::string tracePath;        // CSV trace for the report plot (empty = none)
    bool verbose = false;
};

struct SAResult {
    double initial = 0, best = 0;
    long long attempts = 0, accepted = 0;
    double seconds = 0;
};

// Improves P in place; P stays legal after every accepted move, and the best
// placement seen is returned.
SAResult anneal(const PlaceDB& db, Placement& P, const SAOptions& opt);
