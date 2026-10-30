#pragma once

#include <cmath>
#include <utility>
#include <vector>

namespace sa_detail {

// Immutable, contiguous row bottoms. The interpolation is only a search hint;
// every result obeys PlaceDB::nearestRow's capped lower_bound and tie rule.
class SANearestRows {
public:
    explicit SANearestRows(std::vector<double> y)
        : y_(std::move(y)), last_(static_cast<int>(y_.size()) - 1),
          origin_(y_.empty() ? 0.0 : y_.front()),
          scale_(last_ > 0 ? last_ / (y_.back() - origin_) : 0.0) {}

    int nearestRow(double y) const {
        if (last_ <= 0) return 0;
        int lo = 0;
        const double estimate = (y - origin_) * scale_;
        bool found = false;
        // Check before converting: NaN, infinities and huge finite inputs must
        // retain binary-search behavior without an out-of-range integer cast.
        if (std::isfinite(estimate) && estimate >= 0.0 && estimate <= last_) {
            int at = static_cast<int>(estimate);
            for (int correction = 0; correction < 2; ++correction) {
                if (y_[at] < y) {
                    if (at == last_ || !(y_[at + 1] < y)) {
                        lo = at == last_ ? at : at + 1;
                        found = true;
                        break;
                    }
                    ++at;
                } else {
                    if (at == 0 || y_[at - 1] < y) {
                        lo = at;
                        found = true;
                        break;
                    }
                    --at;
                }
            }
        }
        if (!found) {
            int hi = last_;
            while (lo < hi) {
                const int mid = (lo + hi) / 2;
                if (y_[mid] < y) lo = mid + 1;
                else hi = mid;
            }
        }
        if (lo > 0 && std::fabs(y_[lo - 1] - y) <= std::fabs(y_[lo] - y)) --lo;
        return lo;
    }

private:
    std::vector<double> y_;
    int last_;
    double origin_, scale_;
};

}  // namespace sa_detail
