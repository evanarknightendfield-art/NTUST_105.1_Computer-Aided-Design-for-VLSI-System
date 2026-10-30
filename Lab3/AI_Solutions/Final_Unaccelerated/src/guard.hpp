#pragma once
#include "placedb.hpp"
#include <cstdio>
#include <exception>

// Every optional optimization commits only after its invariant is checked.
template<class Step, class Check>
bool guardedStep(const char* name, Placement& p, Step&& step, Check&& ok) {
    Placement keep = p;
    try {
        step();
        if (ok()) return true;
        std::fprintf(stderr, "warning: %s broke an invariant, step skipped\n", name);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "warning: %s failed (%s), step skipped\n", name, e.what());
    } catch (...) {
        std::fprintf(stderr, "warning: %s failed (non-standard exception), step skipped\n", name);
    }
    p = std::move(keep);
    return false;
}
