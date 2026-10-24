// Codex additions: exact constrained bottleneck matching, net-independent
// HPWL assignment, coordinate descent, and exhaustive three-cell reordering.
// References and inherited components are identified in README.md.
#include "optimizer.hpp"
#include "rowmap.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <tuple>

namespace {
constexpr double INF = std::numeric_limits<double>::infinity();
constexpr double EPS = 1e-7;
}

Assignment minAssignment(const std::vector<double>& costs, int n, double forbidden) {
    if (costs.size() != size_t(n)*n) throw std::runtime_error("assignment matrix size mismatch");
    Assignment result;
    std::vector<double> rowPotential(n+1), colPotential(n+1), distance(n+1);
    std::vector<int> owner(n+1), previous(n+1);
    std::vector<bool> used(n+1);
    for (int row=1;row<=n;++row) {
        owner[0]=row;
        int col=0;
        std::fill(distance.begin(),distance.end(),INF);
        std::fill(used.begin(),used.end(),false);
        do {
            used[col]=true;
            int activeRow=owner[col], next=0;
            double step=INF;
            for(int j=1;j<=n;++j) if(!used[j]) {
                double edge=costs[size_t(activeRow-1)*n+j-1];
                if(edge<forbidden) {
                    double reduced=edge-rowPotential[activeRow]-colPotential[j];
                    if(reduced<distance[j]) {distance[j]=reduced;previous[j]=col;}
                }
                if(distance[j]<step) {step=distance[j];next=j;}
            }
            if(!std::isfinite(step)) return result; // No augmenting path, not a huge-cost fake edge.
            for(int j=0;j<=n;++j) {
                if(used[j]) {rowPotential[owner[j]]+=step;colPotential[j]-=step;}
                else distance[j]-=step;
            }
            col=next;
        } while(owner[col]);
        do {int prev=previous[col];owner[col]=owner[prev];col=prev;} while(col);
    }
    result.column.resize(n);
    for(int j=1;j<=n;++j) result.column[owner[j]-1]=j-1;
    for(int i=0;i<n;++i) result.cost+=costs[size_t(i)*n+result.column[i]];
    result.feasible=true;
    return result;
}

Assignment paretoAssignment(const std::vector<double>& d,int n) {
    double originalTotal=0,originalMax=0;
    for(int i=0;i<n;++i) {originalTotal+=d[size_t(i)*n+i];originalMax=std::max(originalMax,d[size_t(i)*n+i]);}
    std::vector<double> bounds;
    for(double value:d) if(value<=originalMax) bounds.push_back(value);
    std::sort(bounds.begin(),bounds.end());
    bounds.erase(std::unique(bounds.begin(),bounds.end()),bounds.end());
    Assignment best;
    auto solve=[&](double bound) {
        auto restricted=d;
        for(double& value:restricted) if(value>bound) value=INF;
        return minAssignment(restricted,n);
    };
    int low=0,high=int(bounds.size())-1;
    // Increasing the cap only adds allowed edges: minimum total cost is
    // non-increasing. Binary search therefore finds the exact smallest cap
    // for which an assignment fits the ORIGINAL TOTAL displacement budget.
    while(low<=high) {
        int middle=low+(high-low)/2;
        Assignment a=solve(bounds[middle]);
        if(a.feasible && a.cost<=originalTotal+EPS) {best=std::move(a);high=middle-1;}
        else low=middle+1;
    }
    if(!best.feasible) throw std::runtime_error("identity assignment unexpectedly infeasible");
    return best;
}

