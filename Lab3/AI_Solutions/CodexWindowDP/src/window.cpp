#include "window.hpp"
#include "rowmap.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace {
constexpr double INF = std::numeric_limits<double>::infinity();
constexpr double EPS = 1e-7;
struct RowDP {
    int stride;
    std::vector<double> dp;
    std::vector<signed char> last; // -1: skip a site, otherwise last cell.
};
RowDP solveRow(const WindowProblem& p, int r) {
    int n=p.cells, L=p.length[r], M=1<<n;
    RowDP t{L+1, std::vector<double>(size_t(M)*(L+1),INF),
            std::vector<signed char>(size_t(M)*(L+1),-1)};
    std::fill(t.dp.begin(),t.dp.begin()+L+1,0);
    for(int mask=1;mask<M;++mask) {
        double* cur=t.dp.data()+size_t(mask)*(L+1);
        for(int x=0;x<=L;++x) {
            double best=x ? cur[x-1] : INF;
            int chosen=-1;
            for(int bits=mask;bits;bits&=bits-1) {
                int i=__builtin_ctz(unsigned(bits)), w=p.width[r][i];
                if(w>x) continue;
                double value=t.dp[size_t(mask^(1<<i))*(L+1)+x-w]+p.cost[r][i][x-w];
                if(value<best) {best=value;chosen=i;}
            }
            cur[x]=best;
            t.last[size_t(mask)*(L+1)+x]=static_cast<signed char>(chosen);
        }
    }
    return t;
}
struct Region {int seg, lo, hi; std::vector<int> cells;};
Region region(const PlaceDB& db,const Placement& p,const RowMap& rows,int s,double x,int count,int maxSites) {
    const auto& list=rows.cells(s);const auto& sg=db.segs[s];
    int at=int(std::lower_bound(list.begin(),list.end(),x,[&](int v,double xx){return p.x(db,v)+db.nodes[v].w/2<xx;})-list.begin());
    int first=std::max(0,std::min(at-count/2,int(list.size())-count));
    int last=std::min(int(list.size()),first+count);
    Region q{s,first ? p.site[list[first-1]]+db.sitesOf(list[first-1],sg) : 0,
             last<int(list.size()) ? p.site[list[last]] : sg.numSites,{}};
    q.cells.assign(list.begin()+first,list.begin()+last);
    // Retain every incumbent cell, clip only empty space. If their span alone
    // is too large, the caller skips this region rather than losing feasibility.
    int center=int(std::floor((x-sg.x0)/sg.sp));
    int left=q.cells.empty()?center:p.site[q.cells.front()];
    int right=q.cells.empty()?center:p.site[q.cells.back()]+db.sitesOf(q.cells.back(),sg);
    if(right-left<=maxSites) {
        int pad=(maxSites-(right-left))/2;
        q.lo=std::max(q.lo,left-pad);q.hi=std::min(q.hi,right+pad);
    }
    return q;
}
struct Box {double lx=INF,hx=-INF,ly=INF,hy=-INF;};
void extend(Box& b,double x,double y) {
    b.lx=std::min(b.lx,x);b.hx=std::max(b.hx,x);
    b.ly=std::min(b.ly,y);b.hy=std::max(b.hy,y);
}
double wire(const PlaceDB& db,const Placement& p,const std::vector<int>& nets) {
    double result=0;
    for(int net:nets) if(db.nets[net].size()>1) {
        Box b;for(int v:db.nets[net]) extend(b,p.x(db,v)+db.nodes[v].w/2,p.y(db,v)+db.nodes[v].h/2);
        result+=b.hx-b.lx+b.hy-b.ly;
    }
    return result;
}
}

