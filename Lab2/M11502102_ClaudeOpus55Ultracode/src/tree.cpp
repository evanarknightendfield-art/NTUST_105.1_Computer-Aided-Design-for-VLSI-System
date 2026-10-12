// tree.cpp — Tree 的實作：增量更新、transaction、snapshot
#include "tree.h"

#include <algorithm>
#include <climits>
#include <cmath>

void Tree::init(const Problem& p, const TypeTable& tt) {
    P = &p;
    TT = &tt;
    N = p.numSinks();
    nd.assign(N + 1, Node());
    nd[0].x = p.srcX;
    nd[0].y = p.srcY;
    nd[0].kind = SRC_NODE;
    nd[0].alive = true;
    occ_.reset(p.dimx, p.dimy);
    occ_.insert(p.srcX, p.srcY);
    for (int i = 1; i <= N; ++i) {
        nd[i].x = p.sinkX[i - 1];
        nd[i].y = p.sinkY[i - 1];
        nd[i].kind = SINK_NODE;
        nd[i].alive = true;
        nd[i].cnt = 1;
        occ_.insert(nd[i].x, nd[i].y);
    }
    mark_.assign(nd.size(), 0);
    freeList_.clear();
    totalCost = 0;
    numBuf = 0;
}

// ---------------- journal ----------------

void Tree::touch(int v) {
    if (!inTx_) return;
    if (mark_[v] != stamp_) {
        mark_[v] = stamp_;
        jNode_.emplace_back(v, nd[v]);
    }
}

void Tree::occInsert(int x, int y) {
    occ_.insert(x, y);
    if (inTx_) jOcc_.push_back({x, y, 1});
}

void Tree::occErase(int x, int y) {
    occ_.erase(x, y);
    if (inTx_) jOcc_.push_back({x, y, 0});
}

void Tree::begin() {
    inTx_ = true;
    ++stamp_;
    jNode_.clear();
    jOcc_.clear();
    jFree_.clear();
    jCost_ = totalCost;
    jNumBuf_ = numBuf;
}

void Tree::commit() {
    inTx_ = false;
    jNode_.clear();
    jOcc_.clear();
    jFree_.clear();
}

void Tree::rollback() {
    for (auto it = jNode_.rbegin(); it != jNode_.rend(); ++it) nd[it->first] = std::move(it->second);
    for (auto it = jOcc_.rbegin(); it != jOcc_.rend(); ++it) {
        if (it->ins) occ_.erase(it->x, it->y);
        else occ_.insert(it->x, it->y);
    }
    for (auto it = jFree_.rbegin(); it != jFree_.rend(); ++it) {
        if (it->second == 1) freeList_.pop_back();
        else freeList_.push_back(it->first);   // 0：放回去；2：新開的節點已還原成 dead，也放進 free list
    }
    totalCost = jCost_;
    numBuf = jNumBuf_;
    inTx_ = false;
    jNode_.clear();
    jOcc_.clear();
    jFree_.clear();
}

bool Tree::touchedLegal() const {
    for (const auto& e : jNode_)
        if (!nodeLegal(e.first)) return false;
    return true;
}

// ---------------- 基本編輯 ----------------

int Tree::addBuffer(int x, int y) {
    int id;
    if (!freeList_.empty()) {
        id = freeList_.back();
        freeList_.pop_back();
        if (inTx_) jFree_.push_back({id, 0});
    } else {
        id = (int)nd.size();
        nd.emplace_back();
        mark_.push_back(0);
        if (inTx_) jFree_.push_back({id, 2});
    }
    touch(id);
    Node& n = nd[id];
    n = Node();
    n.x = x;
    n.y = y;
    n.kind = BUF_NODE;
    n.alive = true;
    occInsert(x, y);
    ++numBuf;
    return id;
}

void Tree::removeBuffer(int b) {
    touch(b);
    Node& n = nd[b];
    totalCost -= TT->cost(n.type);
    occErase(n.x, n.y);
    n.alive = false;
    n.type = -1;
    n.parent = -1;
    n.ch.clear();
    freeList_.push_back(b);
    if (inTx_) jFree_.push_back({b, 1});
    --numBuf;
}

void Tree::link(int c, int p) {
    touch(c);
    touch(p);
    nd[p].ch.push_back(c);
    nd[c].parent = p;
}

void Tree::unlink(int c) {
    int p = nd[c].parent;
    if (p < 0) return;
    touch(c);
    touch(p);
    std::vector<int>& ch = nd[p].ch;
    for (size_t i = 0; i < ch.size(); ++i) {
        if (ch[i] == c) {
            ch[i] = ch.back();
            ch.pop_back();
            break;
        }
    }
    nd[c].parent = -1;
}

void Tree::moveTo(int b, int x, int y) {
    touch(b);
    occErase(nd[b].x, nd[b].y);
    nd[b].x = x;
    nd[b].y = y;
    occInsert(x, y);
}

