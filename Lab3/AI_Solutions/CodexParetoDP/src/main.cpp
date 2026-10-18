// dp — standard-cell detailed placer (CAD Project 3).
//   Stage 1: legalization (Abacus + displacement refinement) -> OUTPUT_legal.pl
//   Stage 2: simulated annealing on HPWL, legality kept       -> OUTPUT.pl
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <filesystem>
#include <exception>
#include "optimizer.hpp"

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
                 "  --no-pareto       disable new constrained bottleneck matching\n"
                 "  --pareto-passes N displacement matching sweeps (default 2)\n"
                 "  --no-polish       disable new deterministic HPWL descent\n"
                 "  --polish-passes N HPWL descent sweeps (default 3)\n"
                 "  --group-size N    assignment size, 2..128 (default 32)\n"
                 "  --min-moves N     minimum SA attempts (default 5000000)\n"
                 "  --seed N          random seed for SA (default 1)\n"
                 "  --time SEC        SA time limit in seconds (default 60)\n"
                 "  --effort K        SA moves per cell (default 2000)\n"
                 "  --trace FILE      write SA trace (iteration,current,best,T,window,accept) as CSV\n"
                 "  --legalizer NAME  abacus (default) or tetris\n"
                 "  --own-cost        abacus row choice by the new cell's displacement only\n"
                 "  --no-refine       skip the displacement refinement after legalization\n"
                 "  --no-evict        refinement without the two-step (evict) move\n"
                 "  --no-matching     refinement without same-width matching (v2)\n"
                 "  --no-rowopt       refinement without exact per-row re-placement (v2)\n"
                 "  --spread MODE     flow-based spreading before Abacus: auto (default), on, off (v2)\n"
                 "  --bin R C         spreading bins: R rows x C average cell widths (2 16)\n"
                 "  --spread-rounds N bound relaxations while spreading (4)\n"
                 "  --no-sa           skip Stage 2 (OUTPUT_FILE.pl = legal placement)\n"
                 "  -v                progress messages on stderr\n"
                 "SA tuning (defaults in src/annealer.hpp):\n"
                 "  --p0 P            acceptance of an average uphill move at T0 (0.8)\n"
                 "  --tend R          final temperature = R * T0 (0.001)\n"
                 "  --steps N         temperature steps (100)\n"
                 "  --target P        probability of aiming at the optimal region (0.5)\n"
                 "  --win0 F          initial range window, fraction of the chip (0.05)\n"
                 "  --minwin C R      smallest window: C cell widths x R rows (4 1.5)\n"
                 "  --clamp-target    keep optimal-region moves inside the window too\n"
                 "  --reorder P       probability of a local-reordering move (0.1, v2)\n"
                 "  --near K          if a move's target fails, try K cells/gaps on each side (auto: 3 if dense, v2)\n"
                 "  --maxwin F        largest range window, fraction of the chip (0.1, v2)\n");
}