namespace {
using Classes=std::map<std::pair<double,double>,std::vector<int>>;
Classes classes(const PlaceDB& db) {
    Classes result;
    for(int v:db.movable) result[{db.nodes[v].w,db.nodes[v].h}].push_back(v);
    return result;
}
struct Slot {int segment,site;};
std::vector<Slot> slots(const Placement& p,const std::vector<int>& group) {
    std::vector<Slot> result;
    for(int v:group) result.push_back({p.seg[v],p.site[v]});
    return result;
}
void apply(Placement& p,const std::vector<int>& group,const std::vector<Slot>& positions,const Assignment& a) {
    for(size_t i=0;i<group.size();++i) {
        p.seg[group[i]]=positions[a.column[i]].segment;
        p.site[group[i]]=positions[a.column[i]].site;
    }
}
void spatialSort(const PlaceDB& db,const Placement& p,std::vector<int>& group,int pass,bool midpoint) {
    double rh=db.rows.front().height,band=(pass%2 ? 6 : 12)*rh;
    auto key=[&](int v) {
        double x=p.x(db,v),y=p.y(db,v);
        if(midpoint) {x=(x+db.nodes[v].gx)/2;y=(y+db.nodes[v].gy)/2;}
        return std::make_tuple(std::floor(y/band+0.5*(pass%2)),x,v);
    };
    std::sort(group.begin(),group.end(),[&](int a,int b){return key(a)<key(b);});
}

struct Box {double lx,hx,ly,hy;};
std::vector<Box> externalBoxes(const PlaceDB& db,const Placement& p,int v) {
    std::vector<Box> result;
    for(int n:db.nodeNets[v]) if(db.nets[n].size()>1) {
        Box box{INF,-INF,INF,-INF};
        for(int u:db.nets[n]) if(u!=v) {
            double x=p.x(db,u)+db.nodes[u].w/2,y=p.y(db,u)+db.nodes[u].h/2;
            box.lx=std::min(box.lx,x);box.hx=std::max(box.hx,x);
            box.ly=std::min(box.ly,y);box.hy=std::max(box.hy,y);
        }
        result.push_back(box);
    }
    return result;
}
double boxCost(const std::vector<Box>& boxes,double x,double y) {
    double result=0;
    for(const Box& b:boxes) result+=std::max(b.hx,x)-std::min(b.lx,x)+std::max(b.hy,y)-std::min(b.ly,y);
    return result;
}
double netCost(const PlaceDB& db,const Placement& p,const std::vector<int>& nets) {
    double total=0;
    for(int n:nets) if(db.nets[n].size()>1) {
        Box b{INF,-INF,INF,-INF};
        for(int v:db.nets[n]) {
            double x=p.x(db,v)+db.nodes[v].w/2,y=p.y(db,v)+db.nodes[v].h/2;
            b.lx=std::min(b.lx,x);b.hx=std::max(b.hx,x);b.ly=std::min(b.ly,y);b.hy=std::max(b.hy,y);
        }
        total+=b.hx-b.lx+b.hy-b.ly;
    }
    return total;
}

// Exact one-dimensional integer minimization with other cells fixed. Incident
// net boxes are computed once, then their convex sum is searched by differences.
long slideCells(const PlaceDB& db,Placement& p,bool reverse) {
    RowMap rows(db,p);
    auto order=db.movable;if(reverse) std::reverse(order.begin(),order.end());
    long changed=0;
    for(int v:order) {
        auto boxes=externalBoxes(db,p,v);if(boxes.empty()) continue;
        int s=p.seg[v];const auto& sg=db.segs[s];
        Gap gap=rows.freeAround(v);int lo=gap.lo,hi=gap.hi-db.sitesOf(v,sg);
        auto cost=[&](int x){return boxCost(boxes,sg.x0+x*sg.sp+db.nodes[v].w/2,sg.y+db.nodes[v].h/2);};
        while(lo<hi) {int m=lo+(hi-lo)/2;if(cost(m+1)>=cost(m)) hi=m;else lo=m+1;}
        if(cost(lo)<cost(p.site[v])-EPS) {rows.slide(v,lo);++changed;}
    }
    return changed;
}

double medianTarget(const std::vector<Box>& boxes,bool horizontal,double current) {
    std::vector<double> ends;
    for(const auto& b:boxes) {ends.push_back(horizontal?b.lx:b.ly);ends.push_back(horizontal?b.hx:b.hy);}
    if(ends.empty()) return current;
    std::sort(ends.begin(),ends.end());size_t k=ends.size()/2;
    return std::clamp(current,ends[k-1],ends[k]);
}

// Monotone cross-row search around both the current and median-optimal rows.
// Unlike an SA proposal, examine several legal alternatives and take the best.
long relocateCells(const PlaceDB& db,Placement& p,bool reverse) {
    RowMap rows(db,p);long changed=0;
    auto order=db.movable;if(reverse)std::reverse(order.begin(),order.end());
    for(int a:order) {
        auto boxes=externalBoxes(db,p,a);if(boxes.empty())continue;
        int sa=p.seg[a],xa=p.site[a];const auto& cell=db.nodes[a];
        double cx=p.x(db,a)+cell.w/2,cy=p.y(db,a)+cell.h/2;
        double tx=medianTarget(boxes,true,cx),ty=medianTarget(boxes,false,cy);
        double ownCost=boxCost(boxes,cx,cy),bestDelta=-EPS;
        int bestB=-1,bestAS=-1,bestAX=0,bestBX=0;
        Gap roomA=rows.freeAround(a);
        std::vector<int> candidates;
        for(int center:{db.segs[sa].row,db.nearestRow(ty-cell.h/2)})
            for(int r=center-1;r<=center+1;++r) if(r>=0 && r<int(db.rows.size()))candidates.push_back(r);
        std::sort(candidates.begin(),candidates.end());candidates.erase(std::unique(candidates.begin(),candidates.end()),candidates.end());
        for(int r:candidates) for(int s:db.rows[r].segs) {
            const auto& sg=db.segs[s];int widthA=db.sitesOf(a,sg);
            if(std::abs(sg.height-cell.h)>1e-6 || widthA>sg.numSites)continue;
            int target=int(std::llround(std::clamp((tx-cell.w/2-sg.x0)/sg.sp,0.0,double(sg.numSites-widthA))));
            auto tryGap=[&](Gap gap) {
                if(gap.width()<widthA)return;
                int x=std::clamp(target,gap.lo,gap.hi-widthA);
                double d=boxCost(boxes,sg.x0+x*sg.sp+cell.w/2,sg.y+cell.h/2)-ownCost;
                if(d<bestDelta) {bestDelta=d;bestB=-1;bestAS=s;bestAX=x;}
            };
            Gap at;int occupied=rows.cellAt(s,target,at,a);
            if(occupied<0)tryGap(at);
            const auto& list=rows.cells(s);
            int index=int(std::lower_bound(list.begin(),list.end(),target,[&](int v,int x){return p.site[v]<x;})-list.begin());
            for(int j=std::max(0,index-2);j<std::min(int(list.size()),index+2);++j) {
                int b=list[j];
                // Inspect adjacent gaps too, including a's now-available old slot.
                Gap g;int right=p.site[b]+db.sitesOf(b,sg);
                if(right<sg.numSites && rows.cellAt(s,right,g,a)<0)tryGap(g);
                if(b==a || (s==sa && std::abs(rows.indexOf(a)-j)<=1))continue;
                int widthB=db.sitesOf(b,db.segs[sa]);Gap roomB=rows.freeAround(b);
                if(roomB.width()<widthA || roomA.width()<widthB || std::abs(db.nodes[b].h-db.segs[sa].height)>1e-6)continue;
                int ax=std::clamp(target,roomB.lo,roomB.hi-widthA),xb=p.site[b];
                auto bboxes=externalBoxes(db,p,b);
                double bx=medianTarget(bboxes,true,p.x(db,b)+db.nodes[b].w/2);
                int newBX=int(std::llround(std::clamp((bx-db.nodes[b].w/2-db.segs[sa].x0)/db.segs[sa].sp,double(roomA.lo),double(roomA.hi-widthB))));
                std::vector<int> nets=db.nodeNets[a];nets.insert(nets.end(),db.nodeNets[b].begin(),db.nodeNets[b].end());
                std::sort(nets.begin(),nets.end());nets.erase(std::unique(nets.begin(),nets.end()),nets.end());
                double before=netCost(db,p,nets);
                p.seg[a]=s;p.site[a]=ax;p.seg[b]=sa;p.site[b]=newBX;
                double delta=netCost(db,p,nets)-before;
                p.seg[a]=sa;p.site[a]=xa;p.seg[b]=s;p.site[b]=xb;
                if(delta<bestDelta){bestDelta=delta;bestB=b;bestAS=s;bestAX=ax;bestBX=newBX;}
            }
        }
        if(bestAS>=0) {
            if(bestB<0){rows.remove(a);rows.insert(a,bestAS,bestAX);}
            else rows.exchange(a,bestB,bestAS,bestAX,sa,bestBX);
            ++changed;
        }
    }
    return changed;
}

long independentMatching(const PlaceDB& db,Placement& p,int pass,int maximum) {
    if(!db.uniformSites) return 0;
    auto buckets=classes(db);
    std::vector<int> netMark(db.nets.size(),0);int epoch=0;
    long changed=0;
    for(auto& [size,ordered]:buckets) {
        (void)size;spatialSort(db,p,ordered,pass,false);
        // Small spatial pools avoid assigning distant slots; all unused cells
        // stay in the pool for the next independent set rather than being lost.
        for(size_t start=0;start<ordered.size();start+=4*maximum) {
            size_t end=std::min(ordered.size(),start+4*maximum);
            std::vector<bool> taken(end-start,false);size_t remaining=end-start;
            while(remaining) {
                std::vector<int> group;++epoch;
                for(size_t i=start;i<end && group.size()<size_t(maximum);++i) if(!taken[i-start]) {
                    int v=ordered[i];bool independent=true;
                    for(int n:db.nodeNets[v]) if(netMark[n]==epoch) {independent=false;break;}
                    if(!independent) continue;
                    group.push_back(v);taken[i-start]=true;--remaining;
                    for(int n:db.nodeNets[v]) netMark[n]=epoch;
                }
                if(group.size()<2) continue;
                auto positions=slots(p,group);int count=int(group.size());
                std::vector<double> matrix(size_t(count)*count);double old=0;
                for(int i=0;i<count;++i) {
                    int v=group[i];auto boxes=externalBoxes(db,p,v);
                    for(int j=0;j<count;++j) {
                        const auto& sg=db.segs[positions[j].segment];
                        matrix[size_t(i)*count+j]=boxCost(boxes,sg.x0+positions[j].site*sg.sp+db.nodes[v].w/2,sg.y+db.nodes[v].h/2);
                    }
                    old+=matrix[size_t(i)*count+i];
                }
                Assignment a=minAssignment(matrix,count);
                if(a.cost<old-EPS) {apply(p,group,positions,a);++changed;}
            }
        }
    }
    return changed;
}

// Full six-permutation search. Shared nets are evaluated once as a UNION;
// adding individual cell deltas would be incorrect for connected neighbours.
long reorderTriples(const PlaceDB& db,Placement& p,bool reverse) {
    RowMap rows(db,p);long changed=0;
    for(int s=0;s<int(db.segs.size());++s) {
        const auto& sg=db.segs[s];int count=int(rows.cells(s).size());
        for(int k=0;k+2<count;++k) {
            int first=reverse ? count-3-k : k;
            std::vector<int> group;
            for(int j=0;j<3;++j) group.push_back(rows.cells(s)[first+j]);
            int gap1=p.site[group[1]]-p.site[group[0]]-db.sitesOf(group[0],sg);
            int gap2=p.site[group[2]]-p.site[group[1]]-db.sitesOf(group[1],sg);
            int left=p.site[group[0]];
            std::vector<int> nets;
            for(int v:group) nets.insert(nets.end(),db.nodeNets[v].begin(),db.nodeNets[v].end());
            std::sort(nets.begin(),nets.end());nets.erase(std::unique(nets.begin(),nets.end()),nets.end());
            double best=netCost(db,p,nets);auto old=slots(p,group),chosen=old;
            int permutation[3]={0,1,2};
            do {
                int x=left;
                for(int j=0;j<3;++j) {int v=group[permutation[j]];p.site[v]=x;x+=db.sitesOf(v,sg)+(j==0?gap1:j==1?gap2:0);}
                double cost=netCost(db,p,nets);
                if(cost<best-EPS) {best=cost;chosen=slots(p,group);}
            } while(std::next_permutation(permutation,permutation+3));
            bool different=false;
            for(int j=0;j<3;++j) {different|=old[j].site!=chosen[j].site;p.site[group[j]]=chosen[j].site;}
            if(different) {rows.resort(s,first,3);++changed;}
        }
    }
    return changed;
}
}

