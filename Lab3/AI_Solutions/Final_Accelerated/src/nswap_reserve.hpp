#pragma once
#include <algorithm>

// Shared pure policy arithmetic for the annealed path's optional nswap reserve.
// No timing, placement state, or HPWL weight inference belongs in these helpers.
namespace nswap_reserve {

inline bool eligible(bool enabled, bool predictedSA, bool polishOnly,
                     double windowRate, double nswapTime) {
    return enabled && predictedSA && !polishOnly && windowRate > 0 && nswapTime > 0;
}

// The annealed path keeps its existing two-second limit and an explicit or
// resolved automatic duration. The polish-only adaptive policy is separate.
inline double allowanceCap(double nswapTime, double remaining) {
    return std::max(0.0, std::min({nswapTime, 2.0, remaining}));
}

inline double estimate(double firstPassSeconds, double factor, double cap) {
    if (!(firstPassSeconds > 0) || !(factor > 0) || !(cap > 0)) return 0;
    return std::min(factor * firstPassSeconds, cap);
}

// Main may compute its ordinary budget before the reserve. Never let the
// reserve undo the same duration/deadline cap used when shortening window DP.
// A polish-only caller retains its independently computed adaptive budget.
inline double apply(double ordinaryBudget, double reserveSeconds, double cap, bool polishOnly = false) {
    if (polishOnly) return ordinaryBudget;
    return std::max(0.0, std::min(std::max(ordinaryBudget, reserveSeconds), cap));
}

} // namespace nswap_reserve
