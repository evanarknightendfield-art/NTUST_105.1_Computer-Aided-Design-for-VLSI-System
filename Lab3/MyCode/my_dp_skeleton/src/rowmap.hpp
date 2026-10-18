// rowmap.hpp — 每個 segment 一個「依 x 排序的 cell 清單」
//
// 用途（Milestone 5、6）：合法的 move 一定要問兩個問題
//   1. 「某個 site 上面是哪個 cell？還是空的？空的話空位有多大？」 → cellAt
//   2. 「把 cell v 拿起來，左右鄰居之間有多少空間？」             → freeAround
// 只要每次移動都在這兩個答案的範圍內，placement 就一定一直合法。
//
// 要補的地方：TODO(M6) freeAround、cellAt。其他寫好了。
#pragma once
#include <algorithm>
#include <vector>

#include "placedb.hpp"

struct Gap { int lo = 0, hi = 0; int width() const { return hi - lo; } };   // 空的 sites [lo, hi)

class RowMap {
public:
    RowMap(const PlaceDB& db, Placement& p) : db_(db), p_(p) { build(); }

    void build() {
        cells_.assign(db_.segs.size(), {});
        for (int v : db_.movable) cells_[p_.seg[v]].push_back(v);
        for (auto& c : cells_)
            std::sort(c.begin(), c.end(), [&](int a, int b) { return p_.site[a] < p_.site[b]; });
    }

    const std::vector<int>& cells(int s) const { return cells_[s]; }
    int width(int v, int s) const { return db_.sitesOf(v, db_.segs[s]); }

    // v 在自己 segment 清單裡的位置（二分搜尋）
    int indexOf(int v) const {
        const auto& c = cells_[p_.seg[v]];
        int site = p_.site[v];
        return (int)(std::lower_bound(c.begin(), c.end(), site,
                                      [&](int a, int t) { return p_.site[a] < t; }) - c.begin());
    }

    // TODO(M6) 把 v 拿起來之後的空間：左鄰居的結尾 ~ 右鄰居的開頭
    //   沒有左鄰居 → 0；沒有右鄰居 → numSites
    //   ignore：當作這個 cell 也已經被拿走（swap 兩個相鄰 cell 時用得到）
    Gap freeAround(int v, int ignore = -1) const {
        (void)v; (void)ignore;
        return {};
    }

    // TODO(M6) segment s 的第 t 個 site 被哪個 cell 蓋住？回傳 cell 編號；
    //   沒有人蓋住就回傳 -1，並把 gap 設成 t 所在的那段空位
    //   提示：std::upper_bound 找「開頭 <= t 的最後一個 cell」，再看它的結尾有沒有超過 t
    int cellAt(int s, int t, Gap& gap, int ignore = -1) const {
        (void)s; (void)t; (void)gap; (void)ignore;
        return -1;
    }

    void remove(int v) {
        auto& c = cells_[p_.seg[v]];
        c.erase(c.begin() + indexOf(v));
    }

    // 設定 v 的新位置並插回清單（呼叫前要確定那裡是空的）
    void insert(int v, int s, int site) {
        p_.seg[v] = s;
        p_.site[v] = site;
        auto& c = cells_[s];
        c.insert(c.begin() + indexOf(v), v);
    }

    // 在自己的空位裡左右滑動（順序不變，所以清單不用動）
    void slide(int v, int site) { p_.site[v] = site; }

    // a、b 交換位置（新位置由呼叫的人算好，而且要維持清單的順序）
    void exchange(int a, int b, int aSeg, int aSite, int bSeg, int bSite) {
        int ia = indexOf(a), sa = p_.seg[a];
        int ib = indexOf(b), sb = p_.seg[b];
        cells_[sa][ia] = b;
        cells_[sb][ib] = a;
        p_.seg[a] = aSeg; p_.site[a] = aSite;
        p_.seg[b] = bSeg; p_.site[b] = bSite;
        if (sa == sb) {   // 同一個 segment 的相鄰交換，順序會對調
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
