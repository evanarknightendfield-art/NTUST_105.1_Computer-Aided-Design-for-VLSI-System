#include "cap_chain_prototype.hpp"
#include "global_preserve_prototype.hpp"
#include "diagnostic.hpp"
#include "../src/capsearch.hpp"
#include <cstdio>
#include <stdexcept>
bool readPlX(const PlaceDB&, const std::string&, Placement&);
int main(int argc, char** argv) {
    if (argc < 4) return 1;
    PlaceDB db; Placement base; std::string err;
    if (!db.read(argv[1], err) || !readPlX(db, argv[2], base) || !checkLegal(db, base).empty()) return 2;
    auto b = displacement(db, base);
    std::printf("fraction,ok,seconds,solved,applied,remaining,moved,base_total,total,base_max,max\n");
    for (double fraction : {.005, .01, .02, .04}) {
        auto p = base;
        WindowOptions o; o.passes = 3; o.timeLimit = 3;
        if (db.movable.size() <= 2000) { o.perRow = 5; o.perRow3 = 4; }
        CapWindowResult r;
        Placement partial = base;
        bool ok = false;
        if (argc <= 4 || std::string(argv[4]) == "hybrid") ok = repairCapWindows(db, p, b.max * (1 - fraction), o, &r, &partial);
        if (argc > 4) {
            if (std::string(argv[4]) == "chain") {
                partial = base; repairCapOrderedRowsPrototype(db, partial, b.max * (1-fraction));
                ChainOptions co; co.seconds = .6; co.passes = 2; co.roots = int(db.movable.size());
                auto ct = wallSeconds(); ok = repairCapChainsPrototype(db, partial, b.max * (1-fraction), co);
                r.seconds = wallSeconds() - ct; if (ok) p = partial;
            }
            if (std::string(argv[4]) == "site") {
                auto ct = wallSeconds(); partial = base;
                ok = repairCapPreservingPrototype(db, partial, b.max*(1-fraction), ct+.6, 1, true);
                r.seconds = wallSeconds()-ct; if (ok) p = partial;
            }
            double t0 = wallSeconds();
            if (!ok) { RepairOptions ro; ro.phase1Until = t0 + .3; ro.phase2Until = ro.phase1Until; ro.phase2PerCell = 0;
                ok = rowRepair(db, partial, b.max * (1 - fraction), ro); if (ok) p = partial; }
            if (ok) { WindowOptions w; w.threads = 8; w.passes = 1000; w.timeLimit = 1; auto memo = diag::loadMemo(std::string(argv[2]).substr(0, std::string(argv[2]).size()-3)+".memo"); windowSweeps(db, p, w, &memo); }
            r.seconds += wallSeconds() - t0;
        }
        auto d = displacement(db, p);
        std::printf("%.5f,%d,%.6f,%ld,%ld,%d,%d,%.12f,%.12f,%.12f,%.12f\n",
                    fraction, ok, r.seconds, r.solved, r.applied, r.remaining, r.moved, b.total, d.total, b.max, d.max);
        if (ok) writePl(db, p, std::string(argv[3]) + std::to_string(fraction) + ".pl");
    }
}
