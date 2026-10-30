// Exact SA bbox maintenance with one pin witnessing each boundary.
#pragma once
#include "sa_bbox.hpp"
#include <utility>

namespace sa_detail {
struct SAWitness { int xl = -1, xh = -1, yl = -1, yh = -1; };
struct SAWitnessBox { SABox b; SAWitness owner; };

inline void expandWitness(SAWitnessBox& box, int v, const SAPoint& p) {
    if (p.x < box.b.xl) { box.b.xl = p.x; box.owner.xl = v; }
    if (p.x > box.b.xh) { box.b.xh = p.x; box.owner.xh = v; }
    if (p.y < box.b.yl) { box.b.yl = p.y; box.owner.yl = v; }
    if (p.y > box.b.yh) { box.b.yh = p.y; box.owner.yh = v; }
}

// Expand with every moved pin's final position before repairing. Point must
// expose the final positions of all pins; Scan returns exact requested axes.
template<class Point, class Scan>
inline SAWitnessBox repairWitness(SAWitnessBox box, Point&& point, Scan&& scan) {
    const bool scanX = box.owner.xl < 0 || box.owner.xh < 0 ||
        point(box.owner.xl).x != box.b.xl ||
        point(box.owner.xh).x != box.b.xh;
    const bool scanY = box.owner.yl < 0 || box.owner.yh < 0 ||
        point(box.owner.yl).y != box.b.yl ||
        point(box.owner.yh).y != box.b.yh;
    if (scanX || scanY) {
        const SAWitnessBox exact = std::forward<Scan>(scan)(scanX, scanY);
        if (scanX) {
            box.b.xl = exact.b.xl; box.b.xh = exact.b.xh;
            box.owner.xl = exact.owner.xl; box.owner.xh = exact.owner.xh;
        }
        if (scanY) {
            box.b.yl = exact.b.yl; box.b.yh = exact.b.yh;
            box.owner.yl = exact.owner.yl; box.owner.yh = exact.owner.yh;
        }
    }
    return box;
}
} // namespace sa_detail
