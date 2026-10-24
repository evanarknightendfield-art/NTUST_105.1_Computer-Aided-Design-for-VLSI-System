// repair_compare.cpp — diagnosis (v6 phase 1): from ONE legal start and one
// cap D, compare the two repair families CodexCapDP has:
//   rows : capsearch's local cap windows, then the row reassignment (method 3 / 1)
//   neg  : ChainDP's repairCap (ordered packing, construction, packing DFS,
//          site congestion negotiation) -- the method every ChainDP cap success
//          on e03 / e12 / h04 / h06 / h07 came from (phase1/chain_diag)
// For each: repair seconds, raw total, then window DP (memo-less, 8 threads)
// for 0.5 / 1.5 / 4 s cumulative, total and max after each.
//   g++ -std=c++17 -O3 -DNDEBUG -pthread -I../src repair_compare.cpp readpl.cpp ../src/{capsearch,global,window,placedb,chain,optimizer,legalizer}.cpp
// usage: repair_compare BENCH_PREFIX LEGAL_PL REPAIR_SECONDS DROP1 [DROP2 ...]   (drops in %, D = max * (1 - drop))
#include "capsearch.hpp"
#include "global.hpp"
#include "window.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

bool readPlX(const PlaceDB& db, const std::string& path, Placement& p);

int main(int argc, char** argv) {
    if (argc < 5) { std::fprintf(stderr, "usage: repair_compare BENCH LEGAL_PL REPAIR_S DROP%%...\n"); return 1; }
    PlaceDB db;
    std::string err;
    if (!db.read(argv[1], err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    Placement start;
    if (!readPlX(db, argv[2], start) || !checkLegal(db, start).empty()) { std::fprintf(stderr, "bad start\n"); return 1; }
    const double budget = std::atof(argv[3]);
    const auto s0 = displacement(db, start);
    std::printf("method,drop_pct,D,ok,repair_s,raw_total,raw_vs_start,dp_s,total,total_vs_start,max\n");
    WindowOptions w;
    w.passes = 1000;
    w.threads = 8;
    if (db.movable.size() <= 1500 && (db.utilization() >= 0.4 || db.movable.size() < 100)) { w.perRow = 5; w.perRow3 = 4; }
    else if (db.movable.size() > 2000) { w.perRow = std::min(4, w.perRow); w.perRow3 = std::min(3, w.perRow3); }
    for (int a = 4; a < argc; ++a) {
        const double drop = std::atof(argv[a]);
        const double D = s0.max * (1 - drop / 100);
        for (const char* method : {"rows", "neg"}) {
            Placement q = start;
            double t = wallSeconds();
            bool ok = false;
            if (std::string(method) == "rows") {
                Placement partial = start;
                WindowOptions r = w;
                r.passes = 2;
                r.timeLimit = 0.4 * budget;
                ok = repairCapWindows(db, q, D, r, nullptr, &partial);
                if (!ok) {
                    RepairOptions ro;
                    ro.phase1Until = wallSeconds() + 0.6 * (t + budget - wallSeconds());
                    ro.phase2Until = t + budget;
                    ro.phase2PerCell = db.movable.size() <= 3000 ? 2000 : 0;
                    q = partial;
                    ok = rowRepair(db, q, D, ro);
                }
            } else {
                ok = repairCap(db, q, D, t + budget, 12345);
            }
            const double rs = wallSeconds() - t;
            if (!ok) {
                std::printf("%s,%g,%.6f,0,%.3f,,,,,,\n", method, drop, D, rs);
                std::fflush(stdout);
                continue;
            }
            const double raw = displacement(db, q).total;
            double used = 0;
            for (double until : {0.5, 1.5, 4.0}) {
                WindowOptions d = w;
                d.timeLimit = until - used;
                double t2 = wallSeconds();
                windowSweeps(db, q, d);
                used += wallSeconds() - t2;
                const auto s = displacement(db, q);
                std::printf("%s,%g,%.6f,1,%.3f,%.4f,%.6f,%.3f,%.4f,%.6f,%.6f\n", method, drop, D, rs, raw, raw / s0.total - 1,
                            used, s.total, s.total / s0.total - 1, s.max);
                std::fflush(stdout);
            }
        }
    }
    return 0;
}
