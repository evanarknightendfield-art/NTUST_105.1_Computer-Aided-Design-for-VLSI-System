// legalizer.hpp — Stage 1: legal placement with small total and maximum displacement.
#pragma once
#include <string>
#include <utility>
#include <vector>

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
    int maxThreads = 0;              // max/matching workers only; 0 follows threads
    bool parallelPipelines = true;  // overlap auto plain/spread when threads >= 2
    double tieBand = 0;              // |score - 2| < tieBand -> both pipelines run their final rounds
    double maxUntil = -1;            // end of the Stage 1 refinement (deadline mode); <= 0 = schedule
    double maxShare = 0.35;          // legacy max pass share; ignored by maxComplete
    double maxRate = 0.02;           // max pass stops when the max falls < maxRate of itself per second
    double maxPolicyMinSeconds = 0.25; // first greedy rate/tail check interval floor (legacy .25s)
    double maxFloor = 0;             // max pass also stops at maxFloor x the maximum after Abacus (0 = off)
    double maxTail = 0.001;          // max pass stops when > maxTail x cells lie within 10 % of the max (0 = off)
    bool maxPrune = false;           // heuristic row prune in the max pass (searchFor)
    int abacusFast = 2;              // 0 = scorer, 1 = arrays, 2 = exact bounds, 3 = also approximate-cluster bound
    int maxFast = 0;                 // 1 row parallelism, 2 cache, 3 deferred push stores; 4 logarithmic candidates
                                    // 5 bounded endpoint scans; 6 = 5 + static weighted rows; 7 = 5 + O(1) evict room
    bool maxComplete = false;
    bool releaseMaxScratch = false;  // release only after the entire max/matching phase; retain nested reuse
    std::vector<std::pair<int, int>> maxCheckModes;
    bool maxCheckMatching = false;           // (--max-check-matching): also the matching phase, final placement compared
    long maxCheckMoves = 0;                  // (--max-check-moves): compare only the first N moves (0 = all)   // (--max-check-modes): (maxFast, threads), first = reference        // use maxUntil directly; measured next-work admission, no tail/floor/rate stop
    double abacusExactWork = 5e9;    // diagnostic: force bounded scoring in small equivalence fixtures
    bool collectHwm = true;         // process high-water RSS at phase boundaries (--phases)
    bool verbose = false;
};

// Time saved by overlapping independent pipelines is removed from later
// absolute Stage 1 deadlines, rather than given to another refinement step.
struct LegalizeStats {
    double pipelineOverlap = 0;
    bool parallelPipelines = false;
    bool maxPhase = false;   // cut max phase or complete work beyond quick legacy convergence; selects downstream guards
    double abacusSeconds = 0; // elapsed mandatory legalizer time in the selected pipeline, excluding spreading/refinement
    // (the selected pipeline): the first max pass and the matching after it
    int maxMoves = 0;
    double maxSeconds = 0, matchSeconds = 0;
    std::string maxWhy = "none", matchWhy = "none";
    // / P1: the plain / spread competition (choice "single" when only one pipeline ran)
    std::string choice = "single";
    double plainTotal = 0, plainMax = 0, spreadTotal = 0, spreadMax = 0, score = 0;
    bool tieFinish = false;                     // P1: both finished, chosen on the finished placements
    double finalScore = 0;
    double finishSeconds = 0;
    // cumulative process VmHWM, not phase-local allocation or selected-pipeline RSS.
    // Across plain/spread, keep the largest valid boundary sample (workers share the process).
    // 0 = boundary not reached / collection disabled; -1 = attempted, unavailable.
    // Max/match sample the first outer pass only; matching's nested max does not overwrite max.
    long hwmAbacusKiB = 0, hwmMaxKiB = 0, hwmMatchKiB = 0, hwmFinishKiB = 0;
};

struct WindowMemo;
// Returns false (with err set) when some cell cannot be placed anywhere.
// memo (optional) = the window memo of the final rounds, for main's later window DPs.
bool legalize(const PlaceDB& db, Placement& out, const LegalizeOptions& opt, std::string& err, WindowMemo* memo = nullptr,
              LegalizeStats* stats = nullptr);
// (--abacus-check): Abacus alone in modes 0 / 1 / 2 / 3, timed; true if all placements are identical.
bool abacusCheck(const PlaceDB& db, const LegalizeOptions& opt);
// one worst-cell pass, from the same Abacus + row-opt placement, no wall-clock/rate stops.
// Compares every committed move (including pushed cells) for original and modes 1..7 at 1/N threads.
// With releaseMaxScratch, the first configuration keeps scratch; later ones release after greedy + matching.
bool maxRefineCheck(const PlaceDB& db, const LegalizeOptions& opt);
// exercise the actual eviction room helper on a legal placement, without mutating zero-width cells.
bool maxEvictRoomCheck(const PlaceDB& db, const Placement& placement, int* direct = nullptr, int* fallback = nullptr);
