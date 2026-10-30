#pragma once
#include <algorithm>
#include <climits>
#include "tree.h"

// Intersect two Manhattan circles analytically, keeping the original scan order
// (smallest x, then largest y). Only occupied feasible points require scanning.
inline bool diamondIntersection(const Tree& T, int v, int p, long long a, long long b,
                                int& nx, int& ny) {
    if (a < 0 || b < 0) return false;
    const long long vx=T.nd[v].x, vy=T.nd[v].y, px=T.nd[p].x, py=T.nd[p].y;
    long long bestX=LLONG_MAX, bestY=LLONG_MIN;
    for (int half=0; half<2; ++half) for (int branch : {-1,1}) {
        long long x0=half ? vx : vx-a;
        long long y0=half ? vy+branch*a : vy;
        int slope=half ? -branch : branch;
        for (int sx : {-1,1}) for (int sy : {-1,1}) {
            long long lo=0, hi=a;
            // Enforce c + k*t >= 0, for k in {-1,+1}.
            auto constrain=[&](long long c,int k) {
                if (k==1) lo=std::max(lo,-c); else hi=std::min(hi,c);
            };
            constrain(x0-1,1); constrain(T.P->dimx-1LL-x0,-1);
            constrain(y0-1,slope); constrain(T.P->dimy-1LL-y0,-slope);
            constrain(sx*(x0-px),sx); constrain(sy*(y0-py),sy*slope);
            long long rhs=b-sx*(x0-px)-sy*(y0-py);
            int k=sx+sy*slope;
            if (k) {
                if (rhs%k) continue;
                lo=std::max(lo,rhs/k); hi=std::min(hi,rhs/k);
            } else if (rhs) continue;
            if (bestX!=LLONG_MAX) hi=std::min(hi,bestX-x0);
            for (long long t=lo; t<=hi; ++t) {
                long long x=x0+t,y=y0+slope*t;
                if (!T.usable(x,y)) continue;
                if (x<bestX || (x==bestX && y>bestY)) { bestX=x; bestY=y; }
                break;
            }
        }
    }
    if (bestX==LLONG_MAX) return false;
    nx=(int)bestX; ny=(int)bestY;
    return true;
}