void paretoDisplacement(const PlaceDB& db,Placement& p,const OptimizeOptions& opt) {
    if(!db.uniformSites || db.movable.empty()) return;
    auto buckets=classes(db);
    for(int pass=0;pass<opt.paretoPasses && wallSeconds()<opt.until;++pass) {
        long changed=0;
        for(auto& [size,ordered]:buckets) {
            (void)size;spatialSort(db,p,ordered,pass,true);
            size_t offset=pass%2 ? size_t(opt.groupSize/2) : 0;
            for(size_t first=0;first<ordered.size();) {
                size_t end=std::min(ordered.size(),first+(first==0 && offset ? offset : opt.groupSize));
                std::vector<int> group(ordered.begin()+first,ordered.begin()+end);first=end;
                int n=int(group.size());if(n<2) continue;
                auto positions=slots(p,group);
                std::vector<double> matrix(size_t(n)*n);
                for(int i=0;i<n;++i) for(int j=0;j<n;++j) {
                    const auto& sg=db.segs[positions[j].segment];const auto& cell=db.nodes[group[i]];
                    matrix[size_t(i)*n+j]=std::abs(sg.x0+positions[j].site*sg.sp-cell.gx)+std::abs(sg.y-cell.gy);
                }
                Assignment a=paretoAssignment(matrix,n);
                double oldTotal=0,oldMax=0,newMax=0;
                for(int i=0;i<n;++i) {oldTotal+=matrix[size_t(i)*n+i];oldMax=std::max(oldMax,matrix[size_t(i)*n+i]);newMax=std::max(newMax,matrix[size_t(i)*n+a.column[i]]);}
                if(a.cost<=oldTotal+EPS && newMax<=oldMax+EPS && (a.cost<oldTotal-EPS || newMax<oldMax-EPS)) {
                    apply(p,group,positions,a);++changed;
                }
            }
        }
        if(opt.verbose) {auto d=displacement(db,p);std::fprintf(stderr,"[pareto] pass %d: %ld groups, total %.6f, max %.6f\n",pass+1,changed,d.total,d.max);}
        if(!changed) break;
    }
}