WindowSolution solveWindow(const WindowProblem& p) {
    if(p.cells<0 || p.cells>10) throw std::runtime_error("window size outside 0..10");
    for(int r=0;r<2;++r) {
        if(p.length[r]<0 || p.length[r]>4096 || p.width[r].size()!=size_t(p.cells) || p.cost[r].size()!=size_t(p.cells))
            throw std::runtime_error("invalid window dimensions");
        for(int i=0;i<p.cells;++i)
            if(p.width[r][i]<1 || p.cost[r][i].size()!=size_t(p.length[r]+1)) throw std::runtime_error("invalid window cost table");
    }
    auto a=solveRow(p,0),b=solveRow(p,1);
    int full=(1<<p.cells)-1,chosen=-1;
    WindowSolution result;result.cost=INF;
    for(int mask=0;mask<=full;++mask) {
        double v=a.dp[size_t(mask)*a.stride+p.length[0]]+b.dp[size_t(full^mask)*b.stride+p.length[1]];
        if(v<result.cost) {result.cost=v;chosen=mask;}
    }
    if(chosen<0) return result;
    result.feasible=true;result.row.resize(p.cells);result.site.resize(p.cells);
    for(int r=0;r<2;++r) {
        const RowDP& t=r?b:a;int mask=r ? full^chosen : chosen,x=p.length[r];
        while(mask) {
            int i=t.last[size_t(mask)*t.stride+x];
            if(i<0) {if(--x<0) throw std::runtime_error("window reconstruction failed");continue;}
            x-=p.width[r][i];result.row[i]=r;result.site[i]=x;mask^=1<<i;
        }
    }
    return result;
}

OrderSolution solveOrder(const OrderProblem& p) {
    int n=int(p.widths.size());
    if(n<1 || n>10 || p.centerOffsets.size()!=size_t(n) || p.gaps.size()!=size_t(n-1))
        throw std::runtime_error("invalid ordering problem");
    int M=1<<n;
    std::vector<int> width(M),gap(n),last(M,-1);
    std::vector<double> dp(M,INF);dp[0]=0;
    for(int i=1;i<n;++i)gap[i]=gap[i-1]+p.gaps[i-1];
    for(int mask=1;mask<M;++mask) {
        int i=__builtin_ctz(unsigned(mask));width[mask]=width[mask^(1<<i)]+p.widths[i];
    }
    for(int mask=0;mask<M-1;++mask) {
        int rank=__builtin_popcount(unsigned(mask));
        for(int i=0;i<n;++i)if(!(mask&(1<<i))) {
            int next=mask|(1<<i);
            double cx=p.origin+(width[mask]+gap[rank])*p.spacing+p.centerOffsets[i],delta=0;
            for(const auto& net:p.nets)if(net.members&(1u<<i)) {
                // Only the first and last window pins determine the final
                // horizontal span. Their identity is known from the mask,
                // so no previous pin coordinates are needed in the DP state.
                if(!(unsigned(mask)&net.members))delta-=std::min(net.outsideLow,cx);
                if((unsigned(next)&net.members)==net.members)delta+=std::max(net.outsideHigh,cx);
            }
            double value=dp[mask]+delta;
            if(value<dp[next]) {dp[next]=value;last[next]=i;}
        }
    }
    OrderSolution result;result.cost=dp.back();
    for(int mask=M-1;mask;) {int i=last[mask];result.order.push_back(i);mask^=1<<i;}
    std::reverse(result.order.begin(),result.order.end());return result;
}

