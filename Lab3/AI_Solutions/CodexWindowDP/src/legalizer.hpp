// legalizer.hpp — Stage 1: legal placement with small total and maximum displacement.
#pragma once
#include <string>

#include "placedb.hpp"

struct LegalizeOptions {
    std::string method = "abacus";   // "tetris" (greedy baseline) or "abacus"
    bool totalCost = true;           // abacus row choice: total displacement change (true) or the new cell only
    bool refineMax = true;           // post-pass: pull the worst cells closer
    bool refineTotal = true;         // post-pass: greedy moves/swaps that cut total displacement
    bool evict = true;               // max pass may also move a cell out of the worst cell's way
    bool matching = true;            // v2: same-width min-cost matching around the worst cells
    bool rowOpt = true;              // v2: exact per-row re-placement with the order fixed
    std::string spread = "auto";     // v2: flow-based spreading before Abacus: off, on, or auto
                                     //     (run with and without, keep the better)
    int binRows = 2;                 //     bin height in rows
    double binCells = 16;            //     bin width in average cell widths
    int spreadRounds = 4;            //     bound relaxations (x1.5 each)
    int sweepRows = 6, sweepSites = 120;   // v3: total-displacement matching windows
    double totalTol = 5e-4;          // v3: total pass stops when a pass gains less than this fraction
    int finishRounds = 4;            // v3: final row-opt / matching / local-move rounds
    bool swapPush = false;           // v3: swap-with-push move (mixed results, 1.7x slower; off)
    bool verbose = false;
};

// Returns false (with err set) when some cell cannot be placed anywhere.
bool legalize(const PlaceDB& db, Placement& out, const LegalizeOptions& opt, std::string& err);
