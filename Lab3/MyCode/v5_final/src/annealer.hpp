// annealer.hpp — Stage 2: simulated annealing on total HPWL with legality-preserving moves.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "placedb.hpp"

struct SAOptions {
    uint64_t seed = 1;
    double timeLimit = 60;        // seconds; moves per step shrink to fit
    double movesPerCell = 5000;   // attempted moves = movesPerCell * #movable cells ...
    double minMoves = 5e6;        // ... but at least this many (small designs anneal properly)
    double maxMoves = 6e7;        // ... and at most this many (big designs keep ~1 min)
    int steps = 100;              // temperature steps (T0 -> T0 * tEndRatio, geometric)
    double p0 = 0.8;              // acceptance probability of an average uphill move at T0
    double tEndRatio = 1e-3;
    double pTarget = 0.5;         // chance a move aims at the cell's optimal region
    double win0 = 0.05;           // initial range window, as a fraction of the chip
    double maxWin = 0.1;          // largest range window, as a fraction of the chip
    double pGlobalSwap = 0.04;   // cold multi-candidate global/vertical swaps
    double pRowOpt = 0.01;        // chance of a linear-placement window move
    int rowWin = 12;              //     cells per linear-placement window
    double pIsm = 0.01;           // chance of an independent-set-matching move
    int ismSize = 12;             //     cells per matching
    double ismReach = 3;          //     candidates within this many minimum windows in x (+-2 rows)
    double polishFrom = 0.5;      // rowopt / ism only from this fraction of the temperature steps on
    double minWinCells = 4;       // smallest window: this many average cell widths ...
    double minWinRows = 1.5;      // ... and this many rows
    bool clampTarget = false;     // optimal-region moves also obey the window (worse in tests)
    double pReorder = 0.1;        // chance of a local-reordering move (3 neighbours)
    // experiment A: override only the final 30% of temperature steps.
    // Negative (default -1) disables it; .02 is the proposed candidate.
    double coldReorder = -1;
    int nearTries = -1;           // if the target fails, cells/gaps tried on each side of it;
                                  //     -1 = auto: 3 when utilization >= 0.72, else 0
    int threads = 0;              // parallel SA worker threads (0 = auto: hardware, at most 8)
    // a short anneal starts colder. After step 0 (at T0) the measured rate gives the attempts
    // per cell the whole limit buys; below autoPerCell the remaining steps restart from T0 x r with
    // r = clamp((perCell / autoPerCell)^autoExp, autoMinRatio, 1) and cool geometrically to the same
    // final temperature T0 x tEndRatio (a complete schedule, only its hot start is shortened).
    // tStartRatio < 1 forces r. 0 / 1 = off.
    // autoSync: the same short anneal also merges its bands half as often (syncMoves x 2): on L1
    // (160 attempts per cell) the merges took 15 % of the time; 3 seeds -3.0 %, with the cold start -4.5 %.
    double tStartRatio = 1;
    double autoPerCell = 0, autoExp = 1, autoMinRatio = 0.05;
    bool autoSync = false;
    int forceThreads = 0;         // (--sa-threads, measurement): this many SA workers, no memory cap
    int minBandRows = 12;         // rows per band at least (fewer threads on short chips)
    double syncMoves = 1.0;       // bands merge after about this many attempts per cell
    double starvedAttemptsPerCell = 100;   // parallel SA: fewer projected attempts per cell -> keep only ...
    double starvedMinFraction = 0.1;       // ... this fraction of timeLimit (the rest goes to the polish); 0 = off
    std::string tracePath;        // CSV trace for the report plot (empty = none)
    bool verbose = false;
};

// Tested separately from timing and RNG. Keep the original probability exactly
// when disabled; direct API callers get a bounded override. NaN also disables
// the override. CLI validation can reject nonfinite/out-of-range input earlier.
inline double saReorderProbability(const SAOptions& opt, int step, int steps) {
    if (!(opt.coldReorder >= 0) || steps <= 0 || step < 0) return opt.pReorder;
    const long long firstColdStep = (7LL * steps + 9) / 10;
    return step >= firstColdStep ? std::clamp(opt.coldReorder, 0.0, 1.0) : opt.pReorder;
}

// diagnostics only (-v): attempted proposal families, including failures.
// Ordinary successful proposals are classified by their resulting move;
// failed ordinary proposals go in target_failed because no move was formed.
// Global search remains separate even when its winning candidate is a swap.
struct SAMoveProfile {
    enum Kind { slide, move, swap, reorder, rowopt, ism, global, target_failed, count };
    long long attempts = 0, proposed = 0, accepted = 0, improving = 0;
    double localDelta = 0;  // accepted band-local HPWL change; negative is better
    long long timingSamples = 0;
    double sampledSeconds = 0;  // 1/64 attempts timed; excludes merge and profile bookkeeping
};

struct SAStageProfile {
    int firstStep = 0, lastStep = 0;
    double firstTemperature = 0, lastTemperature = 0;
    double initialHPWL = 0, finalHPWL = 0, bestHPWL = 0;
    double wallSeconds = 0, moveWallSeconds = 0, mergeWallSeconds = 0;
    std::array<SAMoveProfile, SAMoveProfile::count> moves{};
};

struct SAResult {
    double initial = 0, best = 0;
    long long attempts = 0, accepted = 0;
    double seconds = 0;
    double limitCut = 0;   // seconds of timeLimit given up by a starved SA (main hands them to the polish)
    // copies of existing search values, also available in quiet runs.
    // No additional HPWL scans, clock samples, RNG draws, or per-move counters.
    // current is the last incumbent BEFORE restoring best. Cached HPWL values
    // retain the algorithm's existing floating-point accumulation order.
    double current = 0;
    double temperature0 = 0, temperatureTarget = 0;
    double lastTemperature = 0, minimumTemperature = 0; // actually used for at least one attempt
    int threads = 0, stepsPlanned = 0, stepsStarted = 0, stepsCompleted = 0, lastStep = -1;
    // A completed step exhausted its scheduled work; a deadline-cut partial
    // step is started only. Completing the schedule does not mean the exact
    // target temperature was used (the existing loop cools after each step).
    bool scheduleComplete = false;
    long long syncRounds = 0, failed = 0;
    // Existing parallel phase timers: sum of longest worker batches, then
    // load/export/merge/import phase wall time. They exclude band-edge setup,
    // best-placement copies and other driver work; neither is total CPU time.
    // Serial runs expose 0 because that path has no comparable timer split.
    double moveSeconds = 0, mergeSeconds = 0;
    double effectiveTimeLimit = 0, step0Seconds = 0;
    double projectedAttemptsPerCell = -1; // step-0 calibration; -1 if not reached
    double coldStartRatio = 1, finalSyncMoves = 0;
    const char* stopReason = "not-run"; // empty / steps / time / no-work
    std::vector<SAStageProfile> stages;  // empty unless verbose; buckets of ten temperature steps
};

// Improves P in place; P stays legal after every accepted move, and the best
// placement seen is returned.
SAResult anneal(const PlaceDB& db, Placement& P, const SAOptions& opt);
// number of worker threads anneal will use for this design.
int saThreads(const PlaceDB& db, const SAOptions& opt);
