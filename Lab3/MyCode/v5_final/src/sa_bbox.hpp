// SA-only exact bbox update. No shared mutable state or floating-point sums.
#pragma once
#include <algorithm>
#include <utility>

namespace sa_detail {
struct SAPoint { double x, y; };
struct SABox { double xl, xh, yl, yh; };

// A side can shrink only when a moved pin occupied it and no new pin
// reaches it. This includes flat axes and motion along a net boundary.
inline bool preservedAxis(double lo, double hi, double oldLo, double oldHi,
                          double newLo, double newHi) {
    return (oldLo > lo || newLo <= lo) && (oldHi < hi || newHi >= hi);
}

template<class Scan>
inline SABox movedBox(const SABox& before, const SABox& oldMoved,
                      const SABox& newMoved, Scan&& scan) {
    const bool scanX = !preservedAxis(before.xl, before.xh, oldMoved.xl, oldMoved.xh,
                                     newMoved.xl, newMoved.xh);
    const bool scanY = !preservedAxis(before.yl, before.yh, oldMoved.yl, oldMoved.yh,
                                     newMoved.yl, newMoved.yh);
    SABox b{std::min(before.xl, newMoved.xl), std::max(before.xh, newMoved.xh),
            std::min(before.yl, newMoved.yl), std::max(before.yh, newMoved.yh)};
    if (scanX || scanY) {
        const SABox exact = std::forward<Scan>(scan)(scanX, scanY);
        if (scanX) { b.xl = exact.xl; b.xh = exact.xh; }
        if (scanY) { b.yl = exact.yl; b.yh = exact.yh; }
    }
    return b;
}
} // namespace sa_detail