// ---------------- 增量統計 ----------------

bool Tree::recompute(int v) {
    touch(v);
    Node& n = nd[v];
    long long lo = 0, hi = 0, cnt = 0, sum = 0;
    double s1 = 0, s2 = 0;
    if (n.kind == SINK_NODE) {
        cnt = 1;
    } else if (!n.ch.empty()) {
        lo = LLONG_MAX;
        hi = LLONG_MIN;
        for (int c : n.ch) {
            const Node& m = nd[c];
            long long e = manhattan(n.x, n.y, m.x, m.y);
            sum += e;
            lo = std::min(lo, e + m.lo);
            hi = std::max(hi, e + m.hi);
            cnt += m.cnt;
            double de = (double)e;
            s1 += (double)m.cnt * de + m.s1;
            s2 += (double)m.cnt * de * de + 2.0 * de * m.s1 + m.s2;
        }
    }
    if (n.kind == BUF_NODE) {
        // 型號永遠選「能撐住目前 fanout 與 length 的最便宜型號」
        int t = n.ch.empty() ? -1 : TT->pick((int)n.ch.size(), sum);
        if (t != n.type) {
            totalCost += TT->cost(t) - TT->cost(n.type);
            n.type = t;
        }
    }
    bool changed = lo != n.lo || hi != n.hi || cnt != n.cnt || s1 != n.s1 || s2 != n.s2;
    n.sumLen = sum;
    n.lo = lo;
    n.hi = hi;
    n.cnt = cnt;
    n.s1 = s1;
    n.s2 = s2;
    return changed;
}

void Tree::refreshUp(int v) {
    while (v >= 0) {
        if (!recompute(v)) break;   // 統計沒變 → 祖先也不用再算
        v = nd[v].parent;
    }
}

void Tree::recomputeAll() {
    // BFS 順序的反向 = 子節點一定先於父節點處理
    std::vector<int> order;
    order.reserve(nd.size());
    order.push_back(0);
    for (size_t i = 0; i < order.size(); ++i)
        for (int c : nd[order[i]].ch) order.push_back(c);
    for (auto it = order.rbegin(); it != order.rend(); ++it) recompute(*it);
    totalCost = 0;
    numBuf = 0;
    for (size_t v = N + 1; v < nd.size(); ++v) {
        if (!nd[v].alive) continue;
        ++numBuf;
        totalCost += TT->cost(nd[v].type);
    }
}

bool Tree::nodeLegal(int v) const {
    const Node& n = nd[v];
    if (!n.alive) return true;
    if (n.kind == SRC_NODE) return (int)n.ch.size() <= P->srcF && n.sumLen <= P->srcL;
    if (n.parent < 0) return false;
    if (n.kind == BUF_NODE) return !n.ch.empty() && n.type >= 0;
    return true;
}

bool Tree::isAncestorOrSelf(int a, int v) const {
    while (v >= 0) {
        if (v == a) return true;
        v = nd[v].parent;
    }
    return false;
}

bool Tree::attachedToRoot(int v) const {
    while (nd[v].parent >= 0) v = nd[v].parent;
    return v == 0;
}

double Tree::sigma() const {
    const Node& r = nd[0];
    if (r.cnt <= 0) return 0;
    double m = r.s1 / (double)r.cnt;
    double var = r.s2 / (double)r.cnt - m * m;
    return var > 0 ? std::sqrt(var) : 0;
}

// ---------------- snapshot ----------------

void Tree::save(Snapshot& s) const {
    size_t n = nd.size();
    s.x.resize(n);
    s.y.resize(n);
    s.parent.resize(n);
    s.kind.resize(n);
    s.alive.resize(n);
    for (size_t i = 0; i < n; ++i) {
        s.x[i] = nd[i].x;
        s.y[i] = nd[i].y;
        s.parent[i] = nd[i].parent;
        s.kind[i] = nd[i].kind;
        s.alive[i] = nd[i].alive;
    }
}

void Tree::load(const Snapshot& s) {
    size_t n = s.x.size();
    nd.assign(n, Node());
    occ_.reset(P->dimx, P->dimy);
    freeList_.clear();
    for (size_t i = 0; i < n; ++i) {
        Node& v = nd[i];
        v.x = s.x[i];
        v.y = s.y[i];
        v.parent = s.parent[i];
        v.kind = s.kind[i];
        v.alive = s.alive[i];
        if (v.alive) occ_.insert(v.x, v.y);
        else freeList_.push_back((int)i);
    }
    for (size_t i = 1; i < n; ++i)
        if (nd[i].alive && nd[i].parent >= 0) nd[nd[i].parent].ch.push_back((int)i);
    mark_.assign(n, 0);
    inTx_ = false;
    recomputeAll();
}
