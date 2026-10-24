#include "global.hpp"
#include "placedb.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <map>
#include <cmath>
#include <cstdlib>
// usage: driver BENCH PL CAP [CAP...]  (budget per cap = 3.2 s, like ChainDP's default)
bool readPl(const PlaceDB& db, const std::string& path, Placement& p) {
    std::map<std::string,int> id; for (size_t i=0;i<db.nodes.size();++i) id[db.nodes[i].name]=int(i);
    p.seg.assign(db.nodes.size(),-1); p.site.assign(db.nodes.size(),0);
    std::ifstream f(path); std::string line;
    while (std::getline(f,line)) {
        std::istringstream ss(line); std::string n; double x,y; if(!(ss>>n>>x>>y)) continue;
        auto it=id.find(n); if(it==id.end()) continue; int v=it->second; if(db.nodes[v].fixed) continue;
        bool ok=false;
        for (size_t s=0;s<db.segs.size();++s){const auto& g=db.segs[s]; if(std::abs(g.y-y)>1e-6) continue;
            double k=(x-g.x0)/g.sp; int ki=(int)std::lround(k); if(std::abs(k-ki)>1e-6) continue;
            if(ki<0||ki+db.sitesOf(v,g)>g.numSites) continue; p.seg[v]=int(s); p.site[v]=ki; ok=true; break;}
        if(!ok){fprintf(stderr,"cannot map %s\n",n.c_str());return false;}
    }
    return true;
}
int main(int argc,char**argv){
    PlaceDB db; std::string err; if(!db.read(argv[1],err)){fprintf(stderr,"%s\n",err.c_str());return 1;}
    Placement p; if(!readPl(db,argv[2],p)) return 1;
    auto d=displacement(db,p); fprintf(stderr,"incumbent total %.4f max %.4f legal:%s\n",d.total,d.max,checkLegal(db,p).c_str());
    double budget = argc>4 && std::string(argv[argc-2])=="--budget" ? atof(argv[argc-1]) : 3.2;
    for(int i=3;i<argc;++i){ if(std::string(argv[i])=="--budget"){break;}
        double cap=atof(argv[i]); Placement q=p; double t=wallSeconds();
        bool ok=repairCap(db,q,cap,wallSeconds()+budget,12345);
        auto e=displacement(db,q);
        fprintf(stderr,"cap %.4f: %s in %.3fs, total %.4f max %.4f\n",cap,ok?"FEASIBLE":"unresolved",wallSeconds()-t,e.total,e.max);
    }
}
