// Serial polish cost workspace. Only accepted moves update cached pin centers;
// candidate moves use fixed external boxes and never mutate the placement.
#pragma once
#include "placedb.hpp"
#include <algorithm>
#include <array>
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
        // v moved, so the external boxes of every other cell on its nets changed
        if(cached_) for(int n:db_.nodeNets[v]) if(db_.nets[n].size()>1) for(int u:db_.nets[n]) if(u!=v) dirty_[u]=1;
    }
    std::vector<Box> externalBoxes(int v) const {
        std::vector<Box> result;
        externalBoxes(v,result);
        return result;
    }
    // Reuse caller-owned capacity across serial proposals. The returned box
    // order and endpoint arithmetic are identical to the allocating overload.
    void externalBoxes(int v,std::vector<Box>& result) const {
        if(cached_) {
            fill(v);
            result.assign(boxes_.begin()+off_[v],boxes_.begin()+off_[v+1]);
            return;
        }
        result.clear();
        result.reserve(db_.nodeNets[v].size());
        for(int n:db_.nodeNets[v]) if(db_.nets[n].size()>1) {
            Box b=emptyBox();
            for(int u:db_.nets[n]) if(u!=v) include(b,points_[u]);
            result.push_back(b);
        }
    }
    // keep every cell's external boxes (and their median interval) until a pin on one of its
    // nets moves (refresh). Same boxes in the same order as computing them on demand: min / max of
    // the same points, so every cost and every decision is unchanged (polish_box_cache_test).
    void enableCache() {
        if(cached_) return;
        const size_t N=db_.nodes.size();
        off_.assign(N+1,0);
        for(size_t v=0;v<N;++v) {
            int k=0;
            for(int n:db_.nodeNets[v]) k+=db_.nets[n].size()>1;
            off_[v+1]=off_[v]+k;
        }
        boxes_.resize(off_[N]);
        med_.resize(N);
        dirty_.assign(N,1);
        cached_=true;
    }
    bool cached() const {return cached_;}
    // medianTarget(externalBoxes(v), horizontal, current) of optimizer.cpp; needs the cache
    double median(int v,bool horizontal,double current) const {
        fill(v);
        if(off_[v]==off_[v+1]) return current;
        const auto& m=med_[v];
        return std::clamp(current,horizontal?m[0]:m[2],horizontal?m[1]:m[3]);
    }
private:
    void fill(int v) const {
        if(!dirty_[v]) return;
        Box* out=boxes_.data()+off_[v];
        for(int n:db_.nodeNets[v]) if(db_.nets[n].size()>1) {
            Box b=emptyBox();
            for(int u:db_.nets[n]) if(u!=v) include(b,points_[u]);
            *out++=b;
        }
        const size_t k=off_[v+1]-off_[v];
        if(k) {
            ends_.resize(2*k);
            for(int axis=0;axis<2;++axis) {
                for(size_t i=0;i<k;++i) {
                    const Box& b=boxes_[off_[v]+i];
                    ends_[2*i]=axis?b.ly:b.lx;ends_[2*i+1]=axis?b.hy:b.hx;
                }
                std::sort(ends_.begin(),ends_.end());
                med_[v][2*axis]=ends_[k-1];med_[v][2*axis+1]=ends_[k];
            }
        }
        dirty_[v]=0;
    }
    const PlaceDB& db_;
    std::vector<Point> points_;
    bool cached_=false;
    std::vector<size_t> off_;
    mutable std::vector<Box> boxes_;
    mutable std::vector<std::array<double,4>> med_;
    mutable std::vector<char> dirty_;
    mutable std::vector<double> ends_;
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
    // a != b; nodeNets are sorted/unique by PlaceDB::read. Both box lists
    // must come from externalBoxes on this same, unchanged pin snapshot.
    // Nonshared nets already have their exact outside box. Only a shared
    // net needs another scan to exclude both cells. Merge in original net
    // order so cost keeps exactly the same floating-point reduction.
    void preparePair(int a,int b,const std::vector<Box>& aBoxes,
                     const std::vector<Box>& bBoxes) {
        count_=2;
        terms_.clear();
        const auto& db=pins_.db();
        const auto& an=db.nodeNets[a];const auto& bn=db.nodeNets[b];
        terms_.reserve(an.size()+bn.size());
        size_t ai=0,bi=0,ab=0,bb=0;
        while(ai<an.size() || bi<bn.size()) {
            int n=bi==bn.size() ? an[ai] : ai==an.size() ? bn[bi] : std::min(an[ai],bn[bi]);
            bool hasA=ai<an.size() && an[ai]==n;
            bool hasB=bi<bn.size() && bn[bi]==n;
            ai+=hasA;bi+=hasB;
            if(db.nets[n].size()<=1) continue;
            Term term;
            if(hasA && hasB) {
                term={emptyBox(),3};
                for(int v:db.nets[n]) if(v!=a && v!=b) include(term.outside,pins_.point(v));
            } else if(hasA) term={aBoxes[ab],1};
            else term={bBoxes[bb],2};
            ab+=hasA;bb+=hasB;
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
