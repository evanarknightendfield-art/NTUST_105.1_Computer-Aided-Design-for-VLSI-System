// window.cpp — window dynamic programs.
//   CodexWindowDP: exact two-row subset DP (solveWindow), exact same-row
//   HPWL ordering DP (solveOrder / reorderWindows), HPWL window proposals.
//   v4: 1- to 3-row DP (solveMulti), sweeps to convergence with a memo of
//   already-solved windows, parallel row bands, max-displacement windows.
#include "window.hpp"
#include "pool.hpp"
#include "rowmap.hpp"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
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

void reorderWindows(const PlaceDB& db,Placement& p,int passes,bool verbose,double until) {
    if(db.movable.empty())return;
    RowMap rows(db,p);
    std::vector<int> mark(db.nodes.size(),-1);
    for(int pass=0;pass<passes && wallSeconds()<until;++pass) {
        double before=totalHPWL(db,p);long accepted=0;
        Placement safe=p;
        for(int ss=0;ss<int(db.segs.size());++ss) {
            if((ss&15)==0 && wallSeconds()>until) break;   // v4: time budget
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
        if(after>before+1e-5 || !checkLegal(db,p).empty()) {   // v4: discard instead of aborting the run
            p=std::move(safe);
            if(verbose)std::fprintf(stderr,"[order-hpwl] invariant failed, pass discarded\n");
            return;
        }
        if(verbose)std::fprintf(stderr,"[order-hpwl] pass %d: %ld accepted, %.6f -> %.6f\n",pass,accepted,before,after);
        if(!accepted)break;
    }
}

// ---------------------------------------------------------------- v4
namespace {
// v4: the window DP for R = 1..3 rows. Same per-row subset DP as CodexWindowDP
// (F_r(S, x): set S packed into the first x sites of row r); the rows are then
// combined over all ordered partitions of the cell set: for 3 rows
// G(C) = min_{S in C} F_1(S) + F_2(C \ S) over all submasks (3^k pairs in
// total), answer = min_S F_0(S) + G(U \ S). Exact for the window, like the
// two-row version; a 3-row window lets chains of cells shift down (or up)
// one row each, which is what the optimal solutions do (h01: 22 of the cells
// that differ from the proven optimum change rows, mostly by one).
RowDP solveRowM(const MultiProblem& p, int r) {
    int n = p.cells, L = p.length[r], M = 1 << n;
    RowDP t{L + 1, std::vector<double>(size_t(M) * (L + 1), INF), std::vector<signed char>(size_t(M) * (L + 1), -1)};
    std::fill(t.dp.begin(), t.dp.begin() + L + 1, 0);
    for (int mask = 1; mask < M; ++mask) {
        double* cur = t.dp.data() + size_t(mask) * (L + 1);
        for (int x = 0; x <= L; ++x) {
            double best = x ? cur[x - 1] : INF;
            int chosen = -1;
            for (int bits = mask; bits; bits &= bits - 1) {
                int i = __builtin_ctz(unsigned(bits)), w = p.width[r][i];
                if (w > x) continue;
                double value = t.dp[size_t(mask ^ (1 << i)) * (L + 1) + x - w] + p.cost[r][i][x - w];
                if (value < best) { best = value; chosen = i; }
            }
            cur[x] = best;
            t.last[size_t(mask) * (L + 1) + x] = static_cast<signed char>(chosen);
        }
    }
    return t;
}
}  // namespace

WindowSolution solveMulti(const MultiProblem& p) {
    int R = int(p.length.size()), n = p.cells, full = (1 << n) - 1;
    WindowSolution res;
    res.cost = INF;
    if (R < 1 || R > 3 || n < 0 || n > 12) return res;
    std::vector<RowDP> t;
    for (int r = 0; r < R; ++r) t.push_back(solveRowM(p, r));
    auto F = [&](int r, int mask) { return t[r].dp[size_t(mask) * t[r].stride + p.length[r]]; };
    std::vector<int> part(R, 0);   // mask per row
    if (R == 1) {
        res.cost = F(0, full);
        part[0] = full;
    } else if (R == 2) {
        for (int m = 0; m <= full; ++m) {
            double v = F(0, m) + F(1, full ^ m);
            if (v < res.cost) { res.cost = v; part = {m, full ^ m}; }
        }
    } else {
        std::vector<double> G(size_t(full) + 1, INF);
        std::vector<int> arg(size_t(full) + 1, 0);
        for (int C = 0; C <= full; ++C)
            for (int S = C;; S = (S - 1) & C) {
                double v = F(1, S) + F(2, C ^ S);
                if (v < G[C]) { G[C] = v; arg[C] = S; }
                if (S == 0) break;
            }
        for (int m = 0; m <= full; ++m) {
            double v = F(0, m) + G[full ^ m];
            if (v < res.cost) { res.cost = v; part = {m, arg[full ^ m], (full ^ m) ^ arg[full ^ m]}; }
        }
    }
    if (!(res.cost < INF)) return res;
    res.feasible = true;
    res.row.assign(n, 0);
    res.site.assign(n, 0);
    for (int r = 0; r < R; ++r) {
        int mask = part[r], x = p.length[r];
        while (mask) {
            int i = t[r].last[size_t(mask) * t[r].stride + x];
            if (i < 0) {
                if (--x < 0) { res.feasible = false; return res; }
                continue;
            }
            x -= p.width[r][i];
            res.row[i] = r;
            res.site[i] = x;
            mask ^= 1 << i;
        }
    }
    return res;
}

namespace {
struct Window {
    std::vector<Region> q;   // one region per row of the window
    std::vector<int> cells;
};
// Segment of row r nearest to x, or -1.
int nearestSeg(const PlaceDB& db, int r, double x) {
    int best = -1;
    double distance = INF;
    for (int t : db.rows[r].segs) {
        const auto& sg = db.segs[t];
        double d = std::max({0.0, sg.x0 - x, x - (sg.x0 + sg.numSites * sg.sp)});
        if (d < distance) { distance = d; best = t; }
    }
    return best;
}
// Window around `anchor`: its own row plus rows r + offsets (e.g. {+1} or
// {-1, +1}). Returns false when the window is skipped (too wide, no row).
bool buildWindow(const PlaceDB& db, const Placement& p, const RowMap& rows, int anchor,
                 const std::vector<int>& offsets, const WindowOptions& opt, Window& w) {
    int s = p.seg[anchor], r = db.segs[s].row;
    double x = p.x(db, anchor) + db.nodes[anchor].w / 2;
    w.q.clear();
    w.q.push_back(region(db, p, rows, s, x, opt.perRow, opt.maxSites));
    for (int off : offsets) {
        int r2 = r + off;
        if (r2 < 0 || r2 >= int(db.rows.size())) continue;
        int other = nearestSeg(db, r2, x);
        if (other < 0) continue;
        w.q.push_back(region(db, p, rows, other, x, opt.perRow, opt.maxSites));
    }
    if (w.q.size() < 2) return false;
    w.cells.clear();
    for (const Region& q : w.q) w.cells.insert(w.cells.end(), q.cells.begin(), q.cells.end());
    if (w.cells.empty() || w.cells.size() > 12) return false;
    // v4: trim the empty padding to where these cells could want to go: from
    // the leftmost global x of the window's cells to the rightmost one (plus
    // a cell width), and never less than the cells' current span. In sparse
    // rows CodexWindowDP padded every window to 256 sites; the DP is linear
    // in the length, so this makes it 3 - 5x faster with the same results.
    double gl = INF, gr = -INF, wmax = 0;
    for (int v : w.cells) {
        gl = std::min(gl, db.nodes[v].gx);
        gr = std::max(gr, db.nodes[v].gx + db.nodes[v].w);
        wmax = std::max(wmax, db.nodes[v].w);
    }
    for (Region& q : w.q) {
        const auto& sg = db.segs[q.seg];
        int lo = int(std::floor((gl - wmax - sg.x0) / sg.sp)), hi = int(std::ceil((gr + wmax - sg.x0) / sg.sp));
        if (!q.cells.empty()) {
            lo = std::min(lo, p.site[q.cells.front()]);
            hi = std::max(hi, p.site[q.cells.back()] + db.sitesOf(q.cells.back(), sg));
        }
        q.lo = std::max(q.lo, lo);
        q.hi = std::min(q.hi, hi);
        if (q.hi <= q.lo || q.hi - q.lo > opt.maxSites) return false;
    }
    return true;
}
// Exact displacement optimum of the window under `cap` (every cell at most cap
// from its global position). feasible = false if the cap cannot be met.
WindowSolution solveDisp(const PlaceDB& db, const Window& w, double cap) {
    MultiProblem problem;
    int n = int(w.cells.size()), R = int(w.q.size());
    problem.cells = n;
    problem.length.resize(R);
    problem.width.resize(R);
    problem.cost.resize(R);
    for (int j = 0; j < R; ++j) {
        const auto& sg = db.segs[w.q[j].seg];
        int L = w.q[j].hi - w.q[j].lo;
        problem.length[j] = L;
        problem.width[j].resize(n);
        problem.cost[j].resize(n);
        for (int i = 0; i < n; ++i) {
            int v = w.cells[i], wv = db.sitesOf(v, sg);
            problem.width[j][i] = wv;
            auto& costs = problem.cost[j][i];
            costs.assign(L + 1, INF);
            if (std::fabs(db.nodes[v].h - sg.height) > EPS) continue;
            double dy = std::fabs(sg.y - db.nodes[v].gy);
            if (dy > cap + EPS) continue;
            for (int t = 0; t + wv <= L; ++t) {
                double d = std::fabs(sg.x0 + (w.q[j].lo + t) * sg.sp - db.nodes[v].gx) + dy;
                if (d <= cap + EPS) costs[t] = d;
            }
        }
    }
    return solveMulti(problem);
}
void applyWindow(RowMap& rows, const Window& w, const WindowSolution& sol) {
    for (int v : w.cells) rows.remove(v);
    for (size_t i = 0; i < w.cells.size(); ++i)
        rows.insert(w.cells[i], w.q[sol.row[i]].seg, w.q[sol.row[i]].lo + sol.site[i]);
}
double seconds(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
}  // namespace

// v4: total displacement, run to convergence. CodexWindowDP stopped after two
// sweeps, but measured on ibm01 / h04 the sweeps keep finding improvements for
// 20+ passes (another 3 - 7 %). A window is solved again only if one of its
// cells, or a neighbour that bounds it, moved since it was last solved without
// gain (`mod` / `seen` stamps), so the later passes cost little.
// opt.rows = 2: rows (r, r+1) and (r, r-1) on alternate passes; 3: (r-1, r, r+1).
namespace {
// Rows split into `bands` bands of about equal cell count; shift = 1 moves
// every boundary by half a band (see the parallel SA).
// Every band keeps at least minRows rows, so each window fits inside a band
// in at least one of the two layouts.
std::vector<int> rowBands(const PlaceDB& db, const Placement& p, int bands, int shift, int minRows = 6) {
    int R = int(db.rows.size());
    std::vector<double> cnt(R, 0);
    for (int v : db.movable) cnt[db.segs[p.seg[v]].row] += 1;
    double total = 0;
    for (double c : cnt) total += c;
    std::vector<int> edge{0};
    double acc = 0, unit = total / bands, next = unit * (shift ? 0.5 : 1.0);
    for (int r = 0; r < R; ++r) {
        acc += cnt[r];
        if (acc >= next && int(edge.size()) < bands && r + 1 < R && r + 1 - edge.back() >= minRows) {
            edge.push_back(r + 1);
            next += unit;
        }
    }
    if (R - edge.back() < minRows && edge.size() > 1) edge.pop_back();   // no thin last band
    edge.push_back(R);
    return edge;
}
}  // namespace

long optimizeWindowsDisp(const PlaceDB& db, Placement& p, const WindowOptions& opt, WindowMemo* keep) {
    if (db.movable.empty() || opt.passes == 0) return 0;
    auto t0 = std::chrono::steady_clock::now();
    Placement safe = p;
    RowMap rows(db, p);
    const DispStats start = displacement(db, p);
    const double cap = start.max;   // never increases
    WindowMemo local;
    WindowMemo& memo = keep ? *keep : local;
    memo.sync(db, p);
    std::vector<long>& mod = memo.mod;
    auto& seen = memo.seen[opt.rows >= 3 ? 1 : 0];
    std::atomic<long> clock(memo.clock);
    long acceptedAll = 0, solvedAll = 0;
    int quiet = 0;
    double total = start.total;
    // v4: windows never leave a band of rows, so bands are independent and
    // can be swept by several threads at once (each thread touches only the
    // rows, cells and row lists of its band); the band boundaries move by
    // half a band every pass. Same thread count -> same result.
    int R = int(db.rows.size());
    int bands = std::max(1, std::min({opt.threads, R / 8, int(db.movable.size() / 2000)}));
    std::unique_ptr<Pool> pool;
    if (bands > 1) pool.reset(new Pool(bands));
    for (int pass = 0; pass < opt.passes; ++pass) {
        int par = pass % 2;
        std::vector<int> offsets = opt.rows >= 3 ? std::vector<int>{-1, 1} : std::vector<int>{par ? -1 : 1};
        std::vector<int> edge = bands > 1 ? rowBands(db, p, bands, par) : std::vector<int>{0, R};
        int nb = int(edge.size()) - 1;
        std::vector<long> accepted(nb, 0), solved(nb, 0);
        std::vector<double> gain(nb, 0);
        std::vector<char> timeUp(nb, 0);
        auto sweep = [&](int b) {
            int r0 = edge[b], r1 = edge[b + 1];
            std::vector<int> anchors;
            for (int r = r0; r < r1; ++r)
                for (int s : db.rows[r].segs) {
                    const auto& list = rows.cells(s);
                    for (int k = par * opt.perRow / 2; k < int(list.size()); k += opt.perRow) anchors.push_back(list[k]);
                }
            if (par) std::reverse(anchors.begin(), anchors.end());
            for (size_t a = 0; a < anchors.size(); ++a) {
                if ((a & 255) == 0 && seconds(t0) > opt.timeLimit) { timeUp[b] = 1; break; }
                int anchor = anchors[a];
                int r = db.segs[p.seg[anchor]].row;
                bool inside = true;
                for (int off : offsets) inside = inside && (r + off < 0 || r + off >= R || (r + off >= r0 && r + off < r1));
                if (!inside) continue;   // crosses the band edge: the other layout covers it
                Window w;
                if (!buildWindow(db, p, rows, anchor, offsets, opt, w)) continue;
                // stamp: newest change among the window cells and the cells bounding it
                long stamp = 0;
                for (int v : w.cells) stamp = std::max(stamp, mod[v]);
                for (const Region& q : w.q) {
                    const auto& list = rows.cells(q.seg);
                    auto at = std::lower_bound(list.begin(), list.end(), q.lo, [&](int v, int x) { return p.site[v] < x; });
                    if (at != list.begin()) stamp = std::max(stamp, mod[*(at - 1)]);
                    auto end = std::lower_bound(list.begin(), list.end(), q.hi, [&](int v, int x) { return p.site[v] < x; });
                    if (end != list.end()) stamp = std::max(stamp, mod[*end]);
                }
                if (stamp <= seen[par][anchor]) continue;
                ++solved[b];
                double old = 0;
                for (int v : w.cells) old += displacementOf(db, p, v);
                WindowSolution sol = solveDisp(db, w, cap);
                long now = clock.load();
                if (sol.feasible && sol.cost < old - EPS) {
                    now = ++clock;
                    // cells bounding the window: their free room changes too, so the
                    // windows they bound (but whose cells did not move) are dirty
                    for (const Region& q : w.q) {
                        const auto& list = rows.cells(q.seg);
                        auto at = std::lower_bound(list.begin(), list.end(), q.lo, [&](int v, int x) { return p.site[v] < x; });
                        if (at != list.begin()) mod[*(at - 1)] = now;
                        auto end = std::lower_bound(list.begin(), list.end(), q.hi, [&](int v, int x) { return p.site[v] < x; });
                        if (end != list.end()) mod[*end] = now;
                    }
                    applyWindow(rows, w, sol);
                    for (int v : w.cells) mod[v] = now;
                    gain[b] += old - sol.cost;
                    ++accepted[b];
                }
                seen[par][anchor] = now;
            }
        };
        if (pool) pool->run(nb, sweep);
        else sweep(0);
        double before = total;
        long acc = 0, sol = 0;
        bool up = false;
        for (int b = 0; b < nb; ++b) {
            total -= gain[b];
            acc += accepted[b];
            sol += solved[b];
            up = up || timeUp[b];
        }
        acceptedAll += acc;
        solvedAll += sol;
        if (opt.verbose && opt.verbose > 1)
            std::fprintf(stderr, "[window-disp] pass %d: %ld/%ld solved windows improved, total %.1f -> %.1f  %.2fs\n",
                         pass, acc, sol, before, total, seconds(t0));
        // stop after two quiet passes in a row (one per direction / band layout)
        quiet = before - total <= opt.tol * before ? quiet + 1 : 0;
        if (up || quiet >= 2) break;
    }
    memo.clock = clock.load();
    DispStats end = displacement(db, p);
    if (!checkLegal(db, p).empty() || end.total > start.total + 1e-5 || end.max > cap + 1e-6) {
        if (opt.verbose) std::fprintf(stderr, "[window-disp] invariant failed, result discarded\n");
        p = std::move(safe);   // never happens in tests; keep the safe placement rather than abort
        memo = WindowMemo();
        return 0;
    }
    memo.seg = p.seg;
    memo.site = p.site;
    if (opt.verbose)
        std::fprintf(stderr, "[window-disp] %d rows x %d cells: %ld windows solved, %ld improved: total %.1f -> %.1f (max %.4f)  %.2fs\n",
                     opt.rows, opt.perRow, solvedAll, acceptedAll, start.total, end.total, end.max, seconds(t0));
    return acceptedAll;
}

// Cells that moved since the memo last saw the placement (by any other
// algorithm) get a new stamp, so their windows are solved again.
void WindowMemo::sync(const PlaceDB& db, const Placement& p) {
    size_t N = db.nodes.size();
    if (mod.size() != N) {
        mod.assign(N, 0);
        for (auto& s : seen) s = {std::vector<long>(N, -1), std::vector<long>(N, -1)};
        seg = p.seg;
        site = p.site;
        clock = 1;
        return;
    }
    ++clock;
    // moved cells, and their neighbours before and after (whose room changed)
    std::vector<char> segTouched(db.segs.size(), 0);
    bool any = false;
    for (int v : db.movable)
        if (seg[v] != p.seg[v] || site[v] != p.site[v]) {
            mod[v] = clock;
            segTouched[seg[v]] = segTouched[p.seg[v]] = 1;
            any = true;
        }
    if (!any) return;
    for (int pass = 0; pass < 2; ++pass) {   // 0: old placement, 1: current
        const std::vector<int>& S = pass ? p.seg : seg;
        const std::vector<int>& X = pass ? p.site : site;
        std::vector<std::vector<int>> lists(db.segs.size());
        for (int v : db.movable)
            if (segTouched[S[v]]) lists[S[v]].push_back(v);
        for (auto& l : lists) {
            std::sort(l.begin(), l.end(), [&](int a, int b) { return X[a] < X[b]; });
            for (size_t k = 0; k < l.size(); ++k)
                if (mod[l[k]] == clock) {
                    if (k > 0) mod[l[k - 1]] = std::max(mod[l[k - 1]], clock);
                    if (k + 1 < l.size()) mod[l[k + 1]] = std::max(mod[l[k + 1]], clock);
                }
        }
    }
}

long windowSweeps(const PlaceDB& db, Placement& p, const WindowOptions& opt, WindowMemo* memo) {
    auto t0 = std::chrono::steady_clock::now();
    WindowMemo local;
    if (!memo) memo = &local;
    long all = 0;
    for (int round = 0; round < 8; ++round) {
        long got = 0;
        for (int rowsPer : {2, 3}) {
            WindowOptions o = opt;
            o.rows = rowsPer;
            o.perRow = rowsPer == 2 ? opt.perRow : opt.perRow3;
            o.timeLimit = opt.timeLimit - seconds(t0);
            if (o.timeLimit <= 0) return all;
            got += optimizeWindowsDisp(db, p, o, memo);
        }
        all += got;
        if (!got) break;
    }
    return all;
}

long targetedWindowsDisp(const PlaceDB& db, Placement& p, const WindowOptions& opt, int maxRoots) {
    if (db.movable.empty() || opt.passes == 0 || opt.timeLimit <= 0 || maxRoots <= 0) return 0;
    const Placement safe = p;
    const auto before = displacement(db, p);
    const auto start = std::chrono::steady_clock::now();
    RowMap rows(db, p);
    long accepted = 0, solved = 0;
    try {
        for (int pass = 0; pass < std::min(4, opt.passes); ++pass) {
            auto anchors = db.movable;
            std::vector<double> d(db.nodes.size());
            for (int v : anchors) d[v] = displacementOf(db, p, v);
            int count = std::min(int(anchors.size()), maxRoots);
            std::partial_sort(anchors.begin(), anchors.begin() + count, anchors.end(), [&](int a, int b) {
                return d[a] != d[b] ? d[a] > d[b] : a < b;
            });
            anchors.resize(count);
            long previous = accepted;
            for (int anchor : anchors) {
                if (seconds(start) >= opt.timeLimit) break;
                if (displacementOf(db, p, anchor) < EPS) continue;
                const auto& n = db.nodes[anchor];
                int targetRow = db.nearestRow(n.gy);
                int destination = nearestSeg(db, targetRow, n.gx + n.w / 2);
                if (destination < 0) continue;
                Window w;
                const int perRegion = std::min(4, opt.perRow);
                auto makeRegion = [&](int s, double x) {
                    const auto& sg = db.segs[s];
                    x = std::clamp(x, sg.x0, sg.x0 + sg.numSites * sg.sp);
                    return region(db, p, rows, s, x, perRegion, opt.maxSites);
                };
                Region a = makeRegion(p.seg[anchor], p.x(db, anchor) + n.w / 2);
                Region b = makeRegion(destination, n.gx + n.w / 2);
                if (a.seg == b.seg && a.lo <= b.hi && b.lo <= a.hi) {
                    // Overlapping intervals are one physical region; a cell
                    // must never appear twice in the assignment problem.
                    a.lo = std::min(a.lo, b.lo); a.hi = std::max(a.hi, b.hi);
                    a.cells.insert(a.cells.end(), b.cells.begin(), b.cells.end());
                    std::sort(a.cells.begin(), a.cells.end(), [&](int x, int y) { return p.site[x] < p.site[y]; });
                    a.cells.erase(std::unique(a.cells.begin(), a.cells.end()), a.cells.end());
                    w.q = {a};
                } else w.q = {a, b};
                bool valid = true;
                for (const auto& q : w.q) {
                    if (q.hi <= q.lo || q.hi - q.lo > opt.maxSites) valid = false;
                    w.cells.insert(w.cells.end(), q.cells.begin(), q.cells.end());
                }
                if (!valid || w.cells.empty() || w.cells.size() > 10) continue;
                double old = 0;
                for (int v : w.cells) old += displacementOf(db, p, v);
                ++solved;
                auto sol = solveDisp(db, w, before.max);
                if (sol.feasible && sol.cost < old - EPS) {
                    applyWindow(rows, w, sol);
                    ++accepted;
                }
            }
            if (accepted == previous || seconds(start) >= opt.timeLimit) break;
        }
        const auto after = displacement(db, p);
        if (!checkLegal(db, p).empty() || after.total > before.total + 1e-5 || after.max > before.max + 1e-6)
            throw std::runtime_error("target window invariant failed");
        if (opt.verbose)
            std::fprintf(stderr, "[target-window] %ld/%ld improved: total %.6f -> %.6f, max %.6f -> %.6f  %.2fs\n",
                         accepted, solved, before.total, after.total, before.max, after.max, seconds(start));
        return accepted;
    } catch (...) {
        p = safe;
        throw;
    }
}

// v4: maximum displacement. The worst cell c and its neighbours in c's row and
// the row above / below form a window; the DP is solved with a cap below c's
// displacement, bisected down to the lowest cap that is still feasible (never
// below the largest displacement outside the window, which would not lower the
// maximum anyway). The window's total may rise a little: this is the step that
// trades total for maximum. Repeats while the worst cell can be helped.
long reduceMaxWindows(const PlaceDB& db, Placement& p, const WindowOptions& opt) {
    if (db.movable.empty()) return 0;
    auto t0 = std::chrono::steady_clock::now();
    Placement safe = p;
    RowMap rows(db, p);
    const DispStats start = displacement(db, p);
    std::vector<double> d(db.nodes.size(), 0);
    for (int v : db.movable) d[v] = displacementOf(db, p, v);
    const double step = db.segs.empty() ? 1 : db.segs[0].sp * 0.5;
    long moves = 0;
    std::vector<char> in(db.nodes.size(), 0);
    const std::vector<std::vector<int>> shapes = {{1}, {-1}, {-1, 1}};
    for (int iter = 0; iter < opt.passes && seconds(t0) < opt.timeLimit; ++iter) {
        int c = db.movable[0];
        for (int v : db.movable)
            if (d[v] > d[c]) c = v;
        double D = d[c];
        Window best;
        WindowSolution bestSol;
        double bestCap = D - step;
        for (const auto& offsets : shapes) {
            Window w;
            WindowOptions o = opt;
            o.perRow = offsets.size() > 1 ? 3 : opt.perRow;
            if (!buildWindow(db, p, rows, c, offsets, o, w)) continue;
            for (int v : w.cells) in[v] = 1;
            double D2 = 0;   // largest displacement outside the window
            for (int v : db.movable)
                if (!in[v]) D2 = std::max(D2, d[v]);
            for (int v : w.cells) in[v] = 0;
            double hi = bestCap, lo = D2;
            if (hi <= lo) continue;
            WindowSolution sol = solveDisp(db, w, hi);
            if (!sol.feasible) continue;
            for (int k = 0; k < 12 && hi - lo > step; ++k) {   // lowest feasible cap
                double mid = (lo + hi) / 2;
                WindowSolution t = solveDisp(db, w, mid);
                if (t.feasible) { hi = mid; sol = std::move(t); }
                else lo = mid;
            }
            bestCap = hi;
            best = w;
            bestSol = std::move(sol);
        }
        if (!bestSol.feasible) break;
        applyWindow(rows, best, bestSol);
        for (int v : best.cells) d[v] = displacementOf(db, p, v);
        ++moves;
    }
    DispStats end = displacement(db, p);
    if (!checkLegal(db, p).empty() || end.max > start.max + 1e-6) {
        if (opt.verbose) std::fprintf(stderr, "[window-max] invariant failed, result discarded\n");
        p = std::move(safe);
        return 0;
    }
    if (opt.verbose && moves)
        std::fprintf(stderr, "[window-max] %ld moves: max %.4f -> %.4f, total %.1f -> %.1f  %.2fs\n", moves, start.max,
                     end.max, start.total, end.total, seconds(t0));
    return moves;
}

// HPWL mode (CodexWindowDP): separable frozen-pin costs propose a window
// placement; it is accepted ONLY if the real HPWL of the affected nets drops.
void optimizeWindowsHPWL(const PlaceDB& db, Placement& p, const WindowOptions& opt) {
    if (db.movable.empty() || opt.passes == 0) return;
    auto t0 = std::chrono::steady_clock::now();
    Placement safe = p;
    RowMap rows(db, p);
    double start = totalHPWL(db, p);
    for (int pass = 0; pass < opt.passes; ++pass) {
        double beforeH = totalHPWL(db, p);
        long tried = 0, accepted = 0;
        std::vector<int> anchors;
        for (size_t s = 0; s < db.segs.size(); ++s) {
            const auto& list = rows.cells(int(s));
            for (int k = (pass % 2) * opt.perRow / 2; k < int(list.size()); k += opt.perRow) anchors.push_back(list[k]);
        }
        if (pass % 2) std::reverse(anchors.begin(), anchors.end());
        for (size_t a = 0; a < anchors.size(); ++a) {
            if ((a & 255) == 0 && seconds(t0) > opt.timeLimit) break;
            Window w;
            if (!buildWindow(db, p, rows, anchors[a], {pass % 2 ? -1 : 1}, opt, w) || w.q.size() != 2) continue;
            int n = int(w.cells.size());
            WindowProblem problem;
            problem.cells = n;
            std::vector<int> nets;
            std::vector<std::vector<Box>> boxes(n);
            for (int i = 0; i < n; ++i) {
                int v = w.cells[i];
                for (int net : db.nodeNets[v]) {
                    nets.push_back(net);
                    if (db.nets[net].size() < 2) continue;
                    Box box;
                    for (int u : db.nets[net])
                        if (u != v) extend(box, p.x(db, u) + db.nodes[u].w / 2, p.y(db, u) + db.nodes[u].h / 2);
                    boxes[i].push_back(box);
                }
            }
            for (int j = 0; j < 2; ++j) {
                const auto& sg = db.segs[w.q[j].seg];
                int L = w.q[j].hi - w.q[j].lo;
                problem.length[j] = L;
                problem.width[j].resize(n);
                problem.cost[j].resize(n);
                for (int i = 0; i < n; ++i) {
                    int v = w.cells[i], wv = db.sitesOf(v, sg);
                    problem.width[j][i] = wv;
                    auto& costs = problem.cost[j][i];
                    costs.assign(L + 1, INF);
                    if (std::fabs(db.nodes[v].h - sg.height) > EPS) continue;
                    for (int t = 0; t + wv <= L; ++t) {
                        double cx = sg.x0 + (w.q[j].lo + t) * sg.sp + db.nodes[v].w / 2, cy = sg.y + db.nodes[v].h / 2;
                        double value = 0;
                        for (const Box& b : boxes[i])
                            value += std::max(b.hx, cx) - std::min(b.lx, cx) + std::max(b.hy, cy) - std::min(b.ly, cy);
                        costs[t] = value;
                    }
                }
            }
            ++tried;
            WindowSolution sol = solveWindow(problem);
            if (!sol.feasible) continue;
            std::sort(nets.begin(), nets.end());
            nets.erase(std::unique(nets.begin(), nets.end()), nets.end());
            double oldH = wire(db, p, nets);
            std::vector<int> oldS, oldX;
            for (int i = 0; i < n; ++i) {
                int v = w.cells[i];
                oldS.push_back(p.seg[v]);
                oldX.push_back(p.site[v]);
                p.seg[v] = w.q[sol.row[i]].seg;
                p.site[v] = w.q[sol.row[i]].lo + sol.site[i];
            }
            bool improve = wire(db, p, nets) < oldH - EPS;
            for (int i = 0; i < n; ++i) { p.seg[w.cells[i]] = oldS[i]; p.site[w.cells[i]] = oldX[i]; }
            if (!improve) continue;
            applyWindow(rows, w, sol);
            ++accepted;
        }
        double afterH = totalHPWL(db, p);
        if (opt.verbose)
            std::fprintf(stderr, "[window-hpwl] pass %d: %ld/%ld accepted, %.6f -> %.6f\n", pass, accepted, tried, beforeH, afterH);
        if (!accepted) break;
    }
    if (!checkLegal(db, p).empty() || totalHPWL(db, p) > start + 1e-5) {
        if (opt.verbose) std::fprintf(stderr, "[window-hpwl] invariant failed, result discarded\n");
        p = std::move(safe);
    }
}
