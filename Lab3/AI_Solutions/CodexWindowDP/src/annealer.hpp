// annealer.hpp — Stage 2: simulated annealing on total HPWL with legality-preserving moves.
#pragma once
#include <cstdint>
#include <string>

#include "placedb.hpp"

struct SAOptions {
    uint64_t seed = 1;
    double timeLimit = 60;        // seconds; moves per step shrink to fit
    double movesPerCell = 5000;   // attempted moves = movesPerCell * #movable cells ...
    double minMoves = 5e6;        // ... but at least this many (small designs anneal properly)
    double maxMoves = 6e7;        // v3: ... and at most this many (big designs keep ~1 min)
    int steps = 100;              // temperature steps (T0 -> T0 * tEndRatio, geometric)
    double p0 = 0.8;              // acceptance probability of an average uphill move at T0
    double tEndRatio = 1e-3;
    double pTarget = 0.5;         // chance a move aims at the cell's optimal region
    double win0 = 0.05;           // initial range window, as a fraction of the chip
    double maxWin = 0.1;          // v2: largest range window, as a fraction of the chip
    double pRowOpt = 0.01;        // v3: chance of a linear-placement window move
    int rowWin = 12;              //     cells per linear-placement window
    double pIsm = 0.01;           // v3: chance of an independent-set-matching move
    int ismSize = 12;             //     cells per matching
    double ismReach = 3;          //     candidates within this many minimum windows in x (+-2 rows)
    double polishFrom = 0.5;      // v3: rowopt / ism only from this fraction of the temperature steps on
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
