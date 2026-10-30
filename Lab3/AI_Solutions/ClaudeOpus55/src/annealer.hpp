// annealer.hpp — Stage 2: simulated annealing on total HPWL with legality-preserving moves.
#pragma once
#include <cstdint>
#include <string>

#include "placedb.hpp"

struct SAOptions {
    uint64_t seed = 1;
    double timeLimit = 60;        // seconds; moves per step shrink to fit
    double movesPerCell = 2000;   // attempted moves = movesPerCell * #movable cells
    int steps = 100;              // temperature steps (T0 -> T0 * tEndRatio, geometric)
    double p0 = 0.8;              // acceptance probability of an average uphill move at T0
    double tEndRatio = 1e-3;
    double pTarget = 0.5;         // chance a move aims at the cell's optimal region
    double win0 = 0.05;           // initial range window, as a fraction of the chip
    double minWinCells = 4;       // smallest window: this many average cell widths ...
    double minWinRows = 1.5;      // ... and this many rows
    bool clampTarget = false;     // optimal-region moves also obey the window (worse in tests)
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
