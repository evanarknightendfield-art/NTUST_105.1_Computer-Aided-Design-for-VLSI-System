#include "exact_placement.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <vector>

namespace {
using I = long long;
using R = long double;
struct ChildLine {
    I u, cross, lo, hi, count, oldArrival;
    R sum;
};
I floorDiv(I a, I b) {
    if (b < 0) { a = -a; b = -b; }
    I q = a / b, r = a % b;
    return q - (r < 0);
}
}

bool exactCoordinateSpot(const Tree& T, int b, bool xAxis, int& bx, int& by) {
    if (b <= T.N || (size_t)b >= T.nd.size() || !T.nd[b].alive || T.nd[b].ch.empty()) return false;
    const Node& n = T.nd[b];
    int p = n.parent;
    if (p < 0) return false;
    const Node& par = T.nd[p];
    const I oldU = xAxis ? n.x : n.y;
    const I fixed = xAxis ? n.y : n.x;
    const I pu = xAxis ? par.x : par.y;
    const I pcross = std::llabs(fixed - (xAxis ? par.y : par.x));
    const I upper = (xAxis ? T.P->dimx : T.P->dimy) - 1;
    if (upper < 1) return false;
    const I oldParentEdge = T.dist(p, b);
    const I restParent = par.sumLen - oldParentEdge;
    I arrP = 0;
    for (int v = p; T.nd[v].parent >= 0; v = T.nd[v].parent) arrP += T.dist(v, T.nd[v].parent);

    I outsideLo = LLONG_MAX, outsideHi = LLONG_MIN;
    I au = arrP;
    int excluded = b;
    for (int u = p; u >= 0;) {
        for (int c : T.nd[u].ch) if (c != excluded && T.nd[c].cnt > 0) {
            I a = au + T.dist(u, c);
            outsideLo = std::min(outsideLo, a + T.nd[c].lo);
            outsideHi = std::max(outsideHi, a + T.nd[c].hi);
        }
        excluded = u;
        int q = T.nd[u].parent;
        if (q < 0) break;
        au -= T.dist(q, u);
        u = q;
    }

    std::vector<ChildLine> children;
    std::vector<I> knots{1, upper};
    auto addKnot = [&](I u) { if (u >= 1 && u <= upper) knots.push_back(u); };
    addKnot(pu);
    for (int c : n.ch) {
        const Node& k = T.nd[c];
        I cu = xAxis ? k.x : k.y;
        I cross = std::llabs(fixed - (xAxis ? k.y : k.x));
        children.push_back({cu, cross, k.lo, k.hi, k.cnt, arrP + oldParentEdge + T.dist(b, c), (R)k.s1});
        addKnot(cu);
    }
    std::sort(knots.begin(), knots.end());
    knots.erase(std::unique(knots.begin(), knots.end()), knots.end());
    std::vector<I> events = knots;
    events.push_back(oldU);
    const R mean = (R)T.nd[0].s1 / std::max<I>(1, T.nd[0].cnt);
    const R count = (R)std::max<I>(1, T.nd[0].cnt);
    auto addCross = [&](I numerator, I denominator, I l, I h) {
        if (!denominator) return;
        I q = floorDiv(numerator, denominator);
        if (q >= l && q <= h) events.push_back(q);
        if (q < LLONG_MAX && q + 1 >= l && q + 1 <= h) events.push_back(q + 1);
    };

    // Within a cell, every affected arrival has only two possible slopes:
    // 0 and +/-2. Both extremum envelopes therefore have at most one knot.
    for (size_t cell = 0; cell + 1 < knots.size(); ++cell) {
        I l = knots[cell], h = knots[cell + 1];
        I sp = pu <= l ? 1 : -1;
        I bp = pcross - sp * pu;
        I lenSlope = 0, lenBase = 0;
        I high0 = outsideHi, low0 = outsideLo, highS = LLONG_MIN, lowS = LLONG_MAX;
        R deltaSlope = 0, deltaBase = 0, square2 = 0, square1 = 0;
        for (const auto& c : children) {
            I sc = c.u <= l ? 1 : -1;
            I bc = c.cross - sc * c.u;
            lenSlope += sc;
            lenBase += bc;
            if (c.count <= 0) continue;
            I s = sp + sc;
            I offset = arrP + bp + bc;
            if (!s) {
                high0 = std::max(high0, offset + c.hi);
                low0 = std::min(low0, offset + c.lo);
            } else {
                highS = std::max(highS, offset + c.hi);
                lowS = std::min(lowS, offset + c.lo);
            }
            R d = (R)offset - c.oldArrival;
            R oldSum = c.sum + (R)c.count * c.oldArrival;
            deltaSlope += (R)c.count * s;
            deltaBase += (R)c.count * d;
            square2 += (R)c.count * s * s;
            square1 += 2 * (R)s * (oldSum + (R)c.count * d);
        }
        I slope = 2 * sp;
        if (high0 != LLONG_MIN && highS != LLONG_MIN) addCross(high0 - highS, slope, l, h);
        if (low0 != LLONG_MAX && lowS != LLONG_MAX) addCross(low0 - lowS, slope, l, h);
        for (const BufType& t : T.P->lib) {
            if (t.F >= (int)n.ch.size()) addCross(t.L - lenBase, lenSlope, l, h);
            if (p != 0 && t.F >= (int)par.ch.size()) addCross(t.L - restParent - bp, sp, l, h);
        }
        if (p == 0) addCross(T.P->srcL - restParent - bp, sp, l, h);
        // On a flat Score plateau, variance is a convex quadratic. Include
        // its vertex so the secondary objective is also optimized exactly
        // up to the double precision of the tree's stored moments.
        R qa = square2 - deltaSlope * deltaSlope / count;
        R qb = square1 - 2 * mean * deltaSlope - 2 * deltaSlope * deltaBase / count;
        if (qa > 1e-12L) {
            R vertex = -qb / (2 * qa);
            if (vertex >= (R)l && vertex <= (R)h) {
                I q = (I)std::floor(vertex);
                events.push_back(q);
                if (q < h) events.push_back(q + 1);
            }
        }
    }
    std::sort(events.begin(), events.end());
    events.erase(std::unique(events.begin(), events.end()), events.end());

    I bestScore = T.score(), bestU = oldU;
    I bestWire = n.sumLen + oldParentEdge;
    R bestVarianceDelta = 0;
    const I oldLocalCost = T.TT->cost(n.type) + (p == 0 ? 0 : T.TT->cost(par.type));
    auto available = [&](I u) {
        return u == oldU || (xAxis ? T.isFree(u, fixed) : T.isFree(fixed, u));
    };
    auto eval = [&](I u) {
        I edgeP = std::llabs(u - pu) + pcross;
        I loadP = restParent + edgeP;
        int typeP = -1;
        if (p == 0) { if (loadP > T.P->srcL) return; }
        else { typeP = T.TT->pick((int)par.ch.size(), loadP); if (typeP < 0) return; }
        I sumLen = 0, lo = outsideLo, hi = outsideHi;
        R delta1 = 0, delta2 = 0;
        for (const auto& c : children) {
            I d = std::llabs(u - c.u) + c.cross;
            sumLen += d;
            if (c.count <= 0) continue;
            I a = arrP + edgeP + d;
            lo = std::min(lo, a + c.lo);
            hi = std::max(hi, a + c.hi);
            R delta = (R)a - c.oldArrival;
            delta1 += (R)c.count * delta;
            delta2 += 2 * delta * (c.sum + (R)c.count * c.oldArrival) + (R)c.count * delta * delta;
        }
        int typeB = T.TT->pick((int)n.ch.size(), sumLen);
        if (typeB < 0) return;
        I cost = T.totalCost - oldLocalCost + T.TT->cost(typeB) + (p == 0 ? 0 : T.TT->cost(typeP));
        I score = cost + T.P->wSkew * (hi - lo);
        R varianceDelta = delta2 - 2 * mean * delta1 - delta1 * delta1 / count;
        I wire = sumLen + edgeP;
        if (score < bestScore || (score == bestScore && (varianceDelta < bestVarianceDelta - 1e-8L ||
            (std::fabs(varianceDelta - bestVarianceDelta) <= 1e-8L && wire < bestWire)))) {
            bestScore = score;
            bestVarianceDelta = varianceDelta;
            bestWire = wire;
            bestU = u;
        }
    };
    // Between consecutive events Score is affine and variance is monotone
    // (its vertex is an event). Only the first/last free integer can win.
    // Occupied runs are scanned in disjoint intervals, hence their total
    // cost is bounded by the number of occupied coordinates on this line.
    if (events.size() == 1) eval(events[0]);
    for (size_t i = 0; i + 1 < events.size(); ++i) {
        I l = events[i], h = events[i + 1];
        while (l <= h && !available(l)) ++l;
        if (l > h) continue;
        eval(l);
        while (h > l && !available(h)) --h;
        if (h != l) eval(h);
    }
    if (bestU == oldU) return false;
    bx = xAxis ? (int)bestU : n.x;
    by = xAxis ? n.y : (int)bestU;
    return true;
}
