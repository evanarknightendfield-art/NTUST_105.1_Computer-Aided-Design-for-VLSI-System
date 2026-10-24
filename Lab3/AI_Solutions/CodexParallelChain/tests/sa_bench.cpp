// A common Stage-2 driver compiled separately against each unmodified source
// tree. Reads a saved legal placement, reports actual SA work, and checks the
// result using that version's own validator; Python also runs check_pl.py.
#include "annealer.hpp"
#include "optimizer.hpp"
#include "window.hpp"
#include <cstdio>
#include <stdexcept>
#include <fstream>

int main(int argc,char** argv) {
    try {
        if(argc!=8 && argc!=9)throw std::runtime_error("sa_bench original legal_prefix out seed moves seconds polish_seconds [threads]");
        PlaceDB db,loc;std::string err;
        if(!db.read(argv[1],err)||!loc.read(argv[2],err))throw std::runtime_error(err);
        Placement p;p.seg.assign(db.nodes.size(),-1);p.site.assign(db.nodes.size(),0);
        for(int v:db.movable)for(size_t s=0;s<db.segs.size();++s) {
            const auto& sg=db.segs[s];double x=(loc.nodes[v].gx-sg.x0)/sg.sp;
            if(std::abs(loc.nodes[v].gy-sg.y)<1e-6 && x>=-1e-6 && x+db.sitesOf(v,sg)<=sg.numSites+1e-6 && std::abs(x-std::round(x))<1e-6) {
                p.seg[v]=int(s);p.site[v]=int(std::round(x));break;
            }
        }
        if(!checkLegal(db,p).empty())throw std::runtime_error(checkLegal(db,p));
        const Placement legal=p;
        SAOptions opt;opt.seed=std::stoull(argv[4]);
        if(std::string(argv[5])!="default") {
            opt.minMoves=opt.maxMoves=std::stod(argv[5]);opt.movesPerCell=0;
        }
        opt.timeLimit=std::stod(argv[6]);opt.threads=argc==9?std::stoi(argv[8]):1;
        opt.tracePath=std::string(argv[3])+".trace.csv";
        double start=wallSeconds();auto stats=anneal(db,p,opt);
        OptimizeOptions polish;polish.until=wallSeconds()+std::stod(argv[7]);
        if(std::stod(argv[7])>0) {
            polishHPWL(db,p,polish);
            WindowOptions w;w.timeLimit=std::max(0.0,polish.until-wallSeconds());
            optimizeWindowsHPWL(db,p,w);reorderWindows(db,p,2,false,polish.until);
        }
        if(!checkLegal(db,p).empty() || totalHPWL(db,p)>totalHPWL(db,legal)+1e-5)throw std::runtime_error("Stage 2 invariant");
        writePl(db,p,std::string(argv[3])+".pl");
        std::printf("hpwl,attempts,sa_seconds,stage2_seconds\n%.12f,%lld,%.6f,%.6f\n",totalHPWL(db,p),stats.attempts,stats.seconds,wallSeconds()-start);
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
