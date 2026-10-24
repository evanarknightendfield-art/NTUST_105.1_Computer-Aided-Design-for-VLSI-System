#include "global.hpp"
#include "window.hpp"
#include "rowmap.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <random>
#include <limits>
#include <functional>

namespace {
constexpr double EPS=1e-7;
struct Sites { std::vector<int> used; std::vector<double> history; };
// Exact convex L1 clumping for a fixed row/order, with genuine cap intervals
// (unlike the inherited row optimizer, the old position is NOT exempt).
bool packSequence(const PlaceDB& db, Placement& p, int si, const std::vector<int>& cells, double cap) {
    const auto& s=db.segs[si];int n=int(cells.size());
    std::vector<int> width(n),lo(n),hi(n);std::vector<double> ideal(n);
    int end=0;
    for(int j=0;j<n;++j) {
        const auto& v=db.nodes[cells[j]];width[j]=db.sitesOf(cells[j],s);
        double reach=(cap-std::abs(v.gy-s.y))/s.sp;
        ideal[j]=(v.gx-s.x0)/s.sp;
        lo[j]=std::max(0,int(std::ceil(ideal[j]-reach-1e-9)));
        hi[j]=std::min(s.numSites-width[j],int(std::floor(ideal[j]+reach+1e-9)));
        end=std::max(end,lo[j]);if(end>hi[j])return false;end+=width[j];
    }
    struct Cluster {int first,last,width,lo,hi,x;};
    std::vector<Cluster> stack;
    auto minimize=[&](Cluster& c) {
        std::vector<double> points;int prefix=0;
        for(int j=c.first;j<=c.last;++j){points.push_back(ideal[j]-prefix);prefix+=width[j];}
        auto mid=points.begin()+(points.size()-1)/2;
        std::nth_element(points.begin(),mid,points.end());
        // A rounded median need not minimize discrete L1: several targets
        // can lie between the same two sites. Evaluate both adjacent sites.
        int a=int(std::clamp(std::floor(*mid),double(c.lo),double(c.hi)));
        int b=int(std::clamp(std::ceil(*mid),double(c.lo),double(c.hi)));
        double ca=0,cb=0;
        for(double target:points){ca+=std::abs(a-target);cb+=std::abs(b-target);}
        c.x=cb<ca ? b:a;
    };
    for(int j=0;j<n;++j) {
        Cluster c{j,j,width[j],lo[j],hi[j],0};minimize(c);
        while(!stack.empty() && stack.back().x+stack.back().width>c.x) {
            auto b=stack.back();stack.pop_back();
            c={b.first,c.last,b.width+c.width,std::max(b.lo,c.lo-b.width),std::min(b.hi,c.hi-b.width),0};
            if(c.lo>c.hi)return false;
            minimize(c);
        }
        stack.push_back(c);
    }
    for(const auto& c:stack){int x=c.x;for(int j=c.first;j<=c.last;++j){p.seg[cells[j]]=si;p.site[cells[j]]=x;x+=width[j];}}
    return true;
}

bool orderedCap(const PlaceDB& db, Placement& p, double cap) {
    RowMap rows(db,p);bool all=true;
    for(size_t s=0;s<db.segs.size();++s)
        if(!packSequence(db,p,int(s),rows.cells(int(s)),cap))all=false;
    return all;
}

// Rebuild the complete row assignment, packing each tentative row with its
// actual cap intervals. Capacity is never treated as divisible cell area.
bool constructCap(const PlaceDB& db, Placement& out, double cap, double until) {
    if(db.movable.size()>3000) return false;
    std::mt19937 rng(761);
    for(int trial=0;trial<12 && wallSeconds()<until;++trial) {
        Placement p=out;std::vector<std::vector<int>> rows(db.segs.size());
        std::vector<double> costs(db.segs.size(),0),priority(db.nodes.size(),0);
        auto order=db.movable;
        for(int v:order) {
            const auto& n=db.nodes[v];double area=0;
            for(const auto& s:db.segs) {
                double dx=cap-std::abs(s.y-n.gy);int w=db.sitesOf(v,s);
                if(dx<0 || std::abs(s.height-n.h)>EPS || w>s.numSites)continue;
                double lo=std::max(0.0,std::ceil((n.gx-dx-s.x0-EPS)/s.sp));
                double hi=std::min(double(s.numSites-w),std::floor((n.gx+dx-s.x0+EPS)/s.sp));
                area+=std::max(0.0,hi-lo+1);
            }
            if(area==0)return false;
            double factor=trial<2?1.0:0.5+double(rng()%1000)/1000;
            priority[v]=(trial%3==0 ? area/std::max(n.w,1e-9) : trial%3==1 ? -n.w : area)*factor;
        }
        std::stable_sort(order.begin(),order.end(),[&](int a,int b){return priority[a]<priority[b];});
        bool ok=true;
        for(int v:order) {
            if(wallSeconds()>=until)return false;
            const auto& n=db.nodes[v];double best=1e300;int bs=-1;
            std::vector<int> bestRow,bestSites;
            for(size_t si=0;si<rows.size();++si) {
                if(wallSeconds()>=until)return false;
                const auto& s=db.segs[si];
                if(std::abs(s.y-n.gy)>cap || std::abs(s.height-n.h)>EPS)continue;
                auto list=rows[si];std::vector<int> old;
                for(int c:list)old.push_back(p.site[c]);
                int at=int(std::lower_bound(list.begin(),list.end(),n.gx,[&](int c,double x){return p.x(db,c)<x;})-list.begin());
                for(int k=std::max(0,at-2);k<=std::min(int(list.size()),at+2);++k) {
                    auto seq=list;seq.insert(seq.begin()+k,v);
                    if(packSequence(db,p,int(si),seq,cap)) {
                        double cost=0;for(int c:seq)cost+=displacementOf(db,p,c);
                        if(cost-costs[si]<best){best=cost-costs[si];bs=int(si);bestRow=seq;bestSites.clear();for(int c:seq)bestSites.push_back(p.site[c]);}
                    }
                    for(size_t j=0;j<list.size();++j){p.seg[list[j]]=int(si);p.site[list[j]]=old[j];}
                }
            }
            if(bs<0){ok=false;break;}
            for(size_t j=0;j<bestRow.size();++j){p.seg[bestRow[j]]=bs;p.site[bestRow[j]]=bestSites[j];}
            rows[bs]=std::move(bestRow);costs[bs]+=best;
        }
        if(ok && checkLegal(db,p).empty() && displacement(db,p).max<=cap+EPS){out=std::move(p);return true;}
    }
    return false;
}
// Complete position domains for small instances. MRV branch-and-bound is an
// independent global repair: no incumbent cell or row order is held fixed.
bool packingSearch(const PlaceDB& db, Placement& out, double cap, double until) {
    if(db.movable.size()>512) return false;
    struct Pos {int seg, site, begin, end; double cost;};
    std::vector<int> offset(db.segs.size()+1);
    for(size_t s=0;s<db.segs.size();++s) offset[s+1]=offset[s]+db.segs[s].numSites;
    if(offset.back()>2000000) return false;
    std::vector<std::vector<Pos>> domains(db.movable.size());
    size_t entries=0;
    for(size_t i=0;i<db.movable.size();++i) {
        int v=db.movable[i];const auto& n=db.nodes[v];
        for(size_t si=0;si<db.segs.size();++si) {
            const auto& s=db.segs[si];int w=db.sitesOf(v,s);
            double dx=cap-std::abs(s.y-n.gy);
            if(dx<-EPS || std::abs(n.h-s.height)>EPS || w>s.numSites) continue;
            int lo=std::max(0,int(std::ceil((n.gx-dx-s.x0-EPS)/s.sp)));
            int hi=std::min(s.numSites-w,int(std::floor((n.gx+dx-s.x0+EPS)/s.sp)));
            for(int x=lo;x<=hi;++x) {
                double d=std::abs(s.x0+x*s.sp-n.gx)+std::abs(s.y-n.gy);
                // Stay near incumbent to limit the total-displacement tradeoff.
                double change=std::abs(s.x0+x*s.sp-out.x(db,v))+std::abs(s.y-out.y(db,v));
                domains[i].push_back({int(si),x,offset[si]+x,offset[si]+x+w,change+0.01*d});
                if(++entries>800000) return false;
            }
        }
        if(domains[i].empty()) return false;
        std::stable_sort(domains[i].begin(),domains[i].end(),[](const Pos& a,const Pos& b){return a.cost<b.cost;});
    }
    std::vector<unsigned char> occupied(offset.back(),0),assigned(domains.size(),0);
    Placement p=out;long nodes=0;
    auto fits=[&](const Pos& c){for(int x=c.begin;x<c.end;++x) if(occupied[x])return false;return true;};
    std::function<bool(int)> dfs=[&](int left) {
        if(!left) return true;
        if((++nodes&127)==0 && wallSeconds()>=until) return false;
        int chosen=-1;size_t fewest=std::numeric_limits<size_t>::max();
        for(size_t i=0;i<domains.size();++i) if(!assigned[i]) {
            size_t count=0;
            for(const auto& c:domains[i]) if(fits(c) && ++count>=fewest) break;
            if(!count) return false;
            if(count<fewest) {fewest=count;chosen=int(i);}
        }
        assigned[chosen]=1;int v=db.movable[chosen];
        for(const auto& c:domains[chosen]) if(fits(c)) {
            if(wallSeconds()>=until) break;
            std::fill(occupied.begin()+c.begin,occupied.begin()+c.end,1);
            p.seg[v]=c.seg;p.site[v]=c.site;
            if(dfs(left-1)) return true;
            std::fill(occupied.begin()+c.begin,occupied.begin()+c.end,0);
        }
        assigned[chosen]=0;return false;
    };
    if(dfs(int(domains.size())) && checkLegal(db,p).empty()) {out=std::move(p);return true;}
    return false;
}
double individualBound(const PlaceDB& db) {
    double bound=0;
    for(int v:db.movable) {
        const auto& n=db.nodes[v]; double best=1e300;
        for(const auto& s:db.segs) {
            int w=db.sitesOf(v,s);
            if(w>s.numSites || n.h>s.height+EPS) continue;
            if(std::abs(s.y-n.gy)>=best) continue;
            int x=std::clamp(int(std::round((n.gx-s.x0)/s.sp)),0,s.numSites-w);
            best=std::min(best,std::abs(s.x0+x*s.sp-n.gx)+std::abs(s.y-n.gy));
        }
        bound=std::max(bound,best);
    }
    return bound;
}
}

