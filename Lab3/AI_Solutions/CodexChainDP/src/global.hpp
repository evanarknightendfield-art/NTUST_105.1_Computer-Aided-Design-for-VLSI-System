#pragma once
#include "placedb.hpp"

// Negotiated site congestion over all rows reachable under a Manhattan cap.
// Success is a checked contiguous-cell placement. Failure is UNKNOWN, never
// an infeasibility certificate: the underlying packing problem is integral.
bool repairCap(const PlaceDB&, Placement&, double cap, double until, unsigned seed=1);
void globalMinimax(const PlaceDB&, Placement&, double seconds, double totalRatio,
                   int threads, bool verbose=false);
