#pragma once
#include "placedb.hpp"

struct ChainOptions {
    int passes = 2;
    int depth = 10;         // maximum number of distinct cells in one complete chain
    int branches = 12;      // occupied candidates retained at each search node
    int expansions = 160;  // maximum search nodes per root
    int roots = 12000;      // worst-displaced roots inspected per pass
    int maxBlockers = 2;    // 1: path; 2: also allow a wide cell to eject two narrow cells
    double seconds = 10;
    bool verbose = false;
};

struct ChainResult {
    long roots = 0, nodes = 0, candidates = 0, accepted = 0;
    long multiBlocker = 0, refreshed = 0;
    int longest = 0;
    bool timedOut = false;
};

// Bounded ejection search across row boundaries. Intermediate displacements may
// rise, but only complete legal chains with strictly lower total displacement
// and no larger maximum are committed. Failure/time/node exhaustion restores
// every tentative move; exceptions restore the input placement.
// This is a heuristic search (beam/depth/position limits), not a global solver.
ChainResult chainDisplacementPrototype(const PlaceDB&, Placement&, const ChainOptions&);
// Repair only cells exceeding D. Complete chains may spend total displacement;
// failure leaves p intact. The caller decides the final total/max tradeoff.
bool repairCapChainsPrototype(const PlaceDB&, Placement&, double D, const ChainOptions&);