double seconds(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

int run(int argc, char** argv) {
    if (argc < 3) { usage(); return 1; }
    std::string input = argv[1], output = argv[2];
    LegalizeOptions lopt;
    SAOptions sopt;
    OptimizeOptions oopt;
    bool runSA = true;
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", a.c_str()); std::exit(1); }
            return argv[++i];
        };
        if (a == "--no-pareto") oopt.paretoPasses = 0;
        else if (a == "--pareto-passes") oopt.paretoPasses = std::stoi(next());
        else if (a == "--no-polish") oopt.polishPasses = 0;
        else if (a == "--polish-passes") oopt.polishPasses = std::stoi(next());
        else if (a == "--group-size") oopt.groupSize = std::stoi(next());
        else if (a == "--min-moves") sopt.minMoves = std::stod(next());
        else if (a == "--seed") sopt.seed = std::strtoull(next(), nullptr, 10);
        else if (a == "--time") sopt.timeLimit = std::atof(next());
        else if (a == "--effort") sopt.movesPerCell = std::atof(next());
        else if (a == "--trace") sopt.tracePath = next();
        else if (a == "--p0") sopt.p0 = std::atof(next());
        else if (a == "--tend") sopt.tEndRatio = std::atof(next());
        else if (a == "--target") sopt.pTarget = std::atof(next());
        else if (a == "--steps") sopt.steps = std::atoi(next());
        else if (a == "--win0") sopt.win0 = std::atof(next());
        else if (a == "--minwin") { sopt.minWinCells = std::atof(next()); sopt.minWinRows = std::atof(next()); }
        else if (a == "--clamp-target") sopt.clampTarget = true;
        else if (a == "--reorder") sopt.pReorder = std::atof(next());
        else if (a == "--near") sopt.nearTries = std::atoi(next());
        else if (a == "--maxwin") sopt.maxWin = std::atof(next());
        else if (a == "--legalizer") lopt.method = next();
        else if (a == "--own-cost") lopt.totalCost = false;
        else if (a == "--no-refine") lopt.refineMax = lopt.refineTotal = false;
        else if (a == "--no-evict") lopt.evict = false;
        else if (a == "--no-matching") lopt.matching = false;
        else if (a == "--no-rowopt") lopt.rowOpt = false;
        else if (a == "--spread") lopt.spread = next();
        else if (a == "--bin") { lopt.binRows = std::atoi(next()); lopt.binCells = std::atof(next()); }
        else if (a == "--spread-rounds") lopt.spreadRounds = std::atoi(next());
        else if (a == "--no-sa") runSA = false;
        else if (a == "-v") lopt.verbose = sopt.verbose = oopt.verbose = true;
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 1; }
    }

    if (oopt.groupSize < 2 || oopt.groupSize > 128 || oopt.paretoPasses < 0 || oopt.paretoPasses > 20 ||
        oopt.polishPasses < 0 || oopt.polishPasses > 20 || !std::isfinite(sopt.timeLimit) || sopt.timeLimit < 0 ||
        !std::isfinite(sopt.movesPerCell) || sopt.movesPerCell < 0 || sopt.movesPerCell > 1e7 ||
        !std::isfinite(sopt.minMoves) || sopt.minMoves < 0 || sopt.minMoves > 1e12 ||
        !(sopt.p0 > 0 && sopt.p0 < 1) || !(sopt.tEndRatio > 0 && sopt.tEndRatio <= 1) || sopt.steps < 1 ||
        !(sopt.pTarget >= 0 && sopt.pTarget <= 1) || !(sopt.pReorder >= 0 && sopt.pReorder <= 1) ||
        lopt.binRows < 1 || !(lopt.binCells > 0) || lopt.spreadRounds < 0 ||
        (lopt.spread != "auto" && lopt.spread != "on" && lopt.spread != "off") ||
        (lopt.method != "abacus" && lopt.method != "tetris")) {
        std::fprintf(stderr, "error: invalid option value\n"); return 1;
    }
    auto t0 = std::chrono::steady_clock::now();
    PlaceDB db;
    std::string err;
    if (!db.read(input, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    if (lopt.verbose)
        std::fprintf(stderr, "[read] %zu nodes (%zu movable), %zu nets, %zu rows, %zu segments, utilization %.3f  %.2fs\n",
                     db.nodes.size(), db.movable.size(), db.nets.size(), db.rows.size(), db.segs.size(),
                     db.utilization(), seconds(t0));

    // Check paths BEFORE opening output or trace files.
    auto canonical = [](const std::string& path) {return std::filesystem::weakly_canonical(path);};
    for (const auto& source : {db.auxPath, db.nodesFile, db.netsFile, db.wtsFile, db.plFile, db.sclFile}) {
        if (source.empty()) continue;
        for (const auto* ext : {".aux", ".nodes", ".nets", ".wts", ".scl", ".pl", "_legal.pl"})
            if (canonical(source) == canonical(output + ext)) throw std::runtime_error("output aliases input");
        if (!sopt.tracePath.empty() && (canonical(source)==canonical(sopt.tracePath) ||
            canonical(source)==canonical(sopt.tracePath+".polish.csv"))) throw std::runtime_error("trace aliases input");
    }
    if (!sopt.tracePath.empty()) {
        for (const auto* ext : {".aux", ".nodes", ".nets", ".wts", ".scl", ".pl", "_legal.pl"})
            if (canonical(output+ext)==canonical(sopt.tracePath) || canonical(output+ext)==canonical(sopt.tracePath+".polish.csv"))
                throw std::runtime_error("trace aliases output");
        auto parent=std::filesystem::path(sopt.tracePath).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
    }
    // ---- Stage 1
    Placement legal;
    if (db.movable.empty()) {legal.seg.assign(db.nodes.size(), -1);legal.site.assign(db.nodes.size(), 0);}
    else if (!legalize(db, legal, lopt, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    DispStats beforePareto = displacement(db, legal);
    paretoDisplacement(db, legal, oopt);
    DispStats afterPareto = displacement(db, legal);
    if (afterPareto.total > beforePareto.total + 1e-5 || afterPareto.max > beforePareto.max + 1e-6)
        throw std::runtime_error("displacement Pareto invariant failed");
    std::string bad = checkLegal(db, legal);
    if (!bad.empty()) { std::fprintf(stderr, "error: legalization is illegal: %s\n", bad.c_str()); return 1; }
    DispStats disp = displacement(db, legal);
    double legalHPWL = totalHPWL(db, legal);
    std::printf("Total displacement (legal): %s\n", fmtNum(disp.total).c_str());
    std::printf("Maximum displacement (legal): %s\n", fmtNum(disp.max).c_str());
    std::printf("Total HPWL (legal): %s\n", fmtNum(legalHPWL).c_str());
    std::fflush(stdout);
    if (lopt.verbose) std::fprintf(stderr, "[stage1] done  %.2fs\n", seconds(t0));

    // ---- Stage 2
    Placement final = legal;
    if (runSA) {
        if (sopt.timeLimit > 0) anneal(db, final, sopt);
        double beforePolish = totalHPWL(db, final);
        polishHPWL(db, final, oopt, sopt.tracePath.empty() ? "" : sopt.tracePath + ".polish.csv");
        if (lopt.verbose) std::fprintf(stderr, "[polish-total] %.6f -> %.6f\n", beforePolish, totalHPWL(db, final));
        bad = checkLegal(db, final);
        if (!bad.empty()) {
            throw std::runtime_error("illegal optimization result: " + bad);
        }
    }
    if (totalHPWL(db, final) > legalHPWL + 1e-5) throw std::runtime_error("HPWL increased after Stage 2");
    std::printf("Total HPWL (SA): %s\n", fmtNum(totalHPWL(db, final)).c_str());
    if (lopt.verbose) std::fprintf(stderr, "[stage2] done  %.2fs\n", seconds(t0));

    if (!writeOutput(db, legal, final, output, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    return 0;
}

int main(int argc, char** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& e) {std::fprintf(stderr, "error: %s\n", e.what());return 1;}
}
