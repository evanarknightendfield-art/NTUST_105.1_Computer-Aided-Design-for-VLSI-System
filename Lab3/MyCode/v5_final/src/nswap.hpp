// nswap.hpp — displacement-neutral HPWL swaps at the end of Stage 1.
//
// The displacement objective sum |x - gx| + |y - gy| is flat along many directions: two cells
// of the same size whose global positions lie on the same side of both their slots (in x and
// in y) can trade slots without changing the total, and the window DP / matching steps pick
// any one of these equal-cost assignments without looking at the nets. This pass swaps
// same-size cells only when the total displacement does not increase, neither cell exceeds
// the maximum displacement the pass started with, and the HPWL strictly decreases.
#pragma once
#include "placedb.hpp"
#include <vector>

struct NeutralSwapOptions {
    double until = 1e30;        // wallSeconds deadline, checked between chunks
    int threads = 1;            // proposals in parallel; the result does not depend on it
    int passes = 20;
    int rowsAround = 1;         // candidate rows on each side of a probe row
    int cellsAround = 3;        // same-size candidates per side of a probe x in each segment
    int chunk = 256;            // cells per deterministic snapshot/deadline chunk
    double quiet = 0.02;        // stop when a pass gains less than this share of the first pass
    bool verbose = false;
    double minRelativeRate = 0; // completed-pass HPWL gain / current HPWL / second; 0 preserves 
    // completed-pass gain / HPWL at that pass's start, without elapsed
    // time. 0 disables this rule; callers select it instead of minRelativeRate.
    // The existing quiet option remains an independent gain / first-pass-gain rule.
    double minFractionalGain = 0;
    double referenceHPWL = 0;   // exact starting HPWL; computed only if a HPWL-based policy needs it
    bool capturePassStats = false; // diagnostics only; no extra exact HPWL calculation
    // visit cells by position (row, then x; snapshot at the start of each pass, reversed on odd
    // passes like the id order) instead of by id. Ids are spatially random on the large designs, so
    // the id order touches memory at random; this changes which swaps are found first.
    bool spatialOrder = false;
};

struct NeutralSwapPassStats {
    int pass = 0;              // one-based; a deadline-cut partial pass is included
    long visited = 0, swaps = 0;
    bool complete = false;
    bool hpwlKnown = false;    // false when no positive reference or HPWL-based policy was supplied
    double seconds = 0;       // order + proposals/apply; excludes setup and outer checks
    double hpwlBefore = 0, hpwlAfter = 0;
    double hpwlGain = 0;       // signed accepted change, as in NeutralSwapResult
    double fractionalGain = 0; // -hpwlGain / max(1, hpwlBefore), valid only when hpwlKnown
    double firstGainRatio = 0; // gain / first-pass gain, or 0 when that first gain is zero
    double relativeRate = 0;   // fractionalGain / max(1e-6, seconds), valid when hpwlKnown
};

struct NeutralSwapResult {
    long swaps = 0, candidates = 0, neutral = 0, proposals = 0;
    int passes = 0;
    double hpwlGain = 0;        // sum of the accepted (negative) HPWL changes
    double dispChange = 0;      // sum of the accepted displacement changes (<= 0)
    bool cut = false;           // stopped by the deadline
    const char* stop = "none";  // deadline / rate / fraction / quiet / passes
    // stage-level diagnostics in both quiet and verbose runs. Setup
    // includes displacement, buckets/net boxes, workers and initial HPWL.
    // First-pass time includes order construction/sort plus proposals/apply,
    // excludes setup, and may describe a deadline-cut partial pass.
    double setupSeconds = 0;
    double firstPassSeconds = 0;
    double firstOrderSeconds = 0; // subset of firstPassSeconds, not additive
    bool firstPassComplete = false; // every first-pass cell was visited
    std::vector<NeutralSwapPassStats> passStats; // empty unless capturePassStats is enabled
};

NeutralSwapResult neutralSwap(const PlaceDB& db, Placement& p, const NeutralSwapOptions& opt);