bool repairCap(const PlaceDB& db, Placement& out, double cap, double until, unsigned seed) {
    if(wallSeconds()>=until) return false;
    Placement p=out;
    if(orderedCap(db,p,cap)) {out=std::move(p);return true;}
    if(constructCap(db,p,cap,wallSeconds()+0.2*(until-wallSeconds()))) {out=std::move(p);return true;}
    // Preserve the inexpensive row improvements even if other rows need
    // global reassignment. Full-domain DFS is capped to avoid starving repair.
    if(packingSearch(db,p,cap,wallSeconds()+0.1*(until-wallSeconds()))) {out=std::move(p);return true;}
    std::vector<Sites> sites(db.segs.size());
    size_t count=0;
    for(const auto& s:db.segs) count+=s.numSites;
    if(count>16000000) return false; // bounded memory, independent of case name
    for(size_t s=0;s<sites.size();++s) {
        sites[s].used.assign(db.segs[s].numSites,0);
        sites[s].history.assign(db.segs[s].numSites,0);
    }
    auto occupy=[&](int v,int delta) {
        auto& a=sites[p.seg[v]].used;
        int end=p.site[v]+db.sitesOf(v,db.segs[p.seg[v]]);
        for(int x=p.site[v];x<end;++x) a[x]+=delta;
    };
    auto conflict=[&](int v) {
        if(displacementOf(db,p,v)>cap+EPS) return true;
        const auto& a=sites[p.seg[v]].used;
        int end=p.site[v]+db.sitesOf(v,db.segs[p.seg[v]]);
        for(int x=p.site[v];x<end;++x) if(a[x]>1) return true;
        return false;
    };
    for(int v:db.movable) occupy(v,1);
    std::mt19937 rng(seed);
    std::vector<int> active;
    for(int pass=0;pass<500 && wallSeconds()<until;++pass) {
        active.clear();
        for(int v:db.movable) if(conflict(v)) active.push_back(v);
        if(active.empty()) {
            if(checkLegal(db,p).empty() && displacement(db,p).max<=cap+EPS) {out=std::move(p);return true;}
            return false;
        }
        std::shuffle(active.begin(),active.end(),rng);
        double present=1.0+0.03*pass;
        for(int v:active) {
            if(wallSeconds()>=until) return false;
            if(!conflict(v)) continue;
            occupy(v,-1);
            const auto& n=db.nodes[v];
            double best=std::numeric_limits<double>::infinity();
            int bs=-1,bx=0;
            auto first=std::lower_bound(db.rows.begin(),db.rows.end(),n.gy-cap-EPS,
                [](const Row& r,double y){return r.y<y;});
            for(auto row=first;row!=db.rows.end() && row->y<=n.gy+cap+EPS;++row) {
                double dy=std::abs(row->y-n.gy), dx=cap-dy;
                for(int si:row->segs) {
                    if(wallSeconds()>=until) return false;
                    const auto& s=db.segs[si];
                    int w=db.sitesOf(v,s);
                    if(n.h>s.height+EPS || w>s.numSites) continue;
                    double left=(n.gx-dx-s.x0)/s.sp, right=(n.gx+dx-s.x0)/s.sp;
                    if(right<0 || left>s.numSites-w) continue;
                    int lo=std::max(0,int(std::ceil(left-EPS/s.sp)));
                    int hi=std::min(s.numSites-w,int(std::floor(right+EPS/s.sp)));
                    if(lo>hi) continue;
                    const auto& a=sites[si];
                    auto price=[&](int x){return present*a.used[x]+a.history[x];};
                    double cost=0;
                    for(int x=lo;x<lo+w;++x) cost+=price(x);
                    for(int x=lo;x<=hi;++x) {
                        double d=std::abs(s.x0+x*s.sp-n.gx)+dy;
                        double score=cost+0.05*d/std::max(cap, s.sp);
                        // Stable tie break favours the incumbent and avoids drift.
                        if(si==p.seg[v] && x==p.site[v]) score-=1e-9;
                        if(score<best) {best=score;bs=si;bx=x;}
                        if(x<hi) cost+=price(x+w)-price(x);
                    }
                }
            }
            if(bs<0) return false;
            p.seg[v]=bs;p.site[v]=bx;occupy(v,1);
        }
        for(auto& s:sites) for(size_t x=0;x<s.used.size();++x)
            if(s.used[x]>1) s.history[x]+=0.2*(s.used[x]-1);
    }
    return false;
}

