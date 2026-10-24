#pragma once
#include "placedb.hpp"
#include <string>

struct OptimizeOptions {
    int paretoPasses = 2;
    int polishPasses = 20;      // v6: was 3; past 3 only while a pass gains >= 1 %, and never a pass that no longer fits
    int groupSize = 32;
    bool verbose = false;
    double until = 1e30;   // v4: stop passes at this wallSeconds()
};

// Additional deterministic optimization; source and algorithms documented in README.
void paretoDisplacement(const PlaceDB&, Placement&, const OptimizeOptions&);
void polishHPWL(const PlaceDB&, Placement&, const OptimizeOptions&, const std::string& trace = "");

// Exposed for brute-force verification of the assignment kernel.
struct Assignment { std::vector<int> column; double cost = 0; bool feasible = false; };
Assignment minAssignment(const std::vector<double>& costs, int n, double forbidden = 1e100);
Assignment paretoAssignment(const std::vector<double>& distances, int n);
