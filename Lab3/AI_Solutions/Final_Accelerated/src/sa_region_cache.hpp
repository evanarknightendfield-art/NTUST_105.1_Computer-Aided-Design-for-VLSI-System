#pragma once

#include "sa_bbox.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sa_detail {

// Per-worker exact optimal-region memoization. Call advance before each
// committed/imported mutation batch, then touch every affected net, including
// nets whose bounding box did not change. Speculative delta/revert leaves this
// cache alone. A result depends on all incident pins, not just net extrema.
class SARegionCache {
public:
    SARegionCache(std::size_t cells, std::size_t nets)
        : netRev_(nets, 0), cellEpoch_(cells, 0), boxes_(cells) {}

    void advance() {
        if (++epoch_ == 0) clear();
    }

    void touch(int net) { netRev_[net] = epoch_; }

    void clear() {
        std::fill(netRev_.begin(), netRev_.end(), 0);
        std::fill(cellEpoch_.begin(), cellEpoch_.end(), 0);
        epoch_ = 1;
    }

    bool get(int cell, const int* begin, const int* end, SABox& result) const {
        if (begin == end) return false;
        const std::uint64_t cached = cellEpoch_[cell];
        if (!cached) return false;
        for (const int* it = begin; it != end; ++it)
            if (netRev_[*it] > cached) return false;
        result = boxes_[cell];
        return true;
    }

    void put(int cell, const SABox& box) {
        boxes_[cell] = box;
        cellEpoch_[cell] = epoch_;
    }

private:
    std::uint64_t epoch_ = 1;
    std::vector<std::uint64_t> netRev_, cellEpoch_;
    std::vector<SABox> boxes_;
};

}  // namespace sa_detail
