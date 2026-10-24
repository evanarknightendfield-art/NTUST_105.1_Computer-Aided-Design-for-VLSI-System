// compare_methods (measurement tool, added after the algorithm was frozen):
// rows repair (with / without phase 2, T = 0) vs ChainDP's site repair at given caps, each followed by
// the window DP from a converged memo.
//   g++ -std=c++17 -O2 -pthread -I../src compare_methods.cpp readpl.cpp ../src/capsearch.cpp ../src/global.cpp ../src/window.cpp ../src/placedb.cpp
// compare_methods BENCH PL D [D...] : rows vs sites repair at each D, each followed by the window DP (warm memo)
#include "capsearch.hpp"
#include "global.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
bool readPlX(const PlaceDB&, const std::string&, Placement&);
int main(int argc, char** argv) {
    PlaceDB db; std::string e; db.read(argv[1], e); Placement p0; if (!readPlX(db, argv[2], p0)) return 1;
    WindowOptions w; w.passes = 1000; w.threads = 4;
    if (db.movable.size() <= 1500 && (db.utilization() >= 0.4 || db.movable.size() < 100)) { w.perRow = 5; w.perRow3 = 4; }
    WindowMemo memo; w.timeLimit = 100; windowSweeps(db, p0, w, &memo);
    auto b = displacement(db, p0);
    std::printf("base total %.4f max %.6f\n", b.total, b.max);
    for (int i = 3; i < argc; ++i) {
        double D = atof(argv[i]);
        for (int m = 0; m < 4; ++m) {
            Placement q = p0; double t = wallSeconds(); bool ok;
            if (m < 3) { RepairOptions o; o.phase1Until = wallSeconds() + 1; o.phase2Until = o.phase1Until + 1;
                         o.phase2PerCell = m == 0 ? 2000 : m == 1 ? 0 : 2000; o.phase2Temp = m == 2 ? 0 : 0.05; o.verbose = true; ok = rowRepair(db, q, D, o); }
            else ok = repairCap(db, q, D, wallSeconds() + 2, 7);
            double raw = displacement(db, q).total, t1 = wallSeconds();
            if (ok) { WindowMemo mm = memo; w.timeLimit = 10; windowSweeps(db, q, w, &mm); }
            auto d = displacement(db, q);
            std::printf("D %.4f %-10s %s raw %+.3f%%  after DP %+.3f%% max %.4f  (repair %.2fs, DP %.2fs)\n", D, m == 0 ? "rows" : m == 1 ? "rows-no-p2" : m == 2 ? "rows-T0" : "sites",
                        ok ? "ok " : "---", 100 * (raw / b.total - 1), 100 * (d.total / b.total - 1), d.max, t1 - t, wallSeconds() - t1);
        }
    }
}
