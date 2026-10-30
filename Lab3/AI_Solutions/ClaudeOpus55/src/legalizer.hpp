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
    bool verbose = false;
};

// Returns false (with err set) when some cell cannot be placed anywhere.
bool legalize(const PlaceDB& db, Placement& out, const LegalizeOptions& opt, std::string& err);
