// sa_rowmap.hpp — SA-local row lists with direct cell indices.
#pragma once
#include "rowmap.hpp"

class SARowMap {
public:
    SARowMap(const PlaceDB& db, Placement& p) : db_(db), p_(p) { build(); }

    void build() {
        cells_.assign(db_.segs.size(), {});
        index_.assign(db_.nodes.size(), -1);
        if (!db_.uniformSites) {
            widths_.resize(db_.nodes.size());
            widthSpacing_.assign(db_.nodes.size(), 0);
        }
        for (int v : db_.movable) cells_[p_.seg[v]].push_back(v);
        for (int s = 0; s < (int)cells_.size(); ++s) {
            auto& c = cells_[s];
            std::sort(c.begin(), c.end(), [&](int a, int b) { return p_.site[a] < p_.site[b]; });
            refresh(s, 0, (int)c.size());
        }
    }

    void rebuild(std::vector<int> segs) {
        std::sort(segs.begin(), segs.end());
        segs.erase(std::unique(segs.begin(), segs.end()), segs.end());
        std::vector<char> mark(db_.segs.size(), 0);
        for (int s : segs) {
            mark[s] = 1;
            for (int v : cells_[s]) if (p_.seg[v] == s) index_[v] = -1;
            cells_[s].clear();
        }
        for (int v : db_.movable) if (mark[p_.seg[v]]) cells_[p_.seg[v]].push_back(v);
        for (int s : segs) {
            auto& c = cells_[s];
            std::sort(c.begin(), c.end(), [&](int a, int b) { return p_.site[a] < p_.site[b]; });
            refresh(s, 0, (int)c.size());
        }
    }

    const std::vector<int>& cells(int s) const { return cells_[s]; }
    void setCells(int s, const std::vector<int>& c) {
        // p_ may already contain another band's positions. Do not invalidate a
        // cell whose new segment has already been loaded by an earlier call.
        for (int v : cells_[s]) if (p_.seg[v] == s) index_[v] = -1;
        cells_[s] = c;
        refresh(s, 0, (int)c.size());
    }
    int width(int v, int s) const {
        if (db_.uniformSites) return db_.wSites[v];
        if (widthSpacing_[v] == db_.segs[s].sp) return widths_[v];
        return db_.sitesOf(v, db_.segs[s]);
    }
    int indexOf(int v) const {
        if (index_[v] >= 0) return index_[v];
        // Also retain the old insertion-position behavior for a removed cell.
        return insertionIndex(p_.seg[v], p_.site[v]);
    }

    Gap freeAround(int v, int ignore = -1) const {
        int s = p_.seg[v];
        const auto& c = cells_[s];
        int i = indexOf(v), l = i - 1, r = i + 1;
        if (l >= 0 && c[l] == ignore) --l;
        if (r < (int)c.size() && c[r] == ignore) ++r;
        return {l >= 0 ? p_.site[c[l]] + width(c[l], s) : 0,
                r < (int)c.size() ? p_.site[c[r]] : db_.segs[s].numSites};
    }

    int cellAt(int s, int t, Gap& gap, int ignore = -1, int* upperIndex = nullptr) const {
        const auto& c = cells_[s];
        int j = (int)(std::upper_bound(c.begin(), c.end(), t,
                    [&](int t2, int a) { return t2 < p_.site[a]; }) - c.begin()) - 1;
        if (upperIndex) *upperIndex = j + 1;
        if (j >= 0 && c[j] != ignore && p_.site[c[j]] + width(c[j], s) > t) return c[j];
        int l = j, r = j + 1;
        if (l >= 0 && c[l] == ignore) --l;
        if (r < (int)c.size() && c[r] == ignore) ++r;
        gap.lo = l >= 0 ? p_.site[c[l]] + width(c[l], s) : 0;
        gap.hi = r < (int)c.size() ? p_.site[c[r]] : db_.segs[s].numSites;
        if (gap.lo > t) gap.hi = gap.lo;
        return -1;
    }

    void remove(int v) {
        int s = p_.seg[v], i = indexOf(v);
        auto& c = cells_[s];
        c.erase(c.begin() + i);
        index_[v] = -1;
        refreshIndices(s, i, (int)c.size());
    }
    void insert(int v, int s, int site) {
        // v is absent; its former cached index is not an insertion position.
        int i = insertionIndex(s, site);
        p_.seg[v] = s; p_.site[v] = site;
        auto& c = cells_[s];
        c.insert(c.begin() + i, v);
        cacheWidth(v, s);
        refreshIndices(s, i, (int)c.size());
    }
    void slide(int v, int site) { p_.site[v] = site; }
    void resort(int s, int from, int count) {
        auto& c = cells_[s];
        std::sort(c.begin() + from, c.begin() + from + count,
                  [&](int a, int b) { return p_.site[a] < p_.site[b]; });
        refreshIndices(s, from, from + count);
    }
    void exchange(int a, int b, int aSeg, int aSite, int bSeg, int bSite) {
        int ia = indexOf(a), sa = p_.seg[a];
        int ib = indexOf(b), sb = p_.seg[b];
        cells_[sa][ia] = b; cells_[sb][ib] = a;
        p_.seg[a] = aSeg; p_.site[a] = aSite;
        p_.seg[b] = bSeg; p_.site[b] = bSite;
        if (sa == sb) {
            auto& c = cells_[sa];
            int lo = std::min(ia, ib), hi = std::max(ia, ib);
            if (p_.site[c[lo]] > p_.site[c[hi]]) std::swap(c[lo], c[hi]);
        }
        index_[cells_[sa][ia]] = ia;
        index_[cells_[sb][ib]] = ib;
        cacheWidth(a, aSeg); cacheWidth(b, bSeg);
    }

private:
    int insertionIndex(int s, int site) const {
        const auto& c = cells_[s];
        return (int)(std::lower_bound(c.begin(), c.end(), site,
                    [&](int a, int t) { return p_.site[a] < t; }) - c.begin());
    }
    void refreshIndices(int s, int from, int end) {
        const auto& c = cells_[s];
        for (int i = from; i < end; ++i) index_[c[i]] = i;
    }
    void cacheWidth(int v, int s) {
        if (!db_.uniformSites && widthSpacing_[v] != db_.segs[s].sp) {
            widths_[v] = db_.sitesOf(v, db_.segs[s]);
            widthSpacing_[v] = db_.segs[s].sp;
        }
    }
    void refresh(int s, int from, int end) {
        refreshIndices(s, from, end);
        for (int i = from; i < end; ++i) cacheWidth(cells_[s][i], s);
    }
    const PlaceDB& db_;
    Placement& p_;
    std::vector<std::vector<int>> cells_;
    std::vector<int> index_, widths_;
    std::vector<double> widthSpacing_;
};
