#include "db.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>

namespace {
constexpr double INF=std::numeric_limits<double>::infinity();
using Clock=std::chrono::steady_clock;
double seconds(Clock::time_point start) { return std::chrono::duration<double>(Clock::now()-start).count(); }
struct Options {
    unsigned long long seed=1;
    long long effort=300;
    double time=30;
    bool noSA=false, noRefine=false, verbose=false;
    std::string trace;
};

// The maps contain non-overlapping, half-open integer site reservations.
class Layout {
public:
    const DB& db;
    std::vector<Slot> p;
    std::vector<std::map<int,int>> rows;
    explicit Layout(const DB& database):db(database),p(db.cells.size()),rows(db.segments.size()) {}
    void erase(int v) { rows[p[v].seg].erase(p[v].site); }
    void insert(int v,Slot q) {
        if(!rows[q.seg].emplace(q.site,v).second) throw std::runtime_error("internal duplicate reservation");
        p[v]=q;
    }
    void rebuild() {
        for(auto& row:rows) row.clear();
        for(int v:db.movable) insert(v,p[v]);
    }
    int occupant(int s,int site) const {
        auto it=rows[s].upper_bound(site);
        if(it==rows[s].begin()) return -1;
        --it; return it->first+db.width(it->second,s)>site ? it->second : -1;
    }
    int gap(int v,int s,double x) const {
        const auto& seg=db.segments[s]; int w=db.width(v,s);
        if(std::abs(db.cells[v].h-seg.h)>1e-6 || w>seg.capacity) return -1;
        double desired=(x-seg.x)/seg.step, best=INF;
        int chosen=-1, lo=0;
        auto consider=[&](int hi) {
            if(hi-lo<w) return;
            // Clamp before rounding, so extremely far-away input coordinates cannot overflow.
            int site=int(std::llround(std::clamp(desired,double(lo),double(hi-w))));
            double cost=std::abs(site-desired);
            if(cost<best) { best=cost; chosen=site; }
        };
        for(auto [site,id]:rows[s]) {
            consider(site); lo=site+db.width(id,s);
            if(site>desired+best) break;
        }
        // If the loop stopped early, the tail is NOT empty. Only test a true tail.
        if(rows[s].empty() || lo==rows[s].rbegin()->first+db.width(rows[s].rbegin()->second,s)) consider(seg.capacity);
        return chosen;
    }
    Slot nearest(int v,double x,double y,double limit=INF) const {
        Slot result;
        int up=int(std::lower_bound(db.segments.begin(),db.segments.end(),y,
            [](const Segment& s,double value){ return s.y<value; })-db.segments.begin());
        int down=up-1;
        double best=limit;
        while(up<int(rows.size()) || down>=0) {
            double a=up<int(rows.size()) ? std::abs(db.segments[up].y-y) : INF;
            double b=down>=0 ? std::abs(db.segments[down].y-y) : INF;
            if(std::min(a,b)>=best) break;
            int s=a<=b ? up++ : down--;
            const auto& seg=db.segments[s];
            double dx=std::max({0.0,seg.x-x,x-(seg.x+seg.capacity*seg.step-db.cells[v].w)});
            if(dx+std::abs(seg.y-y)>=best) continue;
            int t=gap(v,s,x);
            if(t<0) continue;
            double cost=std::abs(seg.x+t*seg.step-x)+std::abs(seg.y-y);
            if(cost<best) { best=cost; result={s,t}; }
        }
        return result;
    }
    int targetSegment(int v,double x,double y) const {
        // Geometry-only target; occupied sites are handled by move/swap proposals.
        int result=-1; double best=INF;
        for(int s=0;s<int(rows.size());++s) {
            const auto& r=db.segments[s];
            if(std::abs(r.h-db.cells[v].h)>1e-6 || db.width(v,s)>r.capacity) continue;
            double cost=std::abs(r.y-y)+std::max({0.0,r.x-x,x-(r.x+(r.capacity-db.width(v,s))*r.step)});
            if(cost<best) {best=cost;result=s;}
        }
        return result;
    }
};

struct Change {
    int a=-1,b=-1;
    Slot oldA,oldB;
    bool valid=false;
};
void undo(Layout& l,const Change& c) {
    l.erase(c.a); if(c.b>=0) l.erase(c.b);
    l.insert(c.a,c.oldA); if(c.b>=0) l.insert(c.b,c.oldB);
}
Change relocate(Layout& l,int a,int s,double x) {
    Change c; c.a=a;c.oldA=l.p[a];
    l.erase(a); int t=l.gap(a,s,x);
    if(t<0) {l.insert(a,c.oldA);return c;}
    l.insert(a,{s,t}); c.valid=s!=c.oldA.seg || t!=c.oldA.site;
    return c;
}
Change exchange(Layout& l,int a,int b) {
    Change c; c.a=a;c.b=b;c.oldA=l.p[a];c.oldB=l.p[b];
    if(a==b) return c;
    const auto& db=l.db;
    // Adjacent cells can exchange unequal widths by reversing their order in
    // the same outer envelope; all pre-existing whitespace remains inside it.
    if(c.oldA.seg==c.oldB.seg) {
        int s=c.oldA.seg; auto ia=l.rows[s].find(c.oldA.site),ib=l.rows[s].find(c.oldB.site);
        bool adjacent=std::next(ia)==ib || std::next(ib)==ia;
        if(adjacent) {
            int lo=std::min(c.oldA.site,c.oldB.site);
            int hi=std::max(c.oldA.site+db.width(a,s),c.oldB.site+db.width(b,s));
            l.erase(a);l.erase(b);
            if(c.oldA.site<c.oldB.site) {l.insert(b,{s,lo});l.insert(a,{s,hi-db.width(a,s)});}
            else {l.insert(a,{s,lo});l.insert(b,{s,hi-db.width(b,s)});}
            c.valid=true;return c;
        }
    }
    l.erase(a);l.erase(b);
    double ax=db.x(b,c.oldB)+(db.cells[b].w-db.cells[a].w)/2;
    double bx=db.x(a,c.oldA)+(db.cells[a].w-db.cells[b].w)/2;
    int at=l.gap(a,c.oldB.seg,ax);
    if(at<0) {l.insert(a,c.oldA);l.insert(b,c.oldB);return c;}
    l.insert(a,{c.oldB.seg,at});
    int bt=l.gap(b,c.oldA.seg,bx);
    if(bt<0) {l.erase(a);l.insert(a,c.oldA);l.insert(b,c.oldB);return c;}
    l.insert(b,{c.oldA.seg,bt});c.valid=true;return c;
}

void legalize(Layout& l,const Options& opt) {
    const auto& db=l.db; auto order=db.movable;
    // Allocate scarce wide slots first; small cells subsequently fill fragments.
    std::sort(order.begin(),order.end(),[&](int a,int b){
        if(db.cells[a].w!=db.cells[b].w) return db.cells[a].w>db.cells[b].w;
        if(db.cells[a].gx!=db.cells[b].gx) return db.cells[a].gx<db.cells[b].gx;
        return a<b;
    });
    for(int v:order) {
        Slot q=l.nearest(v,db.cells[v].gx,db.cells[v].gy);
        if(q.seg<0) throw std::runtime_error("greedy legalization cannot fit " + db.cells[v].name);
        l.insert(v,q);
    }
    if(opt.noRefine) return;
    std::mt19937_64 rng(20261018); // Stage 1 is independent of the SA seed.
    for(int pass=0;pass<3;++pass) {
        std::sort(order.begin(),order.end(),[&](int a,int b){
            double da=db.displacement(a,l.p[a]),dbb=db.displacement(b,l.p[b]);
            return da!=dbb ? da>dbb : a<b;
        });
        long long improvements=0;
        for(int v:order) {
            Slot old=l.p[v];double d=db.displacement(v,old);
            l.erase(v);
            Slot q=l.nearest(v,db.cells[v].gx,db.cells[v].gy,d-1e-8);
            l.insert(v,q.seg<0 ? old : q);improvements+=q.seg>=0;
            int s=l.targetSegment(v,db.cells[v].gx,db.cells[v].gy);
            if(s<0) continue;
            const auto& seg=db.segments[s];
            int target=int(std::llround(std::clamp((db.cells[v].gx-seg.x)/seg.step,0.0,double(seg.capacity-1))));
            for(int trial=0;trial<12;++trial) {
                int t=std::clamp(target+int(rng()%65)-32,0,seg.capacity-1);
                int b=l.occupant(s,t); if(b<0 || b==v) continue;
                double a0=db.displacement(v,l.p[v]),b0=db.displacement(b,l.p[b]);
                Change c=exchange(l,v,b); if(!c.valid) continue;
                double a1=db.displacement(v,l.p[v]),b1=db.displacement(b,l.p[b]);
                // Pareto acceptance: both total and maximum displacement cannot increase.
                bool accept=a1+b1<=a0+b0+1e-8 && std::max(a1,b1)<=std::max(a0,b0)+1e-8 &&
                    (a1+b1<a0+b0-1e-8 || std::max(a1,b1)<std::max(a0,b0)-1e-8);
                if(accept) ++improvements; else undo(l,c);
            }
        }
        if(opt.verbose) std::cerr<<"[refine] pass "<<pass+1<<", improvements "<<improvements<<'\n';
        if(!improvements) break;
    }
}

class Annealer {
    Layout& l;
    const DB& db;
    const Options& opt;
    std::mt19937_64 rng;
    double unit() {return std::generate_canonical<double,53>(rng);}
    int pick(int n) {return int(rng()%static_cast<unsigned>(n));}
    Change propose(double progress) {
        int a=db.movable[pick(int(db.movable.size()))];Slot old=l.p[a];
        double x=db.x(a,old),y=db.y(a,old);
        const auto& s=db.segments[old.seg];
        double window=std::max(2.0,24.0*std::pow(1-progress,2));
        int mode=pick(10);
        if(mode<2 && l.rows[old.seg].size()>1) {
            auto it=l.rows[old.seg].find(old.site);
            if((rng()&1) && std::next(it)!=l.rows[old.seg].end()) return exchange(l,a,std::next(it)->second);
            if(it!=l.rows[old.seg].begin()) return exchange(l,a,std::prev(it)->second);
        }
        if(mode<5 && !db.incident[a].empty()) {
            const auto& net=db.nets[db.incident[a][pick(int(db.incident[a].size()))]];
            int b=net[pick(int(net.size()))];
            // Sample a connected cell, then interpolate toward its center.
            // This is deliberately a cheap stochastic target, not an optimal-region solver.
            double weight=0.25+0.65*unit();
            x+=weight*(db.x(b,l.p[b])+(db.cells[b].w-db.cells[a].w)/2-x);
            y+=weight*(db.y(b,l.p[b])-y);
        } else {
            x+=(2*unit()-1)*window*s.h;
            y+=(2*unit()-1)*window*s.h/3;
        }
        int target=l.targetSegment(a,x,y);
        if(target<0) return {};
        const auto& t=db.segments[target];
        int site=int(std::llround(std::clamp((x-t.x)/t.step,0.0,double(t.capacity-1))));
        int b=l.occupant(target,site);
        if(b>=0 && b!=a && mode!=9) return exchange(l,a,b);
        return relocate(l,a,target,x);
    }
    double delta(const Change& c) {
        auto affected=db.incident[c.a];
        if(c.b>=0) affected.insert(affected.end(),db.incident[c.b].begin(),db.incident[c.b].end());
        std::sort(affected.begin(),affected.end());
        affected.erase(std::unique(affected.begin(),affected.end()),affected.end());
        double after=0,before=0;
        for(int n:affected) after+=db.netHPWL(n,l.p);
        Slot newA=l.p[c.a],newB=c.b>=0 ? l.p[c.b] : Slot{};
        l.p[c.a]=c.oldA;if(c.b>=0) l.p[c.b]=c.oldB;
        for(int n:affected) before+=db.netHPWL(n,l.p);
        l.p[c.a]=newA;if(c.b>=0) l.p[c.b]=newB;
        return after-before;
    }
public:
    Annealer(Layout& layout,const Options& options):l(layout),db(l.db),opt(options),rng(opt.seed) {}
    void run() {
        if(db.movable.empty() || opt.time<=0 || opt.effort==0) return;
        auto start=Clock::now();
        double cur=db.hpwl(l.p),best=cur;
        auto bestPlacement=l.p;
        std::vector<int> dirty;
        std::vector<bool> marked(l.p.size());
        double uphill=0;int samples=0;
        for(int i=0;i<256 && seconds(start)<opt.time/10;++i) {
            Change c=propose(0);
            if(c.valid) {double d=delta(c);if(d>0){uphill+=d;++samples;}undo(l,c);}
        }
        double initialT=samples ? -uphill/samples/std::log(0.65) : 1;
        std::ofstream trace;
        if(!opt.trace.empty()) {
            std::filesystem::path path(opt.trace);
            if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
            trace.open(path);if(!trace) throw std::runtime_error("cannot write SA trace");
            trace<<std::setprecision(15)<<"iteration,current,best,temperature,accept_rate\n";
            trace<<0<<','<<cur<<','<<best<<','<<initialT<<",0\n";
        }
        long long budget=std::max(1000LL,opt.effort*static_cast<long long>(db.movable.size()));
        long long attempts=0,accepted=0,blockAccept=0,blockAttempt=0;
        double progress=0,T=initialT;
        bool restarted=false;
        for(;attempts<budget;++attempts) {
            if(attempts%128==0) {
                double elapsed=seconds(start);
                if(elapsed>=opt.time) break;
                progress=std::max(double(attempts)/budget,elapsed/opt.time);
                // A bounded run may not recover from its hot exploratory phase.
                // Spend the final 40% refining the best state actually observed.
                if(!restarted && progress>=0.6) {
                    l.p=bestPlacement;l.rebuild();cur=best;
                    for(int v:dirty) marked[v]=false;
                    dirty.clear();restarted=true;
                    if(opt.verbose) std::cerr<<"[SA] restart from best at attempt "<<attempts<<'\n';
                }
                // Last 10% is a zero-temperature descent after geometric cooling.
                T=progress<0.9 ? initialT*std::pow(1e-4,progress/0.9) : 0;
            }
            Change c=propose(progress);++blockAttempt;
            if(c.valid) {
                double d=delta(c);
                if(d<=0 || (T>0 && unit()<std::exp(-d/T))) {
                    cur+=d;++accepted;++blockAccept;
                    for(int v:{c.a,c.b}) if(v>=0 && !marked[v]) {marked[v]=true;dirty.push_back(v);}
                    if(cur<best-1e-7) {
                        best=cur;
                        for(int v:dirty) {bestPlacement[v]=l.p[v];marked[v]=false;}
                        dirty.clear();
                    }
                } else undo(l,c);
            }
            if((attempts+1)%std::max(1LL,budget/500)==0) {
                if(trace.is_open()) trace<<attempts+1<<','<<cur<<','<<best<<','<<T<<','<<double(blockAccept)/blockAttempt<<'\n';
                blockAttempt=blockAccept=0;
            }
        }
        if(trace.is_open()) {
            trace<<attempts<<','<<cur<<','<<best<<','<<T<<','<<(blockAttempt?double(blockAccept)/blockAttempt:0)<<'\n';
            trace.close();if(!trace) throw std::runtime_error("failed writing trace");
        }
        l.p=std::move(bestPlacement);l.rebuild();
        if(opt.verbose) std::cerr<<"[SA] attempts "<<attempts<<", accepted "<<accepted<<", seconds "<<seconds(start)<<'\n';
    }
};

void usage() {
    std::cerr<<"usage: dp INPUT_FILE OUTPUT_FILE [--seed N] [--time SEC] [--effort N]\n"
        "          [--trace FILE.csv] [--no-sa] [--no-refine] [-v]\n"
        "defaults: seed=1, SA time=30 seconds, effort=300 attempts/cell\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string(argv[1])=="--help") {usage();return 0;}
        if(argc<3) {usage();return 1;}
        Options opt;
        for(int i=3;i<argc;++i) {
            std::string arg=argv[i];
            auto next=[&]() {if(++i>=argc) throw std::runtime_error("missing value for "+arg);return std::string(argv[i]);};
            auto numeric=[&]() {auto s=next();size_t k;double n=std::stod(s,&k);if(k!=s.size() || !std::isfinite(n) || n<0) throw std::runtime_error("invalid value for "+arg);return n;};
            if(arg=="--time") opt.time=numeric();
            else if(arg=="--seed" || arg=="--effort") {
                double n=numeric();if(n>1000000000 || n!=std::floor(n)) throw std::runtime_error("invalid integer for "+arg);
                if(arg=="--seed") opt.seed=static_cast<unsigned long long>(n);else opt.effort=static_cast<long long>(n);
            } else if(arg=="--trace") opt.trace=next();
            else if(arg=="--no-sa") opt.noSA=true;
            else if(arg=="--no-refine") opt.noRefine=true;
            else if(arg=="-v") opt.verbose=true;
            else throw std::runtime_error("unknown option "+arg);
        }
        auto start=Clock::now();DB db;db.read(argv[1]);
        if(!opt.trace.empty()) {
            auto trace=std::filesystem::weakly_canonical(opt.trace);
            for(const auto& file:db.files) if(trace==std::filesystem::weakly_canonical(file.second)) throw std::runtime_error("trace aliases input file");
            for(const auto* ext:{".aux",".nodes",".nets",".wts",".scl",".pl","_legal.pl"})
                if(trace==std::filesystem::weakly_canonical(std::string(argv[2])+ext)) throw std::runtime_error("trace aliases placement output");
        }
        if(opt.verbose) std::cerr<<"[read] "<<db.movable.size()<<" movable cells, "<<db.nets.size()<<" nets, "<<db.segments.size()<<" segments\n";
        Layout layout(db);legalize(layout,opt);db.validate(layout.p);
        auto legal=layout.p;double total=0,maximum=0,legalHPWL=db.hpwl(legal);
        for(int v:db.movable) {double d=db.displacement(v,legal[v]);total+=d;maximum=std::max(maximum,d);}
        if(opt.verbose) std::cerr<<"[legal] seconds "<<seconds(start)<<'\n';
        if(!opt.noSA) Annealer(layout,opt).run();
        db.validate(layout.p);
        if(db.hpwl(layout.p)>legalHPWL+1e-7) layout.p=legal;
        db.write(argv[2],legal,layout.p);
        std::cout<<std::fixed<<std::setprecision(6)
            <<"Total displacement (legal): "<<total<<'\n'
            <<"Maximum displacement (legal): "<<maximum<<'\n'
            <<"Total HPWL (legal): "<<legalHPWL<<'\n'
            <<"Total HPWL (SA): "<<db.hpwl(layout.p)<<'\n';
        if(opt.verbose) std::cerr<<"[done] seconds "<<seconds(start)<<'\n';
        return 0;
    } catch(const std::exception& e) {std::cerr<<"error: "<<e.what()<<'\n';return 1;}
}
