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
    bool matching = true;            // same-width min-cost matching around the worst cells
    bool rowOpt = true;              // exact per-row re-placement with the order fixed
    std::string spread = "auto";     // flow-based spreading before Abacus: off, on, or auto
                                     //     (run with and without, keep the better)
    int binRows = 2;                 //     bin height in rows
    double binCells = 16;            //     bin width in average cell widths
    int spreadRounds = 4;            //     bound relaxations (x1.5 each)
    int sweepRows = 6, sweepSites = 120;   // total-displacement matching windows
    double totalTol = 5e-4;          // total pass stops when a pass gains less than this fraction
    int finishRounds = 4;            // final row-opt / matching / local-move rounds
    bool swapPush = false;           // swap-with-push move (mixed results, 1.7x slower; off)
    bool windowDP = true;            // two-row window DP inside the final rounds
    int windowCells = 4;             //     cells per row in a two-row window
    int windowCells3 = 3;            //     cells per row in a three-row window
    double until = 1e30;             // refinement stops at this wallSeconds (Abacus always finishes)
    int threads = 1;                 // window DP worker threads
    bool parallelPipelines = true;  // overlap auto plain/spread when threads >= 2
    bool verbose = false;
};

// Time saved by overlapping independent pipelines is removed from later
// absolute Stage 1 deadlines, rather than given to another refinement step.
struct LegalizeStats {
    double pipelineOverlap = 0;
    bool parallelPipelines = false;
};

struct WindowMemo;
// Returns false (with err set) when some cell cannot be placed anywhere.
// memo (optional) = the window memo of the final rounds, for main's later window DPs.
bool legalize(const PlaceDB& db, Placement& out, const LegalizeOptions& opt, std::string& err, WindowMemo* memo = nullptr,
              LegalizeStats* stats = nullptr);
