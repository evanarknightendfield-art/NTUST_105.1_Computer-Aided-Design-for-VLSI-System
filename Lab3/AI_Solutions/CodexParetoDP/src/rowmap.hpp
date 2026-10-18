// rowmap.hpp — per-segment sorted cell lists, so legal moves can ask
// "which cell (or which gap) is at this site?" and "how much room is around this cell?".
#pragma once
#include <algorithm>
#include <vector>

#include "placedb.hpp"

struct Gap { int lo = 0, hi = 0; int width() const { return hi - lo; } };   // free sites [lo, hi)

class RowMap {
public:
    RowMap(const PlaceDB& db, Placement& p) : db_(db), p_(p) { build(); }

    void build() {
        cells_.assign(db_.segs.size(), {});
        for (int v : db_.movable) cells_[p_.seg[v]].push_back(v);
        for (auto& c : cells_)
            std::sort(c.begin(), c.end(), [&](int a, int b) { return p_.site[a] < p_.site[b]; });
    }

    // Rebuilds the lists of the given segments only (cells may have moved between them).
    void rebuild(std::vector<int> segs) {
        std::sort(segs.begin(), segs.end());
        segs.erase(std::unique(segs.begin(), segs.end()), segs.end());
        std::vector<char> mark(db_.segs.size(), 0);
        for (int s : segs) {
            mark[s] = 1;
            cells_[s].clear();
        }
        for (int v : db_.movable)
            if (mark[p_.seg[v]]) cells_[p_.seg[v]].push_back(v);
        for (int s : segs)
            std::sort(cells_[s].begin(), cells_[s].end(), [&](int a, int b) { return p_.site[a] < p_.site[b]; });
    }

    const std::vector<int>& cells(int s) const { return cells_[s]; }
    int width(int v, int s) const { return db_.sitesOf(v, db_.segs[s]); }

    int indexOf(int v) const {
        const auto& c = cells_[p_.seg[v]];
        int site = p_.site[v];
        return (int)(std::lower_bound(c.begin(), c.end(), site,
                                      [&](int a, int t) { return p_.site[a] < t; }) - c.begin());
    }

    // Room for v when it is lifted out: from its left neighbour's end to its right
    // neighbour's start. Cell `ignore` (if any) is treated as already removed.
    Gap freeAround(int v, int ignore = -1) const {
        int s = p_.seg[v];
        const auto& c = cells_[s];
        int i = indexOf(v);
        int l = i - 1, r = i + 1;
        if (l >= 0 && c[l] == ignore) --l;
        if (r < (int)c.size() && c[r] == ignore) ++r;
        Gap g;
        g.lo = l >= 0 ? p_.site[c[l]] + width(c[l], s) : 0;
        g.hi = r < (int)c.size() ? p_.site[c[r]] : db_.segs[s].numSites;
        return g;
    }

    // Cell covering site t of segment s, or -1 with `gap` set to the free run around t.
    // Cell `ignore` is treated as removed.
    int cellAt(int s, int t, Gap& gap, int ignore = -1) const {
        const auto& c = cells_[s];
        int j = (int)(std::upper_bound(c.begin(), c.end(), t,
                                       [&](int t2, int a) { return t2 < p_.site[a]; }) - c.begin()) - 1;
        if (j >= 0 && c[j] != ignore && p_.site[c[j]] + width(c[j], s) > t) return c[j];
        int l = j, r = j + 1;
        if (l >= 0 && c[l] == ignore) --l;
        if (r < (int)c.size() && c[r] == ignore) ++r;
        gap.lo = l >= 0 ? p_.site[c[l]] + width(c[l], s) : 0;
        gap.hi = r < (int)c.size() ? p_.site[c[r]] : db_.segs[s].numSites;
        if (gap.lo > t) gap.hi = gap.lo;   // t is inside the ignored cell's left neighbour: no gap
        return -1;
    }

    void remove(int v) {
        auto& c = cells_[p_.seg[v]];
        c.erase(c.begin() + indexOf(v));
    }

    // Sets v's position and inserts it into the segment list (the spot must be free).
    void insert(int v, int s, int site) {
        p_.seg[v] = s;
        p_.site[v] = site;
        auto& c = cells_[s];
        c.insert(c.begin() + indexOf(v), v);
    }

    // Moves v inside its own segment without changing the order of cells.
    void slide(int v, int site) { p_.site[v] = site; }

    // Re-sorts entries [from, from + count) of segment s by site (after slides that changed their order).
    void resort(int s, int from, int count) {
        auto& c = cells_[s];
        std::sort(c.begin() + from, c.begin() + from + count, [&](int a, int b) { return p_.site[a] < p_.site[b]; });
    }

    // a and b trade places in the lists (positions are set by the caller and must keep order).
    void exchange(int a, int b, int aSeg, int aSite, int bSeg, int bSite) {
        int ia = indexOf(a), sa = p_.seg[a];
        int ib = indexOf(b), sb = p_.seg[b];
        cells_[sa][ia] = b;
        cells_[sb][ib] = a;
        p_.seg[a] = aSeg; p_.site[a] = aSite;
        p_.seg[b] = bSeg; p_.site[b] = bSite;
        if (sa == sb) {   // adjacent swap inside one segment can flip the order
            auto& c = cells_[sa];
            int lo = std::min(ia, ib), hi = std::max(ia, ib);
            if (p_.site[c[lo]] > p_.site[c[hi]]) std::swap(c[lo], c[hi]);
        }
    }

private:
    const PlaceDB& db_;
    Placement& p_;
    std::vector<std::vector<int>> cells_;
};