void polishHPWL(const PlaceDB& db,Placement& p,const OptimizeOptions& opt,const std::string& path) {
    std::ofstream trace;
    if(!path.empty()) {
        trace.open(path);if(!trace) throw std::runtime_error("cannot write polish trace");
        trace<<std::setprecision(15)<<"pass,phase,hpwl,changes\n0,start,"<<totalHPWL(db,p)<<",0\n";
    }
    double last=0;   // v6: seconds of the previous pass
    for(int pass=0;pass<opt.polishPasses && wallSeconds()<opt.until;++pass) {
        // v6: a pass is not interrupted, so start one only if a pass as long
        // as the last one still fits (v5's 30 s polish took 34 s on L2)
        if(pass>0 && wallSeconds()+last>opt.until) break;
        const double t0=wallSeconds();
        double old=totalHPWL(db,p);Placement before=p;
        long a=slideCells(db,p,pass%2);
        if(trace.is_open()) trace<<pass+1<<",slide,"<<totalHPWL(db,p)<<','<<a<<'\n';
        long moves=relocateCells(db,p,pass%2);
        if(trace.is_open()) trace<<pass+1<<",relocate,"<<totalHPWL(db,p)<<','<<moves<<'\n';
        long b=independentMatching(db,p,pass,opt.groupSize);
        if(trace.is_open()) trace<<pass+1<<",matching,"<<totalHPWL(db,p)<<','<<b<<'\n';
        long c=reorderTriples(db,p,pass%2);
        double now=totalHPWL(db,p);
        if(trace.is_open()) trace<<pass+1<<",reorder,"<<now<<','<<c<<'\n';
        std::string bad=checkLegal(db,p);
        if(!bad.empty() || now>old+1e-5) {p=std::move(before);throw std::runtime_error("polish invariant failed: "+bad);}
        if(opt.verbose) std::fprintf(stderr,"[polish] pass %d: %.6f -> %.6f; slide %ld, relocate %ld, assignment %ld, reorder %ld\n",pass+1,old,now,a,moves,b,c);
        last=wallSeconds()-t0;
        if(old-now<=std::max(1e-7,old*1e-6)) break;
        // v6: past v5's 3 passes, only while a pass still gains 1 %: on L2
        // pass 3 gained 7 % (the SA hardly works there), on L1 0.15 %, and
        // there the HPWL windows / order DP after the polish are worth more
        if(pass>=2 && old-now<0.01*old) break;
    }
    if(trace.is_open()) {trace.close();if(!trace) throw std::runtime_error("failed writing polish trace");}
}