void reorderWindows(const PlaceDB& db,Placement& p,int passes,bool verbose) {
    if(db.movable.empty())return;
    RowMap rows(db,p);
    std::vector<int> mark(db.nodes.size(),-1);
    for(int pass=0;pass<passes;++pass) {
        double before=totalHPWL(db,p);long accepted=0;
        for(int ss=0;ss<int(db.segs.size());++ss) {
            int s=pass%2 ? int(db.segs.size())-1-ss : ss;
            const auto& list=rows.cells(s);const auto& sg=db.segs[s];
            for(int start=(pass%2)*2;start+1<int(list.size());start+=4) {
                int end=std::min(int(list.size()),start+8),n=end-start;
                std::vector<int> cells(list.begin()+start,list.begin()+end),nets,old;
                OrderProblem problem;problem.origin=sg.x0+p.site[cells[0]]*sg.sp;problem.spacing=sg.sp;
                for(int i=0;i<n;++i) {
                    int v=cells[i];mark[v]=i;old.push_back(p.site[v]);
                    problem.widths.push_back(db.sitesOf(v,sg));problem.centerOffsets.push_back(db.nodes[v].w/2);
                    if(i+1<n)problem.gaps.push_back(p.site[cells[i+1]]-p.site[v]-problem.widths.back());
                    nets.insert(nets.end(),db.nodeNets[v].begin(),db.nodeNets[v].end());
                }
                std::sort(nets.begin(),nets.end());nets.erase(std::unique(nets.begin(),nets.end()),nets.end());
                for(int net:nets) {
                    OrderNet term{0,INF,-INF};
                    for(int v:db.nets[net]) {
                        if(mark[v]>=0)term.members|=1u<<mark[v];
                        else {double x=p.x(db,v)+db.nodes[v].w/2;term.outsideLow=std::min(term.outsideLow,x);term.outsideHigh=std::max(term.outsideHigh,x);}
                    }
                    problem.nets.push_back(term);
                }
                auto solution=solveOrder(problem);double oldH=wire(db,p,nets);
                int x=old[0];
                for(int j=0;j<n;++j) {
                    int i=solution.order[j];p.site[cells[i]]=x;
                    x+=problem.widths[i]+(j+1<n?problem.gaps[j]:0);
                }
                if(wire(db,p,nets)<oldH-EPS) {rows.resort(s,start,n);++accepted;}
                else for(int i=0;i<n;++i)p.site[cells[i]]=old[i];
                for(int v:cells)mark[v]=-1;
            }
        }
        double after=totalHPWL(db,p);
        if(after>before+1e-5 || !checkLegal(db,p).empty())throw std::runtime_error("order DP invariant failed");
        if(verbose)std::fprintf(stderr,"[order-hpwl] pass %d: %ld accepted, %.6f -> %.6f\n",pass,accepted,before,after);
        if(!accepted)break;
    }
}

