#include "diagnostic.hpp"
#include "global.hpp"
#include <cstdio>
bool readPlX(const PlaceDB&, const std::string&, Placement&);
int main(int argc, char** argv) {
    if (argc < 4) return 1;
    PlaceDB db; Placement base; std::string err;
    if (!db.read(argv[1], err) || !readPlX(db, argv[2], base)) return 2;
    auto memo = diag::loadMemo(std::string(argv[2]).substr(0, std::string(argv[2]).size()-3)+".memo");
    auto b = displacement(db, base);
    std::printf("fraction,ok,repair_s,dp_s,raw,total,max,legal\n");
    for (double frac : {.01,.02,.04}) {
        auto q = base; auto mm = memo; double t = wallSeconds();
        bool ok = repairCap(db, q, b.max*(1-frac), t+1, 7);
        double r = wallSeconds()-t, raw = displacement(db,q).total; t=wallSeconds();
        if (ok) { WindowOptions w; w.passes=1000; w.timeLimit=2; w.threads=8; windowSweeps(db,q,w,&mm); }
        auto d=displacement(db,q);
        std::printf("%.4f,%d,%.6f,%.6f,%.9f,%.9f,%.9f,%d\n",frac,ok,r,wallSeconds()-t,raw,d.total,d.max,checkLegal(db,q).empty());
        std::fflush(stdout);
        if(ok)writePl(db,q,std::string(argv[3])+std::to_string(frac)+".pl");
    }
}
