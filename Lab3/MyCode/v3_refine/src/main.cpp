// dp — standard-cell detailed placer（v3）
//
// 讀檔 → Stage 1（Abacus + 位移微調）→ 印結果 → Stage 2（SA）→ 印結果 → 寫檔。
// v3：10/28 規定每筆 60 秒（含讀寫檔），所以整支程式有一個期限（預設 50 秒），
//     微調和 SA 的時間都從「還剩多少」分出去，最後留時間寫檔。
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "annealer.hpp"
#include "legalizer.hpp"
#include "placedb.hpp"

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: dp INPUT_FILE OUTPUT_FILE [options]\n"
                 "  INPUT_FILE   benchmark prefix (reads INPUT_FILE.aux)\n"
                 "  OUTPUT_FILE  output prefix (writes OUTPUT_FILE.pl, OUTPUT_FILE_legal.pl, ...)\n"
                 "options:\n"
                 "  --deadline SEC    whole run (read + both stages + write) in about SEC seconds (default 50)\n"
                 "  --refine-share F  share of the remaining time the displacement refinement may use (0.3)\n"
                 "  --seed N          random seed for SA (default 1)\n"
                 "  --time SEC        SA time limit (default: whatever the deadline leaves)\n"
                 "  --effort K        SA moves per cell (default 2000)\n"
                 "  --trace FILE      write SA trace as CSV\n"
                 "  --legalizer NAME  abacus (default) or tetris\n"
                 "  --own-cost        abacus row choice by the new cell's displacement only\n"
                 "  --exact-work W    abacus: exact merge cost for the first W cell visits (1e9) ...\n"
                 "  --exact-cells N   ... then only for merges of up to N cells, a bound above (16)\n"
                 "  --no-refine       skip the displacement refinement after legalization\n"
                 "  --no-evict        refinement without the two-step (evict) move\n"
                 "  --no-sa           skip Stage 2\n"
                 "  -v                progress messages on stderr\n"
                 "SA tuning (defaults in src/annealer.hpp):\n"
                 "  --p0 P            acceptance of an average uphill move at T0 (0.8)\n"
                 "  --p0-low K P      under K moves per cell (big designs) use p0 = P instead (500 0.05)\n"
                 "  --tend R          final temperature = R * T0 (0.001)\n"
                 "  --steps N         temperature steps (100)\n"
                 "  --target P        probability of aiming at the optimal region (0.5)\n"
                 "  --win0 F          initial range window, fraction of the chip (0.05)\n"
                 "  --minwin C R      smallest window: C cell widths x R rows (4 1.5)\n"
                 "  --clamp-target    keep optimal-region moves inside the window too\n");
}

double seconds(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

int main(int argc, char** argv) {
    auto t0 = std::chrono::steady_clock::now();
    if (argc < 3) { usage(); return 1; }
    std::string input = argv[1], output = argv[2];
    LegalizeOptions lopt;
    SAOptions sopt;
    bool runSA = true;
    double deadline = 50, saTime = 1e18;
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", a.c_str()); std::exit(1); }
            return argv[++i];
        };
        if (a == "--seed") sopt.seed = std::strtoull(next(), nullptr, 10);
        else if (a == "--deadline") deadline = std::atof(next());
        else if (a == "--refine-share") lopt.refineShare = std::atof(next());
        else if (a == "--time") saTime = std::atof(next());
        else if (a == "--effort") sopt.movesPerCell = std::atof(next());
        else if (a == "--trace") sopt.tracePath = next();
        else if (a == "--p0") sopt.p0 = std::atof(next());
        else if (a == "--p0-low") { sopt.lowEffort = std::atof(next()); sopt.p0Low = std::atof(next()); }
        else if (a == "--tend") sopt.tEndRatio = std::atof(next());
        else if (a == "--steps") sopt.steps = std::atoi(next());
        else if (a == "--win0") sopt.win0 = std::atof(next());
        else if (a == "--target") sopt.pTarget = std::atof(next());
        else if (a == "--minwin") { sopt.minWinCells = std::atof(next()); sopt.minWinRows = std::atof(next()); }
        else if (a == "--clamp-target") sopt.clampTarget = true;
        else if (a == "--legalizer") lopt.method = next();
        else if (a == "--own-cost") lopt.totalCost = false;
        else if (a == "--exact-work") lopt.exactWork = std::atof(next());
        else if (a == "--exact-cells") lopt.exactCells = std::atoi(next());
        else if (a == "--no-refine") lopt.refineMax = lopt.refineTotal = false;
        else if (a == "--no-evict") lopt.evict = false;
        else if (a == "--no-sa") runSA = false;
        else if (a == "-v") lopt.verbose = sopt.verbose = true;
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 1; }
    }

    PlaceDB db;
    std::string err;
    if (!db.read(input, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    if (lopt.verbose)
        std::fprintf(stderr, "[read] %zu nodes (%zu movable), %zu nets, %zu rows, %zu segments  %.2fs\n",
                     db.nodes.size(), db.movable.size(), db.nets.size(), db.rows.size(), db.segs.size(),
                     seconds(t0));
    // 最後要留的時間：Stage 2 之後的合法性檢查、算 HPWL、寫 7 個檔（100 萬 cell 實測約 0.4 秒，留 5 倍）
    double reserve = 0.5 + 1e-6 * (db.nodes.size() + db.nets.size());
    auto left = [&] { return deadline - reserve - seconds(t0); };

    // ---- Stage 1：Abacus 做完後，微調最多用掉剩下時間的 refineShare（預設 0.3）
    lopt.runEnd = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                         std::chrono::duration<double>(std::max(0.0, left())));
    Placement legal;
    if (!legalize(db, legal, lopt, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    std::string bad = checkLegal(db, legal);
    if (!bad.empty()) { std::fprintf(stderr, "error: legalization is illegal: %s\n", bad.c_str()); return 1; }
    DispStats disp = displacement(db, legal);
    std::printf("Total displacement (legal): %s\n", fmtNum(disp.total).c_str());
    std::printf("Maximum displacement (legal): %s\n", fmtNum(disp.max).c_str());
    std::printf("Total HPWL (legal): %s\n", fmtNum(totalHPWL(db, legal)).c_str());
    std::fflush(stdout);
    if (lopt.verbose) std::fprintf(stderr, "[stage1] done  %.2fs\n", seconds(t0));

    // ---- Stage 2：剩下的時間全部給 SA
    Placement final = legal;
    if (runSA) {
        sopt.timeLimit = std::min(saTime, left());
        if (sopt.timeLimit > 0.5) {
            anneal(db, final, sopt);
            bad = checkLegal(db, final);
            if (!bad.empty()) {   // 保險：SA 有 bug 的話至少交出合法的 Stage 1
                std::fprintf(stderr, "warning: SA result is illegal (%s); keeping the legal placement\n", bad.c_str());
                final = legal;
            }
        } else if (lopt.verbose) {
            std::fprintf(stderr, "[sa] skipped: no time left\n");
        }
    }
    std::printf("Total HPWL (SA): %s\n", fmtNum(totalHPWL(db, final)).c_str());
    if (lopt.verbose) std::fprintf(stderr, "[stage2] done  %.2fs\n", seconds(t0));

    if (!writeOutput(db, legal, final, output, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    if (lopt.verbose) std::fprintf(stderr, "[done] %.2fs\n", seconds(t0));
    return 0;
}