void globalMinimax(const PlaceDB& db, Placement& p, double seconds, double totalRatio,
                   int threads, bool verbose) {
    if(seconds<=0 || db.movable.empty()) return;
    double until=wallSeconds()+seconds;
    const auto before=displacement(db,p);
    // Individual bound is exact but ignores overlap. Limit quadratic work on
    // large designs; zero remains a valid lower bound there.
    double low=double(db.movable.size())*db.segs.size()<2e7 ? individualBound(db):0;
    double high=before.max;
    for(int attempt=0;attempt<20 && wallSeconds()<until;++attempt) {
        if(high-low<1e-6*std::max(1.0,high)) break;
        double cap=attempt==0 ? std::max(low,high*0.9) : (low+high)*0.5;
        Placement q=p;
        double remaining=until-wallSeconds();
        double repairUntil=wallSeconds()+std::min(remaining*0.65,std::max(0.15,seconds/5));
        bool ok=repairCap(db,q,cap,repairUntil,12345+attempt);
        if(ok) {
            WindowOptions w;w.passes=1000;w.threads=threads;
            if(db.movable.size()<=1500 && (db.utilization()>=0.4 || db.movable.size()<100)){w.perRow=5;w.perRow3=4;}
            w.timeLimit=std::min(until-wallSeconds(),seconds/5);
            windowSweeps(db,q,w); // uses the new feasible maximum as a hard cap
            auto d=displacement(db,q);
            bool accept=checkLegal(db,q).empty() && d.max<=before.max+EPS && d.total<=before.total*totalRatio+EPS;
            if(verbose) std::fprintf(stderr,"[global-cap] D %.8f feasible: total %.6f (%+.3f%%), max %.6f, %s\n",
                cap,d.total,100*(d.total/std::max(1e-9,before.total)-1),d.max,accept?"accepted":"total budget rejected");
            if(accept) {p=std::move(q);high=d.max;low=std::min(low,high*0.9);} else low=cap;
        } else {
            if(verbose) std::fprintf(stderr,"[global-cap] D %.8f unresolved (not proven infeasible)\n",cap);
            low=cap;
        }
    }
}
