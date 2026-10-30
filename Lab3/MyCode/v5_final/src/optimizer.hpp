#pragma once
#include "placedb.hpp"
#include <string>

struct PolishStats {
    long passes = 0, slideVisited = 0, relocateVisited = 0;
    long slideChanges = 0, relocateChanges = 0, assignmentChanges = 0, reorderChanges = 0;
    long rowMapBuilds = 0, exactChecks = 0;
    double setupSeconds = 0, slideSeconds = 0, relocateSeconds = 0;
    double matchingSeconds = 0, reorderSeconds = 0, validationSeconds = 0;
    double hpwlDrift = 0; // incremental minus independently recomputed HPWL (finalCheck mode)
};

struct OptimizeOptions {
    int paretoPasses = 2;
    int polishPasses = 20;      // was 3; past 3 only while a pass gains >= 1 %, and never a pass that no longer fits
    int groupSize = 32;
    bool verbose = false;
    double until = 1e30;   // stop passes at this wallSeconds
    int threads = 1;       // pareto groups in parallel (disjoint cells: same result for any count)
    // from the third pareto pass on, a pass is run only while the last one lowered the maximum by
    // >= minMaxGain or the HPWL by >= minHpwlGain (fractions; both 0 = paretoPasses passes)
    double minMaxGain = 0, minHpwlGain = 0;
    bool earlyExit = false;   // a polish sub-phase that starts after `until` returns before building anything
    bool boxCache = false;    // polish keeps each cell's external net boxes until a pin of its nets moves (same result)
    bool partialPass = false; // start another polish pass even if a whole one no longer fits (it is cut at until)
    // experiments are opt-in until equal-budget measurements establish their benefit.
    bool spatialTraversal = false; // snapshot current row/x order for slide and relocate; node ids stay unchanged
    bool reuseRowMap = false;      // maintain one row map across every polish phase and pass
    bool finalCheck = false;       // track accepted HPWL deltas; exact validation at entry/exit, rollback on failure
    PolishStats* polishStats = nullptr; // optional counters; verbose also prints phase throughput
};

// Additional deterministic optimization; source and algorithms documented in README.
void paretoDisplacement(const PlaceDB&, Placement&, const OptimizeOptions&);
void polishHPWL(const PlaceDB&, Placement&, const OptimizeOptions&, const std::string& trace = "");

// Exposed for brute-force verification of the assignment kernel.
struct Assignment { std::vector<int> column; double cost = 0; bool feasible = false; };
Assignment minAssignment(const std::vector<double>& costs, int n, double forbidden = 1e100);
Assignment paretoAssignment(const std::vector<double>& distances, int n);
