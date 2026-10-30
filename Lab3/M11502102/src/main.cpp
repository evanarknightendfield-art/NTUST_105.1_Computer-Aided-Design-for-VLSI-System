// dp — standard-cell detailed placer（你的版本）
//
// 照 Lab3/AI_Workspace/TUTORIAL.md 的 Milestone 1 ~ 6 把 TODO 補完。
// 這個檔案已經寫好：讀檔 → Stage 1 → 印結果 → Stage 2 → 印結果 → 寫檔。
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
                 "  --seed N          random seed for SA (default 1)\n"
                 "  --time SEC        SA time limit in seconds (default 60)\n"
                 "  --trace FILE      write SA trace as CSV\n"
                 "  --legalizer NAME  tetris or abacus\n"
                 "  --no-sa           skip Stage 2\n"
                 "  -v                progress messages on stderr\n");
}

double seconds(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { usage(); return 1; }
    std::string input = argv[1], output = argv[2];
    LegalizeOptions lopt;
    SAOptions sopt;
    bool runSA = true;
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", a.c_str()); std::exit(1); }
            return argv[++i];
        };
        if (a == "--seed") sopt.seed = std::strtoull(next(), nullptr, 10);
        else if (a == "--time") sopt.timeLimit = std::atof(next());
        else if (a == "--trace") sopt.tracePath = next();
        else if (a == "--legalizer") lopt.method = next();
        else if (a == "--no-sa") runSA = false;
        else if (a == "-v") lopt.verbose = sopt.verbose = true;
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 1; }
    }

    auto t0 = std::chrono::steady_clock::now();
    PlaceDB db;
    std::string err;
    if (!db.read(input, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    if (lopt.verbose)
        std::fprintf(stderr, "[read] %zu nodes (%zu movable), %zu nets, %zu rows, %zu segments  %.2fs\n",
                     db.nodes.size(), db.movable.size(), db.nets.size(), db.rows.size(), db.segs.size(),
                     seconds(t0));

    // ---- Stage 1
    Placement legal;
    if (!legalize(db, legal, lopt, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    std::string bad = checkLegal(db, legal);
    if (!bad.empty()) { std::fprintf(stderr, "error: legalization is illegal: %s\n", bad.c_str()); return 1; }
    DispStats disp = displacement(db, legal);
    std::printf("Total displacement (legal): %s\n", fmtNum(disp.total).c_str());
    std::printf("Maximum displacement (legal): %s\n", fmtNum(disp.max).c_str());
    std::printf("Total HPWL (legal): %s\n", fmtNum(totalHPWL(db, legal)).c_str());
    std::fflush(stdout);

    // ---- Stage 2
    Placement final = legal;
    if (runSA) {
        anneal(db, final, sopt);
        bad = checkLegal(db, final);
        if (!bad.empty()) {   // 保險：SA 有 bug 的話至少交出合法的 Stage 1
            std::fprintf(stderr, "warning: SA result is illegal (%s); keeping the legal placement\n", bad.c_str());
            final = legal;
        }
    }
    std::printf("Total HPWL (SA): %s\n", fmtNum(totalHPWL(db, final)).c_str());
    if (lopt.verbose) std::fprintf(stderr, "[done] %.2fs\n", seconds(t0));

    if (!writeOutput(db, legal, final, output, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    return 0;
}
