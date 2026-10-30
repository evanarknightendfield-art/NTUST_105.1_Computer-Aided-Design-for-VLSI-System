// Serial polish cost workspace. Only accepted moves update cached pin centers;
// candidate moves use fixed external boxes and never mutate the placement.
#pragma once
#include "placedb.hpp"
#include <algorithm>
#include <limits>
#include <vector>

namespace polish_detail {
struct Point { double x, y; };
struct Box { double lx, hx, ly, hy; };

inline Box emptyBox() {
    const double inf=std::numeric_limits<double>::infinity();
    return {inf,-inf,inf,-inf};
}
inline void include(Box& b,const Point& p) {
    b.lx=std::min(b.lx,p.x);b.hx=std::max(b.hx,p.x);
    b.ly=std::min(b.ly,p.y);b.hy=std::max(b.hy,p.y);
}

class PinCache {
public:
    PinCache(const PlaceDB& db,const Placement& p):db_(db),points_(db.nodes.size()) {
        for(int v=0;v<int(db.nodes.size());++v) refresh(p,v);
    }
    const PlaceDB& db() const {return db_;}
    const Point& point(int v) const {return points_[v];}
    void refresh(const Placement& p,int v) {
        points_[v]={p.x(db_,v)+db_.nodes[v].w/2,p.y(db_,v)+db_.nodes[v].h/2};
    }
    std::vector<Box> externalBoxes(int v) const {
        std::vector<Box> result;
        result.reserve(db_.nodeNets[v].size());
        for(int n:db_.nodeNets[v]) if(db_.nets[n].size()>1) {
            Box b=emptyBox();
            for(int u:db_.nets[n]) if(u!=v) include(b,points_[u]);
            result.push_back(b);
        }
        return result;
    }
private:
    const PlaceDB& db_;
    std::vector<Point> points_;
};

// All terms retain the original sorted net order and the original HPWL
// expression. Only min/max reductions are shared; floating sums are not split
// into axis sums or changed to a sum of per-cell deltas (nets may be shared).
class MoveCosts {
public:
    explicit MoveCosts(const PinCache& pins):pins_(pins) {}
    void prepare(const std::vector<int>& nets,const int* group,int count) {
        count_=count;
        terms_.clear();
        terms_.reserve(nets.size());
        for(int n:nets) if(pins_.db().nets[n].size()>1) {
            Term term{emptyBox(),0};
            for(int v:pins_.db().nets[n]) {
                unsigned member=0;
                for(int i=0;i<count;++i) if(v==group[i]) member=1u<<i;
                if(member) term.mask|=member;
                else include(term.outside,pins_.point(v));
            }
            terms_.push_back(term);
        }
    }
    double cost(const Point* proposed) const {
        double total=0;
        for(const Term& term:terms_) {
            Box b=term.outside;
            for(int i=0;i<count_;++i) if(term.mask&(1u<<i)) include(b,proposed[i]);
            total+=b.hx-b.lx+b.hy-b.ly;
        }
        return total;
    }
private:
    struct Term {Box outside;unsigned mask;};
    const PinCache& pins_;
    std::vector<Term> terms_;
    int count_=0;
};
} // namespace polish_detail
