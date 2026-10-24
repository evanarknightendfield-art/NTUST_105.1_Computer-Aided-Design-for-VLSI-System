// ClaudeOpus55_v5 — CodexChainDP (v4 + chains, global cap repair, evaluated
// global swaps) with the changes of Testcases/results/ClaudeOpus55_v5/diagnosis.md:
//   * the maximum-displacement search reassigns rows and re-packs them exactly
//     (capsearch.cpp), with ChainDP's site-level repair as a second method;
//   * the extra Stage 1 steps (chains, cap search) get a budget scaled with the
//     design and Stage 1's own time instead of a fixed 4 + 16 seconds;
//   * the SA uses its whole time limit (annealer.cpp).
//   Stage 1: legalization (Abacus + displacement refinement
//            + 2/3-row window DP)                              -> OUTPUT_legal.pl
//   Stage 2: simulated annealing on HPWL (parallel row bands),
//            legality kept, then deterministic polish          -> OUTPUT.pl
// v4: every stage has a time budget and every optional step is guarded, so
// the program always ends with legal output files.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <filesystem>
#include <exception>
#include <functional>
#include <thread>
#include "optimizer.hpp"
#include "window.hpp"
#include "chain.hpp"
#include "global.hpp"
#include "capsearch.hpp"

#include "annealer.hpp"
#include "legalizer.hpp"
#include "placedb.hpp"

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: dp INPUT_FILE OUTPUT_FILE [options]\n"
                 "  INPUT_FILE   benchmark prefix (reads INPUT_FILE.aux)\n"
                 "  OUTPUT_FILE  output prefix (writes OUTPUT_FILE.pl, OUTPUT_FILE_legal.pl, ...)\n"
                 "options (v4):\n"
                 "options (v5):\n"
                 "  --extra-time S          budget of the extra Stage 1 steps (chains + cap search);\n"
                 "                          default: 12 s (<= 2000 cells) or 6 s; 16 = max displacement first (see README)\n"
                 "  --cap-time S            cap search budget (default: what the chains leave of --extra-time; 0 disables)\n"
                 "  --total-ratio R         cap search: maximum total displacement / total before it (1.004)\n"
                 "  --legacy-global         ChainDP's global cap repair instead of the v5 cap search\n"
                 "options (CodexChainDP):\n"
                 "  --global-swap P         cold global/vertical swap proposal probability (0.04)\n"
                 "  --global-time S         legacy global cap repair budget (16 seconds; with --legacy-global)\n"
                 "  --chain-time S          chain + DP budget (default: 25 %% of --extra-time, at most 3 seconds)\n"
                 "  --chain-rounds N        alternate chains and window DP (2; 0 disables)\n"
                 "  --chain-depth N         maximum cells displaced in a chain (10)\n"
                 "  --chain-branches N      retained nonterminal candidates (12)\n"
                 "  --chain-expansions N    search nodes per root (160)\n"
                 "  --chain-roots N         worst-displacement roots per pass (12000)\n"
                 "  --chain-blockers N      maximum cells ejected per insertion (2; 1 or 2)\n"
                 "  --no-target-windows     disable global-target region pairing\n"
                 "  --threads N             worker threads for SA and window DP (0 = auto, at most 8)\n"
                 "  --stage1-time S         Stage 1 refinement budget, seconds from start (90)\n"
                 "  --time S                SA time limit in seconds (60)\n"
                 "  --polish-time S         post-SA polish budget in seconds (30)\n"
                 "  --sync M                parallel SA: bands merge every M attempts per cell (1)\n"
                 "options (CodexWindowDP / v3):\n"
                 "  --window-passes N       window DP sweeps (1000 = until converged; 0 disables)\n"
                 "  --hpwl-window-passes N  joint two-row HPWL proposal sweeps (2)\n"
                 "  --order-passes N        exact fixed-gap HPWL ordering sweeps (2)\n"
                 "  --window-cells N        cells per row of a two-row window (4, 5 if <= 1500 cells; 1..5)\n"
                 "  --window-sites N        maximum span per row in sites (256)\n"
                 "  --rowopt P L / --ism P K  v3 cold SA macro moves (0.01 12 each)\n"
                 "  --polish-from F / --finish N  v3 defaults 0.5 / 4\n"
                 "  --no-pareto       disable new constrained bottleneck matching\n"
                 "  --pareto-passes N displacement matching sweeps (default 2)\n"
                 "  --no-polish       disable new deterministic HPWL descent\n"
                 "  --polish-passes N HPWL descent sweeps (default 3)\n"
                 "  --group-size N    assignment size, 2..128 (default 32)\n"
                 "  --min-moves N     minimum SA attempts (default 5000000)\n"
                 "  --seed N          random seed for SA (default 1)\n"
                 "  --effort K        SA moves per cell (default 5000; default total capped at 60000000)\n"
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
    WindowOptions wopt;
    ChainOptions copt;
    int chainRounds = 2;
    double chainTime = -1;                // v5: -1 = 25 % of the extra budget, at most 3 s
    double globalTime = 16, totalRatio = 1.004;
    double extraTime = -1, capTime = -1;  // v5: -1 = automatic (see usage)
    bool legacyGlobal = false;
    bool targetWindows = true;
    wopt.passes = 1000;   // v4: until converged
    int hpwlWindowPasses=2;
    int orderPasses=2;
    bool runSA = true;
    double stage1Time = 90;   // v4: Stage 1 refinement budget (seconds since start)
    double polishTime = 30;   // v4: post-SA deterministic polish budget (seconds)
    bool windowCellsSet = false;
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", a.c_str()); std::exit(1); }
            return argv[++i];
        };
        if (a == "--global-swap") sopt.pGlobalSwap = std::stod(next());
        else if (a == "--global-time") globalTime = std::stod(next());
        else if (a == "--total-ratio") totalRatio = std::stod(next());
        else if (a == "--chain-time") chainTime = std::stod(next());
        else if (a == "--extra-time") extraTime = std::stod(next());
        else if (a == "--cap-time") capTime = std::stod(next());
        else if (a == "--legacy-global") legacyGlobal = true;
        else if (a == "--chain-rounds") chainRounds = std::stoi(next());
        else if (a == "--chain-depth") copt.depth = std::stoi(next());
        else if (a == "--chain-branches") copt.branches = std::stoi(next());
        else if (a == "--chain-expansions") copt.expansions = std::stoi(next());
        else if (a == "--chain-roots") copt.roots = std::stoi(next());
        else if (a == "--chain-blockers") copt.maxBlockers = std::stoi(next());
        else if (a == "--no-target-windows") targetWindows = false;
        else if (a == "--window-passes") wopt.passes = std::stoi(next());
        else if (a == "--order-passes") orderPasses = std::stoi(next());
        else if (a == "--hpwl-window-passes") hpwlWindowPasses = std::stoi(next());
        else if (a == "--window-cells") { wopt.perRow = std::stoi(next()); windowCellsSet = true; }
        else if (a == "--window-sites") wopt.maxSites = std::stoi(next());
        else if (a == "--no-pareto") oopt.paretoPasses = 0;
        else if (a == "--pareto-passes") oopt.paretoPasses = std::stoi(next());
        else if (a == "--no-polish") oopt.polishPasses = 0;
        else if (a == "--polish-passes") oopt.polishPasses = std::stoi(next());
        else if (a == "--group-size") oopt.groupSize = std::stoi(next());
        else if (a == "--min-moves") sopt.minMoves = std::stod(next());
        else if (a == "--seed") sopt.seed = std::strtoull(next(), nullptr, 10);
        else if (a == "--time") sopt.timeLimit = std::atof(next());
        else if (a == "--effort") {sopt.movesPerCell = std::atof(next());sopt.maxMoves=1e18;}
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
        else if (a == "--rowopt") {sopt.pRowOpt=std::stod(next());sopt.rowWin=std::stoi(next());}
        else if (a == "--ism") {sopt.pIsm=std::stod(next());sopt.ismSize=std::stoi(next());}
        else if (a == "--polish-from") sopt.polishFrom=std::stod(next());
        else if (a == "--finish") lopt.finishRounds=std::stoi(next());
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
        else if (a == "--stage1-time") stage1Time = std::atof(next());
        else if (a == "--threads") sopt.threads = std::atoi(next());
        else if (a == "--polish-time") polishTime = std::atof(next());
        else if (a == "--sync") sopt.syncMoves = std::atof(next());
        else if (a == "-v") { lopt.verbose = sopt.verbose = oopt.verbose = copt.verbose = true; wopt.verbose = 1; }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 1; }
    }

    if (!(sopt.pGlobalSwap >= 0 && sopt.pGlobalSwap <= 0.5) ||
        !std::isfinite(globalTime) || globalTime < 0 || !std::isfinite(totalRatio) || totalRatio < 1 ||
        !std::isfinite(chainTime) || chainTime < -1 || chainRounds < 0 || chainRounds > 20 ||
        !std::isfinite(extraTime) || extraTime < -1 || !std::isfinite(capTime) || capTime < -1 ||
        copt.depth < 1 || copt.depth > 32 || copt.branches < 1 || copt.branches > 128 ||
        copt.expansions < 1 || copt.expansions > 100000 || copt.roots < 1 || copt.maxBlockers < 1 || copt.maxBlockers > 2 ||
        !std::isfinite(stage1Time) || stage1Time < 0 || !std::isfinite(polishTime) || polishTime < 0 ||
        sopt.threads < 0 || sopt.threads > 64 || !std::isfinite(sopt.syncMoves) || sopt.syncMoves <= 0 ||
        wopt.passes<0 || wopt.passes>1000 || hpwlWindowPasses<0 || hpwlWindowPasses>20 || orderPasses<0 || orderPasses>20 ||
        wopt.perRow<1 || wopt.perRow>5 || wopt.maxSites<1 || wopt.maxSites>4096 ||
        sopt.rowWin<2 || sopt.rowWin>32 || sopt.ismSize<2 || sopt.ismSize>32 ||
        !(sopt.pRowOpt>=0 && sopt.pIsm>=0 && sopt.pRowOpt+sopt.pIsm<=1) ||
        !(sopt.polishFrom>=0 && sopt.polishFrom<=1) || lopt.finishRounds<0 || lopt.finishRounds>20 ||
        oopt.groupSize < 2 || oopt.groupSize > 128 || oopt.paretoPasses < 0 || oopt.paretoPasses > 20 ||
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
    lopt.windowDP = wopt.passes > 0;
    lopt.windowCells = wopt.perRow;
    lopt.until = stage1Time;
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
    // v4: every optional step is guarded. If one throws or breaks its invariant,
    // the placement from before that step is kept and the run goes on: a
    // crash or a missing output scores zero, a skipped step only costs quality.
    auto guarded = [&](const char* name, Placement& p, const std::function<void()>& step,
                       const std::function<bool()>& ok) {
        Placement keep = p;
        try {
            step();
            if (ok()) return;
            std::fprintf(stderr, "warning: %s broke an invariant, step skipped\n", name);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "warning: %s failed (%s), step skipped\n", name, e.what());
        } catch (...) {
            std::fprintf(stderr, "warning: %s failed (non-standard exception), step skipped\n", name);
        }
        p = std::move(keep);
    };

    // v4: window DP threads: all of them share one placement (no copies), so
    // unlike the SA they are not limited by the design size
    wopt.threads = lopt.threads =
        sopt.threads > 0 ? sopt.threads : (int)std::min(8u, std::max(1u, std::thread::hardware_concurrency()));
    // v4: small designs get bigger windows (10 / 12 cells instead of 8 / 9):
    // they cost about 4x, which is nothing there, and reach further.
    if (!windowCellsSet && db.movable.size() <= 1500 && (db.utilization() >= 0.4 || db.movable.size() < 100)) {
        wopt.perRow = lopt.windowCells = 5;
        wopt.perRow3 = lopt.windowCells3 = 4;
    }
    // ---- Stage 1
    Placement legal;
    if (db.movable.empty()) {legal.seg.assign(db.nodes.size(), -1);legal.site.assign(db.nodes.size(), 0);}
    else {
        bool ok = false;
        try { ok = legalize(db, legal, lopt, err); }
        catch (const std::exception& e) { err = e.what(); }
        if (!ok || !checkLegal(db, legal).empty()) {   // fall back to plain Abacus without refinement
            std::fprintf(stderr, "warning: legalization failed (%s), retrying without refinement\n", err.c_str());
            LegalizeOptions plain = lopt;
            plain.refineMax = plain.refineTotal = false;
            plain.spread = "off";
            if (!legalize(db, legal, plain, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        }
    }
    const DispStats s1 = displacement(db, legal);
    auto noWorse = [&] {
        DispStats d = displacement(db, legal);
        return checkLegal(db, legal).empty() && d.total <= s1.total + 1e-5 && d.max <= s1.max + 1e-6;
    };
    oopt.until = stage1Time + 10;
    guarded("pareto matching", legal, [&] { paretoDisplacement(db, legal, oopt); }, noWorse);
    wopt.timeLimit = std::max(1.0, stage1Time + 10 - wallSeconds());
    // v5: one memo for every displacement DP after legalization, so the DPs
    // after the chains and the cap search only re-solve windows that changed
    WindowMemo stage1Memo;
    guarded("window DP", legal, [&] { windowSweeps(db, legal, wopt, &stage1Memo); }, noWorse);
    // v5: the extra steps get 6 s, or 12 s up to 2000 cells (their SA always
    // finishes its schedule, so Stage 1 time does not cost HPWL there). ChainDP
    // spent a fixed 4 + 16 s (205 s of global search over the 27 cases, mostly
    // on unresolved caps); under equal total time that came straight out of
    // the SA on every design whose SA is cut by the time limit (diagnosis.md
    // 2.3). v5's SA does 1.4 - 3x the moves of v4's in the same time (full time
    // use, cheaper merge), which pays for these 6 s, not for 16 (holdout,
    // equal time, 3 seeds: 6 s -0.9 %, 16 s -0.1 % with h06 +2 %).
    if (extraTime < 0)
        extraTime = db.movable.size() <= 2000 ? 12.0 : 6.0;
    if (chainTime < 0) chainTime = std::min(3.0, 0.25 * extraTime);
    const double extraUntil = wallSeconds() + extraTime;
    // Keep v4's incumbent intact, then extend its neighbourhood beyond three
    // adjacent rows. Every phase is checked against its immediate predecessor.
    // This is an explicit EXTRA budget, not an equal-total-time comparison.
    const double chainUntil = wallSeconds() + chainTime;
    for (int round = 0; round < chainRounds && wallSeconds() < chainUntil; ++round) {
        const DispStats before = displacement(db, legal);
        auto paretoNow = [&] {
            const auto after = displacement(db, legal);
            return checkLegal(db, legal).empty() && after.total <= before.total + 1e-5 && after.max <= before.max + 1e-6;
        };
        long targetMoves = 0;
        if (targetWindows) {
            WindowOptions targetOpt = wopt;
            targetOpt.passes = 2;
            targetOpt.timeLimit = (chainUntil - wallSeconds()) * 0.3 / (chainRounds - round);
            guarded("global-target windows", legal, [&] {
                targetMoves = targetedWindowsDisp(db, legal, targetOpt);
            }, paretoNow);
        }
        const auto beforeChain = displacement(db, legal);
        ChainResult result;
        copt.seconds = (chainUntil - wallSeconds()) * 0.75 / (chainRounds - round);
        copt.seconds = std::max(0.0, copt.seconds);
        guarded("ejection chains", legal, [&] { result = chainDisplacement(db, legal, copt); }, [&] {
            const auto after = displacement(db, legal);
            return checkLegal(db, legal).empty() && after.total <= beforeChain.total + 1e-5 && after.max <= beforeChain.max + 1e-6;
        });
        if (result.accepted == 0 && targetMoves == 0) break;
        WindowOptions afterChain = wopt;
        afterChain.timeLimit = std::max(0.0, (chainUntil - wallSeconds()) * 0.5 / (chainRounds - round));
        const auto beforeDP = displacement(db, legal);
        guarded("post-chain windows", legal, [&] { windowSweeps(db, legal, afterChain, &stage1Memo); }, [&] {
            const auto after = displacement(db, legal);
            return checkLegal(db, legal).empty() && after.total <= beforeDP.total + 1e-5 && after.max <= beforeDP.max + 1e-6;
        });
        if (lopt.verbose) {
            const auto after = displacement(db, legal);
            std::fprintf(stderr, "[chain-round] %d: %ld target windows, %ld chains, longest %d, total %.6f -> %.6f, max %.6f -> %.6f\n",
                         round + 1, targetMoves, result.accepted, result.longest, before.total, after.total, before.max, after.max);
        }
    }
    const auto beforeGlobal = displacement(db, legal);
    if (legacyGlobal) {
        guarded("global cap repair", legal, [&] {
            globalMinimax(db, legal, globalTime, totalRatio, wopt.threads, lopt.verbose);
        }, [&] {
            const auto after = displacement(db, legal);
            return checkLegal(db, legal).empty() && after.max <= beforeGlobal.max + 1e-6 &&
                   after.total <= beforeGlobal.total * totalRatio + 1e-5;
        });
    } else {
        CapSearchOptions copts;
        copts.seconds = capTime >= 0 ? capTime : std::max(0.0, extraUntil - wallSeconds());
        copts.totalRatio = totalRatio;
        copts.threads = wopt.threads;
        copts.dp = wopt;
        copts.dp.passes = 1000;
        copts.verbose = lopt.verbose;
        guarded("cap search", legal, [&] { capSearch(db, legal, copts, &stage1Memo); }, [&] {
            const auto after = displacement(db, legal);
            return checkLegal(db, legal).empty() && after.max <= beforeGlobal.max + 1e-6 &&
                   after.total <= beforeGlobal.total * totalRatio + 1e-5;
        });
    }
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
        auto legalNow = [&] { return checkLegal(db, final).empty(); };
        if (sopt.timeLimit > 0) guarded("SA", final, [&] { anneal(db, final, sopt); }, legalNow);
        double beforePolish = totalHPWL(db, final);
        auto better = [&] { return checkLegal(db, final).empty() && totalHPWL(db, final) <= beforePolish + 1e-5; };
        oopt.until = wallSeconds() + polishTime;
        guarded("HPWL polish", final, [&] {
            polishHPWL(db, final, oopt, sopt.tracePath.empty() ? "" : sopt.tracePath + ".polish.csv");
        }, better);
        WindowOptions hpopt = wopt;
        hpopt.passes = hpwlWindowPasses;
        hpopt.timeLimit = std::max(0.0, oopt.until - wallSeconds());
        guarded("HPWL windows", final, [&] { optimizeWindowsHPWL(db, final, hpopt); }, better);
        guarded("order DP", final, [&] { reorderWindows(db, final, orderPasses, wopt.verbose, oopt.until); }, better);
        if (lopt.verbose) std::fprintf(stderr, "[polish-total] %.6f -> %.6f\n", beforePolish, totalHPWL(db, final));
    }
    if (!checkLegal(db, final).empty() || totalHPWL(db, final) > legalHPWL + 1e-5) {
        std::fprintf(stderr, "warning: Stage 2 result rejected, keeping the legal placement\n");
        final = legal;
    }
    std::printf("Total HPWL (SA): %s\n", fmtNum(totalHPWL(db, final)).c_str());
    if (lopt.verbose) std::fprintf(stderr, "[stage2] done  %.2fs\n", seconds(t0));

    if (!writeOutput(db, legal, final, output, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    return 0;
}

int main(int argc, char** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& e) {std::fprintf(stderr, "error: %s\n", e.what());return 1;}
}
