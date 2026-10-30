#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace sa_detail {

inline bool useDenseImport(std::size_t cells, std::size_t externalMoved) {
    // Use streaming copies only once random scatter covers at least a quarter.
    // Written without (cells + 3) so even the boundary calculation cannot wrap.
    return externalMoved > 0 && externalMoved >= cells / 4 + (cells % 4 != 0);
}

// All arrays have the same node count. The caller has already exported this
// worker's own moves; every other changed cell appears in lists. Consequently
// entries outside lists already equal the shared arrays, including fixed cells.
// Copying those entries again does not change any coordinate bits. Shared arrays
// remain read-only while each worker imports into its own distinct local arrays.
template<class Point>
void importCells(std::vector<int>& dstSeg, std::vector<int>& dstSite,
                 std::vector<Point>& dstCenters,
                 const std::vector<int>& srcSeg, const std::vector<int>& srcSite,
                 const std::vector<Point>& srcCenters,
                 const std::vector<const std::vector<int>*>& lists,
                 std::size_t externalMoved) {
    if (useDenseImport(srcCenters.size(), externalMoved)) {
        std::copy(srcSeg.begin(), srcSeg.end(), dstSeg.begin());
        std::copy(srcSite.begin(), srcSite.end(), dstSite.begin());
        std::copy(srcCenters.begin(), srcCenters.end(), dstCenters.begin());
    } else {
        for (const auto* list : lists)
            for (int v : *list) {
                dstSeg[v] = srcSeg[v];
                dstSite[v] = srcSite[v];
                dstCenters[v] = srcCenters[v];
            }
    }
}

} // namespace sa_detail
