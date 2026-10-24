#pragma once
#include "placedb.hpp"

// Negotiated site congestion over all rows reachable under a Manhattan cap.
// Success is a checked contiguous-cell placement. Failure is UNKNOWN, never
// an infeasibility certificate: the underlying packing problem is integral.
bool repairCapPreservingPrototype(const PlaceDB&, Placement&, double cap, double until, unsigned seed=1, bool preserve=false);
// Keep row membership and order. Rows that cannot meet cap stay unchanged.
bool repairCapOrderedRowsPrototype(const PlaceDB&, Placement&, double cap);
void globalMinimaxPrototype(const PlaceDB&, Placement&, double seconds, double totalRatio,
                   int threads, bool verbose=false);