void optimizeWindows(const PlaceDB& db,Placement& p,const WindowOptions& opt,bool hpwl) {
    if(db.movable.empty() || opt.passes==0) return;
    if(opt.perRow<1 || opt.perRow>5 || opt.maxSites<1 || opt.maxSites>4096) throw std::runtime_error("invalid window options");
    RowMap rows(db,p);
    const double cap=displacement(db,p).max; // Never increases during a sweep.
    for(int pass=0;pass<opt.passes;++pass) {
        auto before=displacement(db,p);double beforeH=hpwl?totalHPWL(db,p):0;
        long tried=0,accepted=0;
        // Snapshot anchors, not cell lists: lists are rebuilt after every move.
        std::vector<int> anchors;
        for(size_t s=0;s<db.segs.size();++s) {
            const auto& list=rows.cells(int(s));
            for(int k=(pass%2)*opt.perRow/2;k<int(list.size());k+=opt.perRow) anchors.push_back(list[k]);
        }
        if(pass%2) std::reverse(anchors.begin(),anchors.end());
        for(int anchor:anchors) {
            int s=p.seg[anchor],r=db.segs[s].row;
            int r2=r+(pass%2 ? -1 : 1);
            if(r2<0 || r2>=int(db.rows.size())) continue;
            double x=p.x(db,anchor)+db.nodes[anchor].w/2;
            int other=-1;double distance=INF;
            for(int t:db.rows[r2].segs) {
                const auto& sg=db.segs[t];
                double d=std::max({0.0,sg.x0-x,x-(sg.x0+sg.numSites*sg.sp)});
                if(d<distance) {distance=d;other=t;}
            }
            if(other<0) continue;
            std::array<Region,2> q{region(db,p,rows,s,x,opt.perRow,opt.maxSites),
                                   region(db,p,rows,other,x,opt.perRow,opt.maxSites)};
            if(q[0].hi<=q[0].lo || q[1].hi<=q[1].lo || q[0].hi-q[0].lo>opt.maxSites || q[1].hi-q[1].lo>opt.maxSites) continue;
            auto cells=q[0].cells;cells.insert(cells.end(),q[1].cells.begin(),q[1].cells.end());
            int n=int(cells.size());if(!n) continue;
            WindowProblem problem;problem.cells=n;
            std::vector<int> nets;
            std::vector<std::vector<Box>> boxes(n);
            double old=0;
            for(int i=0;i<n;++i) {
                int v=cells[i];old+=displacementOf(db,p,v);
                if(hpwl) for(int net:db.nodeNets[v]) {
                    nets.push_back(net);if(db.nets[net].size()<2)continue;
                    Box box;
                    for(int u:db.nets[net]) if(u!=v) extend(box,p.x(db,u)+db.nodes[u].w/2,p.y(db,u)+db.nodes[u].h/2);
                    boxes[i].push_back(box);
                }
            }
            for(int j=0;j<2;++j) {
                const auto& sg=db.segs[q[j].seg];int L=q[j].hi-q[j].lo;
                problem.length[j]=L;problem.width[j].resize(n);problem.cost[j].resize(n);
                for(int i=0;i<n;++i) {
                    int v=cells[i],w=db.sitesOf(v,sg);problem.width[j][i]=w;
                    auto& costs=problem.cost[j][i];costs.assign(L+1,INF);
                    if(std::fabs(db.nodes[v].h-sg.height)>EPS)continue;
                    for(int t=0;t+w<=L;++t) {
                        double px=sg.x0+(q[j].lo+t)*sg.sp;
                        if(!hpwl) {
                            double d=std::fabs(px-db.nodes[v].gx)+std::fabs(sg.y-db.nodes[v].gy);
                            if(d<=cap+EPS)costs[t]=d;
                        } else {
                            double cx=px+db.nodes[v].w/2,cy=sg.y+db.nodes[v].h/2,value=0;
                            for(const Box& b:boxes[i]) value+=std::max(b.hx,cx)-std::min(b.lx,cx)+std::max(b.hy,cy)-std::min(b.ly,cy);
                            costs[t]=value;
                        }
                    }
                }
            }
            ++tried;auto solution=solveWindow(problem);
            if(!solution.feasible) throw std::runtime_error("window lost the incumbent solution");
            if(!hpwl && solution.cost>=old-EPS)continue;
            std::vector<int> oldS,oldX;
            std::sort(nets.begin(),nets.end());nets.erase(std::unique(nets.begin(),nets.end()),nets.end());
            double oldH=hpwl?wire(db,p,nets):0;
            for(int i=0;i<n;++i) {
                int v=cells[i];oldS.push_back(p.seg[v]);oldX.push_back(p.site[v]);
                p.seg[v]=q[solution.row[i]].seg;p.site[v]=q[solution.row[i]].lo+solution.site[i];
            }
            bool improve=!hpwl || wire(db,p,nets)<oldH-EPS;
            // Restore before changing RowMap: its remove() requires old sites.
            for(int i=0;i<n;++i) {p.seg[cells[i]]=oldS[i];p.site[cells[i]]=oldX[i];}
            if(!improve)continue;
            for(int v:cells)rows.remove(v);
            for(int i=0;i<n;++i) rows.insert(cells[i],q[solution.row[i]].seg,q[solution.row[i]].lo+solution.site[i]);
            ++accepted;
        }
        auto after=displacement(db,p);double afterH=hpwl?totalHPWL(db,p):0;
        if(!checkLegal(db,p).empty() || (hpwl ? afterH>beforeH+1e-5 : after.total>before.total+1e-5 || after.max>cap+1e-6))
            throw std::runtime_error("window monotonicity or legality check failed");
        if(opt.verbose)std::fprintf(stderr,"[window-%s] pass %d: %ld/%ld accepted, %.6f -> %.6f, max %.6f\n",
            hpwl?"hpwl":"disp",pass,accepted,tried,hpwl?beforeH:before.total,hpwl?afterH:after.total,after.max);
        if(!accepted)break;
    }
}
