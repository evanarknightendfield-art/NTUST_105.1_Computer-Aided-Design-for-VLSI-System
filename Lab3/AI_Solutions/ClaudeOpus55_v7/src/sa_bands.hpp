#pragma once
#include "placedb.hpp"
#include <vector>

namespace sa_detail {
// Sweep both sides of the balanced boundaries. Unlike alternating only the
// balanced and half-band-left layouts, the right-shifted layout also lets
// cells near the top boundary share a worker with their neighbours above it.
// The phase depends only on the completed batch number, never worker timing.
inline std::vector<int> bandEdges(const PlaceDB& db, const Placement& p,
                                  int bands, long long round, int minRows) {
    const int rows = static_cast<int>(db.rows.size());
    std::vector<double> count(rows, 0);
    for (int v : db.movable) ++count[db.segs[p.seg[v]].row];
    double total = 0;
    for (double n : count) total += n;
    constexpr double offsets[] = {1.0, 0.5, 1.25, 0.75};
    const double unit = total / bands;
    double next = unit * offsets[round % 4], acc = 0;
    std::vector<int> edge{0};
    for (int r = 0; r < rows; ++r) {
        acc += count[r];
        if (acc >= next && static_cast<int>(edge.size()) < bands &&
            r + 1 < rows && r + 1 - edge.back() >= minRows) {
            edge.push_back(r + 1);
            next += unit;
        }
    }
    if (rows - edge.back() < minRows && edge.size() > 1) edge.pop_back();
    edge.push_back(rows);
    return edge;
}
}  // namespace sa_detail
