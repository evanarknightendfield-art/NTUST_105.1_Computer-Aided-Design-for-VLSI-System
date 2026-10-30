// Shared SA net scan: keep pin order and previous endpoint bits on ties.
#pragma once
#include "sa_bbox.hpp"
#include <cstddef>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace sa_detail {
inline SABox scanSharedBox(const SAPoint* centers, const int* pins, std::size_t count) {
    if (count < 2) return {0, 0, 0, 0};
    const SAPoint& first = centers[*pins];
#if defined(__SSE2__)
    // Each lane follows the original sequential scan. The OLD bound must be
    // operand two: MINPD/MAXPD return it on equal or unordered coordinates,
    // preserving signed zero and NaN payloads with the default FP environment.
    __m128d lo = _mm_set_pd(first.y, first.x), hi = lo;
    for (std::size_t i = 1; i < count; ++i) {
        const SAPoint& point = centers[pins[i]];
        const __m128d q = _mm_set_pd(point.y, point.x);
        lo = _mm_min_pd(q, lo);
        hi = _mm_max_pd(q, hi);
    }
    alignas(16) double low[2], high[2];
    _mm_store_pd(low, lo);
    _mm_store_pd(high, hi);
    return {low[0], high[0], low[1], high[1]};
#else
    SABox b{first.x, first.x, first.y, first.y};
    for (std::size_t i = 1; i < count; ++i) {
        const SAPoint& q = centers[pins[i]];
        if (q.x < b.xl) b.xl = q.x; else if (q.x > b.xh) b.xh = q.x;
        if (q.y < b.yl) b.yl = q.y; else if (q.y > b.yh) b.yh = q.y;
    }
    return b;
#endif
}
} // namespace sa_detail
