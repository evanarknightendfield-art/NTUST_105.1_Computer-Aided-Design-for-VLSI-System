#include "guard.hpp"
// dp — standard-cell detailed placer.
//   * the Stage 1 refinement has a time share; when it runs out, the final rounds are skipped
//     so the pareto matching (parallel) and the window DP get the time;
//   * parallel workers (SA: CPUs only on memory-bound designs);
//   * the final rounds' window memo goes on to main's window DPs;
//   * the maximum-displacement search reassigns rows and re-packs them exactly
//     (capsearch.cpp), with a site-level repair as a second method; an attempt
//     starts only if what the last one overran still fits;
//   * the extra Stage 1 steps (chains, cap search) get a budget scaled with the
//     design and Stage 1's own time;
//   * the SA uses its whole time limit (annealer.cpp).
//   Stage 1: legalization (Abacus + displacement refinement
//            + 2/3-row window DP)                              -> OUTPUT_legal.pl
//   Stage 2: simulated annealing on HPWL (parallel row bands),
//            legality kept, then deterministic polish          -> OUTPUT.pl
// every stage has a time budget and every optional step is guarded, so
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
#include "nswap.hpp"
#include "nswap_reserve.hpp"
#include <algorithm>

#include "annealer.hpp"
#include "legalizer.hpp"
#include "placedb.hpp"
#include "options_dump.hpp"

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: dp INPUT_FILE OUTPUT_FILE [options]\n"
                 "  INPUT_FILE   benchmark prefix (reads INPUT_FILE.aux)\n"
                 "  OUTPUT_FILE  output prefix (writes OUTPUT_FILE.pl, OUTPUT_FILE_legal.pl, ...)\n"
                 "options (scheduling controls):\n"
                 "  --abacus-fast N          scorer: 0 original, 1 arrays, 2 exact bound, 3 approximate bound (default 3)\n"
                 "                           all four preserve row choices and scorer switch points\n"
                 "  --abacus-check           run Abacus alone in modes 0 / 1 / 2 / 3, print times and whether they agree, exit\n"
                 "  --legacy-sched           the scheduling controls below off; options after it turn single ones on\n"
                 "  --max-after-abacus 0|1             max pass budget measured from the end of Abacus (1)\n"
                 "  --max-share F / --max-rate R / --max-tail T / --max-floor F / --max-prune 0|1   its share of the\n"
                 "                           Stage 1 time left (0.35), stop rate (0.02 = 2 %% of the max per second),\n"
                 "                           stop when > T of the cells lie within 10 %% of the max (0.001), stop at\n"
                 "                           F x the max after Abacus (0 = off), heuristic row prune (0)\n"
                 "  --predict-sched 0|1           Stage 1 schedule by predicted Stage 2 (polish only / anneal) and pass gains (1)\n"
                 "  --rate-window 0|1          main displacement DP scored per pass, stopped by value per second (1)\n"
                 "  --window-rate-scope 0|1 try each row neighborhood before rate stopping\n"
                 "  --window-scratch 0|1    reuse window DP storage (auto: off in small dual pipeline)\n"
                 "  --window-kernel 0|1|2   scalar / transposed / restricted-select recurrence (0)\n"
                 "  --window-rate R / --window-hpwl Wsa Wpolish   its stop rate (0.005) and HPWL weights (0.5 1)\n"
                 "  --early-exit 0|1 / --polish-partial 0|1 / --box-cache 0|1   D: expired polish steps return at once (1),\n"
                 "                           a polish pass may start when a whole one no longer fits (1), box cache (0)\n"
                 "  --sa-auto N E M / --sa-sync-auto 0|1   E: below N attempts per cell the SA restarts at T0 x (n/N)^E\n"
                 "                           (>= M) and merges every 2 attempts per cell (1000 0.5 0.2, 1)\n"
                 "  --sa-tstart R / --sa-threads N   measurement: force the start ratio / the SA worker count\n"
                 "options (more):\n"
                 "options (neutral swaps):\n"
                 "options (max search and memory):\n"
                 "  --max-fast 0..7          4 binary endpoints; 5 monotone endpoints; 6 also weighted rows; 7 also direct evict rooms\n"
                 "                           default 7; legacy dual pipelines 0\n"
                 "  --max-threads N          max-search workers only (default min(16, available CPUs), small dual pipeline\n"
                 "                           follows --threads; 0 follows --threads; an explicit N is used as given)\n"
                 "  --max-release 0|1        release scratch after the whole max/matching phase (1)\n"
                 "  --stage1-memo-release 0|1  release displacement-DP memo before final neutral swaps (1)\n"
                 "  --release-trim 0|1       after memo release, return unused allocator pages (1; glibc only)\n"
                 "  --max-policy-min-seconds S  legacy max policy interval (default 1; small/complete paths retain 0.25)\n"
                 "  --max-check-modes F:T,.. --max-check configurations (maxFast:threads), the first is the reference\n"
                 "  --max-check-moves N      --max-check compares only the first N moves\n"
                 "  --max-check-matching     --max-check also runs the max matching and compares the placements\n"
                 "options (defaults on unless noted; --legacy-stage1 turns them off):\n"
                 "  --tie-band B             plain/spread within B of a tie: both run the final rounds (0.01; 0 = off)\n"
                 "  --chain-probe 0|1        chain phase only while its first step pays the window DP's last rate\n"
                 "  --nswap-reserve 0|1      reserve estimated neutral-swap work after DP (default 0)\n"
                 "  --nswap-spatial 0|1      neutral swaps visit cells by position\n"
                 "  --window-inc-hpwl 0|1    per-pass HPWL of the window DP from changed nets (bit-identical)\n"
                 "  --window-hpwl-check      also compute it in full every pass and print the comparison\n"
                 "  --window-lb-skip 0|1     skip windows already at their per-cell minimum (exact)\n"
                 "  --phases 0|1             one [phases] summary line on stderr (1)\n"
                 "  --window-threads MODE    auto (rate DP: available CPUs, up to 16), threads (follow --threads), or 1..64\n"
                 "  --print-options         print resolved settings to stderr; continue the normal run\n"
                 "  --max-check              compare complete maximum-displacement move traces\n"
                 "  --max-complete -1|0|1    automatic SA prediction / off / on (default -1)\n"
                 "  --polish-spatial 0|1     slide/relocate in row and x order (1)\n"
                 "  --polish-row-reuse 0|1   maintain row index across polish substeps\n"
                 "  --polish-final-check 0|1 accumulate exact move deltas, verify at polish exit\n"
                 "  --nswap-adaptive 0|1     polish-only: use completed-pass improvement rates (1)\n"
                 "  --nswap-stop MODE       polish-only stopping: fraction (default), rate, first\n"
                 "  --nswap-gain R / --nswap-first-ratio R   time-free completed-pass thresholds (.02 / .06)\n"
                 "  --nswap-pass-stats 0|1   optional per-pass summary from existing gains (0)\n"
                 "  --nswap-rate R          relative HPWL gain/second threshold (0.01)\n"
                 "  --nswap-full-budget 0|1 half-time cap / full remaining cap (1)\n"
                 "  --complete-neutral-budget S  complete-max SA path: replace chain allowance with swaps (0)\n"
                 "  --sa-cold-reorder P     final 30%% steps reorder probability (-1 disables)\n"
                 "  --nswap-time S           displacement-neutral HPWL swaps using unused Stage 1 time, at most 2 s\n"
                 "                           (default limit 10 %% of --stage1-time + 10; 0 disables)\n"
                 "  --stage-hpwl             measurement only: print the HPWL after every Stage 1 step and DP pass\n"
                 "  --nswap-legal-in         measurement only: with --legal-in, run the swaps on that placement first\n"
                 "options (measurement only):\n"
                 "  --trace-dp               print every pass of the displacement window DP\n"
                 "  --chain-split T E D      chain phase: shares of the time left for target windows /\n"
                 "                          ejection chains / window DP (0.3 0.75 0.5)\n"
                 "  --stage2-budget SA P    with --legal-in: replay production SA/polish seconds and starvation policy\n"
                 "  --legal-in PL            Stage 2 only: start from this legal placement (a dp OUTPUT_legal.pl\n"
                 "                          of the same design); Stage 1 is skipped\n"
                 "options (more):\n"
                 "  --sa-starved A          parallel SA: if its time limit buys fewer than A attempts per cell, it\n"
                 "                          keeps 10 %% of the limit and the polish gets the rest (100; 0 = off;\n"
                 "                          off by default when --time is given)\n"
                 "options (more):\n"
                 "  --extra-time S          budget of the extra Stage 1 steps (chains + cap search);\n"
                 "                          default: 12 s (<= 2000 cells) or 6 s; 16 = max displacement first (see README)\n"
                 "  --cap-time S            cap search budget (default: what the chains leave of --extra-time; 0 disables)\n"
                 "  --total-ratio R         cap search: maximum total displacement / total before it (1.0048)\n"
                 "  --legacy-global         the older global cap repair instead of the cap search\n"
                 "options (chains and cap repair):\n"
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
                 "  --threads N             workers for pipelines, SA and window DP (0 = auto: 8)\n"
                 "  --deadline S            whole run (read, both stages, write) ends by S seconds from start (50);\n"
                 "                          the budgets below default to shares of it; 0 = no deadline (fixed budgets)\n"
                 "  --stage1-time S         Stage 1 refinement budget, seconds from start (0.3 x deadline; 90 without)\n"
                 "  --time S                SA time limit in seconds (what is left before the polish; 60 without)\n"
                 "  --polish-time S         post-SA polish budget in seconds (0.1 x deadline; 30 without)\n"
                 "  --sync M                parallel SA: bands merge every M attempts per cell (1)\n"
                 "options (window DP and cold SA):\n"
                 "  --window-passes N       window DP sweeps (1000 = until converged; 0 disables)\n"
                 "  --hpwl-window-passes N  joint two-row HPWL proposal sweeps (2)\n"
                 "  --order-passes N        exact fixed-gap HPWL ordering sweeps (2)\n"
                 "  --window-cells N        cells per row of a two-row window (4, 5 if <= 1500 cells; 1..5)\n"
                 "  --window-sites N        maximum span per row in sites (256)\n"
                 "  --rowopt P L / --ism P K  cold SA macro moves (0.01 12 each)\n"
                 "  --polish-from F / --finish N  defaults 0.5 / 4\n"
                 "  --no-pareto       disable new constrained bottleneck matching\n"
                 "  --pareto-passes N displacement matching sweeps (default 2)\n"
                 "  --no-polish       disable new deterministic HPWL descent\n"
                 "  --polish-passes N HPWL descent sweeps (default 20: until no gain or out of time)\n"
                 "  --group-size N    assignment size, 2..128 (default 32)\n"
                 "  --min-moves N     minimum SA attempts (default 5000000)\n"
                 "  --seed N          random seed for SA (default 1)\n"
                 "  --effort K        SA moves per cell (default 5000; default total capped at 60000000)\n"
                 "  --trace FILE      write SA trace (iteration,current,best,T,window,accept) as CSV\n"
                 "  --legalizer NAME  abacus (default) or tetris\n"
                 "  --own-cost        abacus row choice by the new cell's displacement only\n"
                 "  --no-refine       skip the displacement refinement after legalization\n"
                 "  --no-evict        refinement without the two-step (evict) move\n"
                 "  --no-matching     refinement without same-width matching\n"
                 "  --no-rowopt       refinement without exact per-row re-placement\n"
                 "  --spread MODE     flow-based spreading before Abacus: auto (default), on, off\n"
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
                 "  --reorder P       probability of a local-reordering move (0.1)\n"
                 "  --near K          if a move's target fails, try K cells/gaps on each side (auto: 3 if dense)\n"
                 "  --maxwin F        largest range window, fraction of the chip (0.1)\n");
}

double seconds(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

int run(int argc, char** argv) {
    if (argc < 3) { usage(); return 1; }
    std::string input = argv[1], output = argv[2];
    LegalizeOptions lopt;
    lopt.abacusFast = 3; lopt.maxFast = 7; // A6: bounded endpoint scans + direct evict rooms; small gate below
    lopt.maxThreads = 16;                 // A6: max/matching workers only, preserve the SA/window budgets
    lopt.maxPolicyMinSeconds = 1.0;       // E1: .25/1/2 s comparison; small/complete paths restore .25 below
    lopt.releaseMaxScratch = true;        // M1: retain nested reuse, release only after the whole phase
    lopt.tieBand = 0.01;
    bool chainProbe = true;                // chain phase only while its probe pays the window DP's rate
    bool nswapReserve = false;             // R1: controlled trials trade higher total displacement for mixed HPWL gains
    bool nswapSpatial = true;              // L2 same legal-in / 32 s: nswap passes 1.5x faster, final HPWL -0.96 %
    bool phasesLine = true;                // one [phases] line on stderr
    WindowThreadSetting mainWindowThreads; // explicit auto / inherited / count modes (0 is an auto alias).
    bool printOptions = false;
    bool releaseStage1Memo = true;         // M1: no remaining consumer after cap search.
    bool releaseTrim = true;              // M1: measured lower peak RSS after all Stage 1 workers join (glibc).
    SAOptions sopt;
    OptimizeOptions oopt;
    WindowOptions wopt;
    wopt.incrementalHpwl = true;   // bit-identical HPWL of the rate rule
    wopt.lowerBoundSkip = true;    // exact skip of windows at their per-cell minimum
    ChainOptions copt;
    int chainRounds = 2;
    double chainTime = -1;                // -1 = 25 % of the extra budget, at most 3 s
    double globalTime = 16, totalRatio = 1.0048;
    double extraTime = -1, capTime = -1;  // -1 = automatic (see usage)
    bool legacyGlobal = false;
    bool targetWindows = true;
    double splitTarget = 0.3, splitChains = 0.75, splitDP = 0.5;   // --chain-split (shares of what is left)
    wopt.passes = 1000;   // until converged
    int hpwlWindowPasses=2;
    int orderPasses=2;
    bool runSA = true;
    std::string legalIn;   // stage 2 bench: --legal-in
    double stage1Time = 90;   // Stage 1 refinement budget (seconds since start)
    bool stage2BudgetSet = false;
    double replaySA = 0, replayPolish = 0;
    double polishTime = 30;   // post-SA deterministic polish budget (seconds)
    // the 10/28 rules give every case 60 s in all (I/O included). All budgets are
    // shares of one deadline, so no combination of defaults can add up past it.
    double deadline = 50;
    bool stage1TimeSet = false, polishTimeSet = false;
    double stage1Tail = 10;   // pareto + window DP run until stage1Time + stage1Tail
    bool windowCellsSet = false;
    bool extraTimeSet = false, saTimeSet = false, starvedSet = false;
    double nswapTime = -1;      // -1 = 10 % of the Stage 1 budget (stage1Time + 10)
    bool stageHPWL = false, nswapLegalIn = false;
    bool abacusCheckOnly = false;   // measurement only
    bool maxCheckOnly = false;
    bool maxFastExplicit = false;
    bool maxThreadsExplicit = false;
    bool windowScratchExplicit = false;
    int maxCompleteMode = -1; // -1 predicts SA vs polish-only after read; 0/1 explicit
    bool fullNswapBudget = true;
    double completeNeutralBudget = 0; // opt-in: reassign existing extra allowance, never extend it
    bool windowRateScope = false;
    bool maxAfterAbacus = true;              // the max pass budget after Abacus (LegalizeOptions::maxUntil)
    bool rateWindow = true;           // rate-scored main displacement DP
    bool predictSched = true;            // Stage 1 schedule by predicted Stage 2 and measured gains
    bool polishOnly = false;
    bool adaptiveNswap = true, nswapTimeSet = false;
    std::string nswapStop = "fraction";  // N2: completed-pass gain is independent of machine speed
    double nswapGain = 0.02, nswapFirstRatio = 0.06;
    bool nswapPassStats = false;
    double nswapRate = 0.01; // measured initial polish yield; experimental, independently ablated
    double windowRate = 0.005, windowHpwlSA = 0.5, windowHpwlPolish = 1.0;   // exp_tune: 0.01 cost L1 +1.4 % total
    // defaults (--early-exit / --box-cache / --polish-partial 0 turn them off)
    oopt.spatialTraversal = true; // same legal-in / equal-time L2 ablation
    oopt.earlyExit = oopt.partialPass = true;   // box cache: exact but no faster (exp_D_equiv), off
    // defaults: a parallel SA projected below 1000 attempts per cell starts at T0 x (n/1000)^0.5
    // (>= 0.2) and merges every 2 attempts per cell (--sa-auto 0 0 0 / --sa-sync-auto 0 turn it off)
    sopt.autoPerCell = 1000; sopt.autoExp = 0.5; sopt.autoMinRatio = 0.2; sopt.autoSync = true;
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
        else if (a == "--extra-time") { extraTime = std::stod(next()); extraTimeSet = true; }
        else if (a == "--cap-time") capTime = std::stod(next());
        else if (a == "--legacy-global") legacyGlobal = true;
        else if (a == "--chain-rounds") chainRounds = std::stoi(next());
        else if (a == "--chain-depth") copt.depth = std::stoi(next());
        else if (a == "--chain-branches") copt.branches = std::stoi(next());
        else if (a == "--chain-expansions") copt.expansions = std::stoi(next());
        else if (a == "--chain-roots") copt.roots = std::stoi(next());
        else if (a == "--chain-blockers") copt.maxBlockers = std::stoi(next());
        else if (a == "--no-target-windows") targetWindows = false;
        else if (a == "--chain-split") { splitTarget = std::stod(next()); splitChains = std::stod(next()); splitDP = std::stod(next()); }
        else if (a == "--trace-dp") windowTrace = 1;
        else if (a == "--legal-in") legalIn = next();
        else if (a == "--stage2-budget") {
            replaySA = std::stod(next()); replayPolish = std::stod(next()); stage2BudgetSet = true;
        }
        else if (a == "--nswap-time") { nswapTime = std::stod(next()); nswapTimeSet = true; }
        else if (a == "--nswap-adaptive") adaptiveNswap = std::stoi(next()) != 0;
        else if (a == "--nswap-rate") nswapRate = std::stod(next());
        else if (a == "--nswap-stop") nswapStop = next();
        else if (a == "--nswap-gain") nswapGain = std::stod(next());
        else if (a == "--nswap-first-ratio") nswapFirstRatio = std::stod(next());
        else if (a == "--nswap-pass-stats") nswapPassStats = std::stoi(next()) != 0;
        else if (a == "--stage-hpwl") { stageHPWL = true; windowTraceHPWL = 1; windowTrace = 1; }
        else if (a == "--nswap-legal-in") nswapLegalIn = true;
        else if (a == "--abacus-fast") lopt.abacusFast = std::stoi(next());
        else if (a == "--max-share") lopt.maxShare = std::stod(next());
        else if (a == "--max-rate") lopt.maxRate = std::stod(next());
        else if (a == "--max-prune") lopt.maxPrune = std::stoi(next()) != 0;
        else if (a == "--max-fast") { lopt.maxFast = std::stoi(next()); maxFastExplicit = true; }
        else if (a == "--max-threads") { lopt.maxThreads = std::stoi(next()); maxThreadsExplicit = true; }
        else if (a == "--max-release") lopt.releaseMaxScratch = std::stoi(next()) != 0;
        else if (a == "--stage1-memo-release") releaseStage1Memo = std::stoi(next()) != 0;
        else if (a == "--release-trim") releaseTrim = std::stoi(next()) != 0;
        else if (a == "--max-policy-min-seconds") lopt.maxPolicyMinSeconds = std::stod(next());
        else if (a == "--max-complete") maxCompleteMode = std::stoi(next());
        else if (a == "--nswap-full-budget") fullNswapBudget = std::stoi(next()) != 0;
        else if (a == "--complete-neutral-budget") completeNeutralBudget = std::stod(next());
        else if (a == "--sa-cold-reorder") sopt.coldReorder = std::stod(next());
        else if (a == "--max-check") maxCheckOnly = true;
        else if (a == "--tie-band") lopt.tieBand = std::stod(next());
        else if (a == "--chain-probe") chainProbe = std::stoi(next()) != 0;
        else if (a == "--nswap-reserve") nswapReserve = std::stoi(next()) != 0;
        else if (a == "--nswap-spatial") nswapSpatial = std::stoi(next()) != 0;
        else if (a == "--window-inc-hpwl") wopt.incrementalHpwl = std::stoi(next()) != 0;
        else if (a == "--window-hpwl-check") wopt.checkHpwl = true;
        else if (a == "--window-lb-skip") wopt.lowerBoundSkip = std::stoi(next()) != 0;
        else if (a == "--phases") phasesLine = std::stoi(next()) != 0;
        else if (a == "--window-threads") mainWindowThreads.parse(next());
        else if (a == "--print-options") printOptions = true;
        else if (a == "--legacy-stage1") {   // every change off
            nswapStop = "rate";
            nswapGain = 0.02; nswapFirstRatio = 0.06; nswapPassStats = false;
            lopt.maxFast = 3; lopt.tieBand = 0; chainProbe = nswapReserve = nswapSpatial = false;
            wopt.incrementalHpwl = wopt.lowerBoundSkip = false;
            mainWindowThreads.parse("threads");
            maxFastExplicit = false;
            lopt.maxThreads = 0;
            maxThreadsExplicit = false;
            lopt.releaseMaxScratch = false;
            releaseStage1Memo = false;
            releaseTrim = false;
            lopt.maxPolicyMinSeconds = 0.25;
        }
        else if (a == "--max-check-moves") lopt.maxCheckMoves = std::stol(next());
        else if (a == "--max-check-matching") lopt.maxCheckMatching = true;
        else if (a == "--max-check-modes") {
            std::string list = next();
            for (size_t pos = 0; pos < list.size();) {
                size_t end = list.find(',', pos);
                if (end == std::string::npos) end = list.size();
                const std::string item = list.substr(pos, end - pos);
                const size_t colon = item.find(':');
                if (colon == std::string::npos) { std::fprintf(stderr, "error: --max-check-modes F:T,...\n"); return 1; }
                lopt.maxCheckModes.push_back({std::stoi(item.substr(0, colon)), std::stoi(item.substr(colon + 1))});
                pos = end + 1;
            }
        }
        else if (a == "--max-floor") lopt.maxFloor = std::stod(next());
        else if (a == "--max-tail") lopt.maxTail = std::stod(next());
        else if (a == "--max-after-abacus") maxAfterAbacus = std::stoi(next()) != 0;
        else if (a == "--early-exit") oopt.earlyExit = std::stoi(next()) != 0;
        else if (a == "--polish-spatial") oopt.spatialTraversal = std::stoi(next()) != 0;
        else if (a == "--polish-row-reuse") oopt.reuseRowMap = std::stoi(next()) != 0;
        else if (a == "--polish-final-check") oopt.finalCheck = std::stoi(next()) != 0;
        else if (a == "--legacy-sched") {   // every change off; later options turn single ones on
            // Preserve the existing compatibility switch when defaults are enabled.
            lopt.maxFast = 0; maxFastExplicit = true; maxCompleteMode = 0;
            lopt.maxThreads = 0; lopt.releaseMaxScratch = false; lopt.maxPolicyMinSeconds = 0.25;
            maxThreadsExplicit = false;
            releaseStage1Memo = false;
            releaseTrim = false;
            windowRateScope = adaptiveNswap = fullNswapBudget = false;
            wopt.reuseScratch = false; windowScratchExplicit = true; completeNeutralBudget = 0;
            wopt.rowKernel = 0;
            oopt.spatialTraversal = oopt.reuseRowMap = oopt.finalCheck = false;
            sopt.coldReorder = -1;
            lopt.abacusFast = 0; maxAfterAbacus = false; predictSched = false; rateWindow = false;
            oopt.earlyExit = oopt.boxCache = oopt.partialPass = false;
            sopt.autoPerCell = 0; sopt.tStartRatio = 1; sopt.autoSync = false;
        }
        else if (a == "--rate-window") rateWindow = std::stoi(next()) != 0;
        else if (a == "--predict-sched") predictSched = std::stoi(next()) != 0;
        else if (a == "--sa-threads") sopt.forceThreads = std::stoi(next());
        else if (a == "--sa-tstart") sopt.tStartRatio = std::stod(next());
        else if (a == "--sa-sync-auto") sopt.autoSync = std::stoi(next()) != 0;
        else if (a == "--sa-auto") { sopt.autoPerCell = std::stod(next()); sopt.autoExp = std::stod(next()); sopt.autoMinRatio = std::stod(next()); }
        else if (a == "--window-rate-scope") windowRateScope = std::stoi(next()) != 0;
        else if (a == "--window-scratch") { wopt.reuseScratch = std::stoi(next()) != 0; windowScratchExplicit = true; }
        else if (a == "--window-kernel") wopt.rowKernel = std::stoi(next());
        else if (a == "--window-rate") windowRate = std::stod(next());
        else if (a == "--window-hpwl") { windowHpwlSA = std::stod(next()); windowHpwlPolish = std::stod(next()); }
        else if (a == "--polish-partial") oopt.partialPass = std::stoi(next()) != 0;
        else if (a == "--box-cache") oopt.boxCache = std::stoi(next()) != 0;
        else if (a == "--abacus-check") abacusCheckOnly = true;
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
        else if (a == "--time") { sopt.timeLimit = std::atof(next()); saTimeSet = true; }
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
        else if (a == "--stage1-time") { stage1Time = std::atof(next()); stage1TimeSet = true; }
        else if (a == "--deadline") deadline = std::atof(next());
        else if (a == "--threads") sopt.threads = std::atoi(next());
        else if (a == "--polish-time") { polishTime = std::atof(next()); polishTimeSet = true; }
        else if (a == "--sync") sopt.syncMoves = std::atof(next());
        else if (a == "--sa-starved") { sopt.starvedAttemptsPerCell = std::atof(next()); starvedSet = true; }
        else if (a == "-v") {
            lopt.verbose = sopt.verbose = oopt.verbose = copt.verbose = true;
            wopt.verbose = 1;
            stageHPWL = true;
            windowTraceHPWL = windowTrace = 1;
        }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 1; }
    }

    if (!(sopt.pGlobalSwap >= 0 && sopt.pGlobalSwap <= 0.5) ||
        !std::isfinite(globalTime) || globalTime < 0 || !std::isfinite(totalRatio) || totalRatio < 1 ||
        !(splitTarget >= 0 && splitTarget <= 1) || !(splitChains >= 0 && splitChains <= 1) || !(splitDP >= 0 && splitDP <= 1) ||
        !std::isfinite(chainTime) || chainTime < -1 || chainRounds < 0 || chainRounds > 20 ||
        !std::isfinite(nswapTime) || nswapTime < -1 || !std::isfinite(nswapRate) || nswapRate < 0 ||
        !std::isfinite(completeNeutralBudget) || completeNeutralBudget < 0 || wopt.rowKernel < 0 || wopt.rowKernel > 2 ||
        !std::isfinite(extraTime) || extraTime < -1 || !std::isfinite(capTime) || capTime < -1 ||
        copt.depth < 1 || copt.depth > 32 || copt.branches < 1 || copt.branches > 128 ||
        copt.expansions < 1 || copt.expansions > 100000 || copt.roots < 1 || copt.maxBlockers < 1 || copt.maxBlockers > 2 ||
        !std::isfinite(stage1Time) || stage1Time < 0 || !std::isfinite(polishTime) || polishTime < 0 ||
        !std::isfinite(deadline) || deadline < 0 || !(lopt.maxShare >= 0 && lopt.maxShare <= 1) ||
        !(lopt.maxRate >= 0 && lopt.maxRate < 10) ||
        sopt.threads < 0 || sopt.threads > 64 || !std::isfinite(sopt.syncMoves) || sopt.syncMoves <= 0 ||
        !std::isfinite(sopt.starvedAttemptsPerCell) || sopt.starvedAttemptsPerCell < 0 ||
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
        (lopt.method != "abacus" && lopt.method != "tetris") || lopt.abacusFast < 0 || lopt.abacusFast > 3 ||
        lopt.maxFast < 0 || lopt.maxFast > 7 || maxCompleteMode < -1 || maxCompleteMode > 1 ||
        lopt.maxThreads < 0 || lopt.maxThreads > 64 || !std::isfinite(lopt.maxPolicyMinSeconds) || lopt.maxPolicyMinSeconds < 0.25 ||
        !std::isfinite(sopt.coldReorder) || (sopt.coldReorder != -1 && (sopt.coldReorder < 0 || sopt.coldReorder > 1))) {
        std::fprintf(stderr, "error: invalid option value\n"); return 1;
    }
    if ((nswapStop != "rate" && nswapStop != "fraction" && nswapStop != "first") ||
            !std::isfinite(nswapGain) || nswapGain < 0 || nswapGain > 1 ||
            !std::isfinite(nswapFirstRatio) || nswapFirstRatio < 0 || nswapFirstRatio > 1) {
        std::fprintf(stderr, "error: invalid nswap stopping policy\n"); return 1;
    }
    if (stage2BudgetSet && (legalIn.empty() || saTimeSet || polishTimeSet ||
            !std::isfinite(replaySA) || replaySA < 0 || !std::isfinite(replayPolish) || replayPolish < 0)) {
        std::fprintf(stderr, "error: --stage2-budget requires --legal-in and nonnegative finite budgets; conflicts with --time/--polish-time\n");
        return 1;
    }
    lopt.windowDP = wopt.passes > 0;
    lopt.windowCells = wopt.perRow;
    // Stage 1 shares of the deadline; the Stage 2 shares are set when Stage 2 starts
    if (deadline > 0) {
        if (!stage1TimeSet) stage1Time = 0.3 * deadline;
        stage1Tail = 0.1 * deadline;
        if (!polishTimeSet) polishTime = 0.1 * deadline;
    }
    lopt.until = stage1Time;
    // the max pass may run until the Stage 1 refinement ends (main's base stage1Until)
    if (maxAfterAbacus && (deadline > 0 || maxCompleteMode == 1))
        lopt.maxUntil = stage1Time + stage1Tail; // explicit complete mode also works in deadline-free diagnostics
    auto t0 = std::chrono::steady_clock::now();
    PlaceDB db;
    std::string err;
    if (!db.read(input, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    double readSeconds = wallSeconds();
    const long hwmRead = phasesLine ? readVmHwmKiB() : -1;
    long hwmLegal = -1, hwmPareto = -1, hwmWindow = -1, hwmExtra = -1;
    long hwmNswap = -1, hwmStage1 = -1, hwmSA = -1, hwmPolish = -1, hwmWrite = -1;
    lopt.collectHwm = phasesLine;
    lopt.threads = sopt.threads > 0 ? sopt.threads : 8;
    if (abacusCheckOnly || maxCheckOnly) {
        if (printOptions) printLegalizeOptions(lopt, "diagnostic-legal");
        return (abacusCheckOnly ? abacusCheck(db, lopt) : maxRefineCheck(db, lopt)) ? 0 : 2;
    }
    // everything must be done (outputs written) by `deadline`; writing the two .pl
    // files and copying the four inputs took 0.45 s for 1M cells (2026-10-30), so the
    // reserve is a generous 0.5 s + 1 s per million nodes.
    const double endBy = deadline > 0 ? deadline - (0.5 + 1e-6 * db.nodes.size()) : 1e30;
    // Preserve the existing timed plain/spread competition when selecting defaults.
    // This is the same structural regime used by legalize, not a benchmark-name check.
    const bool legacyDualPipeline = lopt.method != "tetris" && db.uniformSites &&
        lopt.spread == "auto" && db.movable.size() <= 60000;
    if (legacyDualPipeline && !maxFastExplicit) lopt.maxFast = 0;
    if (legacyDualPipeline && !maxThreadsExplicit) lopt.maxThreads = 0;
    // the max pass team spins between steps, so the default never exceeds the CPUs this
    // process may use. Its
    // moves do not depend on the worker count (--max-check 1/8/16), so 16 CPUs are unchanged.
    const int cpusAvailable = availableCpus();
    if (!maxThreadsExplicit && lopt.maxThreads > cpusAvailable) lopt.maxThreads = cpusAvailable;
    if (!windowScratchExplicit) wopt.reuseScratch = !legacyDualPipeline;
    // A preliminary forecast cannot place the Stage 1 end before input reading has finished.
    lopt.maxComplete = maxCompleteMode == 1 || (maxCompleteMode == -1 && !legacyDualPipeline && deadline > 0 &&
        endBy - std::max(stage1Time + stage1Tail, readSeconds) - 0.06 * deadline >= 25.0 * readSeconds);
    // E1 applies only to the legacy, single-pipeline path; preserve small/complete schedules.
    if (legacyDualPipeline || lopt.maxComplete) lopt.maxPolicyMinSeconds = 0.25;
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
    // every optional step is guarded. If one throws or breaks its invariant,
    // the placement from before that step is kept and the run goes on: a
    // crash or a missing output scores zero, a skipped step only costs quality.
    auto guarded = [&](const char* name, Placement& p, const std::function<void()>& step,
                       const std::function<bool()>& ok) {
        return guardedStep(name, p, step, ok);
    };

    // window DP threads: all of them share one placement (no copies), so
    // unlike the SA they are not limited by the design size
    // 8 workers whatever the core count. On 2 / 4 P-cores
    // 8 workers did more than 2 / 4: SA +28 % attempts and HPWL -1.6 % (h08, 2 seeds), 200k-cell
    // Stage 1 total -2 / -3 %; the pool blocks instead of spinning, so idle
    // workers cost nothing. Machines with >= 8 hardware threads are unchanged.
    wopt.threads = lopt.threads = sopt.threads > 0 ? sopt.threads : 8;
    // small designs get bigger windows (10 / 12 cells instead of 8 / 9):
    // they cost about 4x, which is nothing there, and reach further.
    if (!windowCellsSet && db.movable.size() <= 1500 && (db.utilization() >= 0.4 || db.movable.size() < 100)) {
        wopt.perRow = lopt.windowCells = 5;
        wopt.perRow3 = lopt.windowCells3 = 4;
    }
    // ---- Stage 1
    // -v / --stage-hpwl costs one exact HPWL sum per step, inside the same deadlines.
    auto stageH = [&](const char* step, const Placement& p) {
        if (!stageHPWL) return;
        const DispStats d = displacement(db, p);
        std::fprintf(stderr, "[stage-hpwl] %-20s hpwl %.6e  total %.6e  max %.4f  %.2fs\n", step, totalHPWL(db, p), d.total,
                     d.max, wallSeconds());
    };
    // displacement-neutral HPWL swaps (nswap.hpp) for at most `budget` seconds; returns the seconds used
    if (nswapTime < 0) {
        nswapTime = 0.1 * (stage1Time + stage1Tail);
        nswapTimeSet = false; // an explicit automatic sentinel is not a duration cap
    }
    if (printOptions) {
        printLegalizeOptions(lopt); printSAOptions(sopt); printOptimizeOptions(oopt);
        printWindowOptions(wopt); printChainOptions(copt);
        OptionLine line("main");
#define MAIN_OPTION(name) line.add(#name, name)
        MAIN_OPTION(chainProbe); MAIN_OPTION(nswapReserve); MAIN_OPTION(nswapSpatial);
        MAIN_OPTION(phasesLine); MAIN_OPTION(chainRounds); MAIN_OPTION(chainTime);
        MAIN_OPTION(releaseStage1Memo);
        MAIN_OPTION(releaseTrim);
        line.add("releaseTrimSupported", releasedMemoryTrimSupported());
        line.add("releaseTrimEffective", releaseTrim && releaseStage1Memo && releasedMemoryTrimSupported());
        MAIN_OPTION(globalTime); MAIN_OPTION(totalRatio); MAIN_OPTION(extraTime); MAIN_OPTION(capTime);
        MAIN_OPTION(legacyGlobal); MAIN_OPTION(targetWindows); MAIN_OPTION(splitTarget);
        MAIN_OPTION(splitChains); MAIN_OPTION(splitDP); MAIN_OPTION(hpwlWindowPasses); MAIN_OPTION(orderPasses);
        MAIN_OPTION(runSA); MAIN_OPTION(legalIn); MAIN_OPTION(stage1Time); MAIN_OPTION(stage1Tail);
        MAIN_OPTION(stage2BudgetSet); MAIN_OPTION(replaySA); MAIN_OPTION(replayPolish);
        MAIN_OPTION(polishTime); MAIN_OPTION(deadline); MAIN_OPTION(stage1TimeSet); MAIN_OPTION(polishTimeSet);
        MAIN_OPTION(windowCellsSet); MAIN_OPTION(extraTimeSet); MAIN_OPTION(saTimeSet); MAIN_OPTION(starvedSet);
        MAIN_OPTION(nswapTime); MAIN_OPTION(nswapTimeSet); MAIN_OPTION(stageHPWL); MAIN_OPTION(nswapLegalIn);
        MAIN_OPTION(abacusCheckOnly); MAIN_OPTION(maxCheckOnly); MAIN_OPTION(maxFastExplicit);
        MAIN_OPTION(maxThreadsExplicit);
        MAIN_OPTION(windowScratchExplicit); MAIN_OPTION(maxCompleteMode); MAIN_OPTION(fullNswapBudget);
        MAIN_OPTION(completeNeutralBudget); MAIN_OPTION(windowRateScope); MAIN_OPTION(maxAfterAbacus);
        MAIN_OPTION(rateWindow); MAIN_OPTION(predictSched); MAIN_OPTION(adaptiveNswap); MAIN_OPTION(nswapRate);
        MAIN_OPTION(nswapStop); MAIN_OPTION(nswapGain); MAIN_OPTION(nswapFirstRatio); MAIN_OPTION(nswapPassStats);
        MAIN_OPTION(windowRate); MAIN_OPTION(windowHpwlSA); MAIN_OPTION(windowHpwlPolish);
        MAIN_OPTION(legacyDualPipeline); MAIN_OPTION(endBy); MAIN_OPTION(windowTrace); MAIN_OPTION(windowTraceHPWL);
#undef MAIN_OPTION
        line.add("availableCpus", cpusAvailable);
        line.add("mainWindowThreadsMode", mainWindowThreads.name());
        line.add("mainWindowThreadsCount", mainWindowThreads.count);
        line.add("mainWindowThreadsNonRate", mainWindowThreads.resolve(wopt.threads, availableCpus(), false));
        line.add("mainWindowThreadsRate", mainWindowThreads.resolve(wopt.threads, availableCpus(), true));
    }
    double dtParetoPass = 0.0, dtNswapPass = 0.0;
    NeutralSwapResult lastNswap;
    double lastNswapSeconds = 0;
    double lastNswapAllowance = 0;
    double lastNswapPrelude = 0;
    auto neutralSwaps = [&](Placement& p, double budget, double absoluteUntil = -1) {
        const double t = wallSeconds();
        if (budget <= 0 || db.movable.empty()) return 0.0;
        NeutralSwapOptions nopt;
        nopt.threads = wopt.threads;
        nopt.spatialOrder = nswapSpatial;
        nopt.verbose = lopt.verbose;
        // Diagnostics between budget selection and this call must not move the absolute cutoff.
        // Keep explicit durations unchanged when there is no overall deadline.
        nopt.until = deadline > 0 ? std::min(t + budget, endBy) : t + budget;
        if (absoluteUntil >= 0) nopt.until = std::min(nopt.until, absoluteUntil);
        if (wallSeconds() >= nopt.until) return 0.0;
        lastNswapAllowance = std::max(0.0, nopt.until - t);
        const DispStats before = displacement(db, p);
        const double hpwlBefore = totalHPWL(db, p);
        if (adaptiveNswap && polishOnly) {
            if (nswapStop == "rate") nopt.minRelativeRate = nswapRate;
            else if (nswapStop == "fraction") nopt.minFractionalGain = nswapGain;
            else nopt.quiet = nswapFirstRatio;
            nopt.referenceHPWL = hpwlBefore;
            nopt.capturePassStats = nswapPassStats;
        }
        if (printOptions) {
            OptionLine line("nswap-runtime");
            line.add("threads", nopt.threads); line.add("spatialOrder", nopt.spatialOrder);
            line.add("until", nopt.until); line.add("budget", budget);
            line.add("minFractionalGain", nopt.minFractionalGain); line.add("capturePassStats", nopt.capturePassStats);
            line.add("minRelativeRate", nopt.minRelativeRate); line.add("referenceHPWL", nopt.referenceHPWL);
            line.add("passes", nopt.passes); line.add("rowsAround", nopt.rowsAround);
            line.add("cellsAround", nopt.cellsAround); line.add("chunk", nopt.chunk);
            line.add("quiet", nopt.quiet); line.add("verbose", nopt.verbose);
        }
        NeutralSwapResult r;
        guarded("neutral swaps", p, [&] {
            lastNswapPrelude = wallSeconds() - t;
            r = neutralSwap(db, p, nopt);
        }, [&] {
            const DispStats after = displacement(db, p);
            // a swap counts as neutral at an exact change <= 1e-9 (equal L1 costs differ by rounding);
            // the relative term covers that and the rounding of a 1M-term double sum
            return checkLegal(db, p).empty() && after.total <= before.total + 1e-5 + 1e-11 * before.total &&
                   after.max <= before.max + 1e-6 && totalHPWL(db, p) <= hpwlBefore + 1e-5;
        });
        const double used = wallSeconds() - t;
        if (r.passes > 0) dtNswapPass = used / r.passes;
        lastNswap = r;
        lastNswapSeconds = used;
        for (const auto& pass : r.passStats)
            std::fprintf(stderr, "[nswap-pass] pass=%d complete=%d visited=%ld swaps=%ld seconds=%.9f before=%.17g after=%.17g gain=%.17g fraction=%.9f first=%.9f rate=%.9f\n",
                         pass.pass, int(pass.complete), pass.visited, pass.swaps, pass.seconds,
                         pass.hpwlBefore, pass.hpwlAfter, pass.hpwlGain, pass.fractionalGain,
                         pass.firstGainRatio, pass.relativeRate);
        if (phasesLine) hwmNswap = readVmHwmKiB();
        if (lopt.verbose)
            std::fprintf(stderr, "[nswap] budget %.3f used %.3f: %d passes, %ld swaps (%ld proposals, %ld neutral of %ld candidates), "
                         "HPWL %+.6e, displacement %+.6e%s\n", budget, used, r.passes, r.swaps, r.proposals, r.neutral,
                         r.candidates, r.hpwlGain, r.dispChange, r.cut ? " (deadline)" : "");
        return used;
    };
    // what the [phases] line reports
    struct Phases {
        double pareto = 0, extra = 0, sa = 0, polish = 0;
        int winPasses = 0;
        double winSeconds = 0, winRate = -1;
        double winFirst = 0, winLimit = 0, reserveEstimate = 0, reserveBudgetIncrease = 0;
        bool reserveEligible = false, reserveTimeStop = false;
        std::string winStop = "not-run", chain = "not-run";
        long winSkips = 0;
    } phases;
    Placement legal;
    // one memo for every displacement DP after legalization, so the DPs
    // after the chains and the cap search only re-solve windows that changed
    // it starts in the final rounds of the legalization (legalize fills it)
    WindowMemo stage1Memo;
    LegalizeStats legalizeStats;
    if (!legalIn.empty()) {   // stage 2 bench: the legal placement comes from a file
        if (!readLegalPlacement(db, legalIn, legal, err)) { std::fprintf(stderr, "error: --legal-in: %s\n", err.c_str()); return 1; }
        readSeconds = wallSeconds();
        if (nswapLegalIn) {
            // Explicit zero-SA legal-in runs exercise the same rate policy as a predicted polish-only flow.
            polishOnly = saTimeSet && sopt.timeLimit <= 0;
            double budget = nswapTime;
            if (adaptiveNswap && polishOnly && deadline > 0) {
                budget = (fullNswapBudget ? 1.0 : 0.5) * std::max(0.0, endBy - wallSeconds());
                if (nswapTimeSet) budget = std::min(budget, nswapTime);
            }
            stageH("legal-in", legal); neutralSwaps(legal, budget); stageH("neutral swaps", legal);
        }
    } else {
        if (db.movable.empty()) {legal.seg.assign(db.nodes.size(), -1);legal.site.assign(db.nodes.size(), 0);}
        else {
            bool ok = guarded("legalization", legal, [&] {
                if (!legalize(db, legal, lopt, err, &stage1Memo, &legalizeStats))
                    throw std::runtime_error(err);
            }, [&] { return checkLegal(db, legal).empty(); });
            if (!ok) {   // fall back to plain Abacus without refinement
                std::fprintf(stderr, "warning: legalization failed (%s), retrying without refinement\n", err.c_str());
                stage1Memo = WindowMemo();
                legalizeStats = LegalizeStats();
                LegalizeOptions plain = lopt;
                plain.refineMax = plain.refineTotal = false;
                plain.spread = "off";
                if (!legalize(db, legal, plain, err, nullptr, &legalizeStats)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
            }
        }
        const double afterLegal = wallSeconds();
        if (phasesLine) hwmLegal = readVmHwmKiB();
        const DispStats s1 = displacement(db, legal);
        stageH("legalize", legal);
        auto noWorse = [&] {
            DispStats d = displacement(db, legal);
            return checkLegal(db, legal).empty() && d.total <= s1.total + 1e-5 && d.max <= s1.max + 1e-6;
        };
        // Parallel pipelines save elapsed time, not extra refinement budget.
        // Keep 's matching/guard rescue budget; reserve no time for neutral swaps.
        // The final swaps use only slack under this same adjusted deadline.
        const double baseStage1Until = stage1Time + stage1Tail - legalizeStats.pipelineOverlap;
        const bool stage1Overtime = (deadline > 0 && !stage1TimeSet && afterLegal >= baseStage1Until);
        double stage1Until = baseStage1Until;
        OptimizeOptions paretoOpt = oopt;
        // switched its Stage 1 schedule on one test (legalization ended after the base
        // deadline). With the faster Abacus the 1M-cell L2 no longer passes it and lost the steps that
        // pay most there (c3's +14.8 % max; legal HPWL 2.53e9 instead of 2.11e9). decides by what
        // Stage 2 will do, predicted with the test main applies before Stage 2: a design whose Stage 2
        // cannot afford the SA (polish only) keeps the legal HPWL, so it gets 's long pareto (passes
        // while one lowers the maximum by 0.5 % or the HPWL by 1 %), the post-window pareto and the
        // neutral swaps; otherwise pareto passes go on while they lower the maximum by 0.5 %.
        polishOnly = false;
        if (deadline > 0 && predictSched) {
            const double minSa = 25.0 * readSeconds;
            polishOnly = endBy - std::max(baseStage1Until, afterLegal) - 0.06 * deadline < minSa;
            paretoOpt.minMaxGain = 0.005;
            if (polishOnly) {
                paretoOpt.minHpwlGain = 0.01;
                stage1Until = std::max(stage1Until, afterLegal + 0.35 * std::max(0.0, endBy - afterLegal));
            }
            paretoOpt.paretoPasses = std::max(paretoOpt.paretoPasses, 8);
            if (lopt.verbose)
                std::fprintf(stderr, "[sched] Stage 2 predicted %s; Stage 1 refinement until %.2fs%s\n",
                             polishOnly ? "polish only" : "to anneal", stage1Until, legalizeStats.maxPhase ? ", max phase ran" : "");
        } else if (stage1Overtime) {
            stage1Until = std::max(stage1Until, afterLegal + 0.35 * std::max(0.0, endBy - afterLegal));
            paretoOpt.paretoPasses = std::max(paretoOpt.paretoPasses, 4);
        }
        const bool longPath = (deadline > 0 && predictSched) ? polishOnly : stage1Overtime;   // stage1Overtime
        const double tPareto0 = wallSeconds();
        paretoOpt.until = stage1Until;
        paretoOpt.threads = wopt.threads;
        if (printOptions) printOptimizeOptions(paretoOpt, "pareto-runtime");
        guarded("pareto matching", legal, [&] { paretoDisplacement(db, legal, paretoOpt); }, noWorse);
        if (phasesLine) hwmPareto = readVmHwmKiB();
        const double dtPareto = wallSeconds() - tPareto0;
        phases.pareto = dtPareto;
        if (paretoOpt.paretoPasses > 0) dtParetoPass = dtPareto / paretoOpt.paretoPasses;
        stageH("pareto", legal);
        wopt.guardHPWL = true;
        double winLimit = std::max(1.0, stage1Until - wallSeconds());
        if (longPath && dtParetoPass > 0.0) {
            winLimit = std::min(winLimit, std::max(1.0, dtParetoPass));
        }
        wopt.timeLimit = winLimit;
        // the main displacement DP is scored every pass (window.hpp, WindowOptions::rateStop).
        // hpwlWeight = how much of the legal HPWL survives Stage 2: an annealed design re-shuffles it
        // from a hot start, a polish-only one keeps it. Stage 2 is predicted to anneal by the same
        // test main uses before Stage 2 (minSaBudget below) on the time Stage 1 is planned to leave.
        WindowOptions mainWin = wopt;
        bool mainSaLikely = false;
        // only where the Stage 1 refinement is time-bound (the max pass was cut, legalizeStats.maxPhase):
        // on ibm01 the rollback rule stopped a converging DP and cost total displacement
        if (deadline > 0 && rateWindow && legalizeStats.maxPhase) {
            const double minSa = std::max({25.0 * readSeconds, 8.0 * dtParetoPass});
            const double extraPlan = stage1Overtime ? 0.0 : std::min(extraTime < 0 ? 6.0 : extraTime, 0.06 * deadline);
            const bool saLikely = endBy - std::max(stage1Until, wallSeconds()) - extraPlan >= minSa;
            mainSaLikely = saLikely;
            mainWin.rateStop = windowRate;
            mainWin.ratePerNeighborhood = windowRateScope && lopt.maxComplete;
            mainWin.hpwlWeight = saLikely ? windowHpwlSA : windowHpwlPolish;
            if (lopt.verbose)
                std::fprintf(stderr, "[window-disp] rate stop %.4f%%/s, HPWL weight %.2f (Stage 2 %s)\n", 100 * mainWin.rateStop,
                             mainWin.hpwlWeight, saLikely ? "anneals" : "polish only");
        }
        // Optional reserve: 1.5 x the first DP pass is only a proxy. R1 measured incomplete first
        // nswap passes and increased displacement, so leaves this policy disabled by default.
        const bool reserveNswap = nswap_reserve::eligible(nswapReserve, mainSaLikely, polishOnly, mainWin.rateStop, nswapTime);
        phases.reserveEligible = reserveNswap;
        if (reserveNswap) {
            mainWin.reserveFactor = 1.5;
            mainWin.reserveCap = nswap_reserve::allowanceCap(nswapTime, std::max(0.0, endBy - wallSeconds()));
        }
        // the rate-scored main DP (time-bound designs) runs one band per available CPU, 8 to 16
        // (L1: 84 passes in 7.0 s with 16 bands against 67 in 8.5 s with 8, on 8 cores / 16 threads)
        mainWin.threads = mainWindowThreads.resolve(mainWin.threads, availableCpus(), mainWin.rateStop > 0);
        phases.winLimit = mainWin.timeLimit;
        if (printOptions) printWindowOptions(mainWin, "main-window-runtime");
        guarded("window DP", legal, [&] { windowSweeps(db, legal, mainWin, &stage1Memo); }, noWorse);
        if (phasesLine) hwmWindow = readVmHwmKiB();
        stageH("window DP", legal);
        // the main displacement DP as it ended
        phases.winPasses = stage1Memo.lastPasses;
        phases.winSeconds = stage1Memo.lastSeconds;
        phases.winStop = stage1Memo.lastStop;
        phases.winSkips = stage1Memo.lastBoundSkips;
        const double winRate = phases.winRate = stage1Memo.lastRate;
        const double nswapReserveSeconds = reserveNswap
            ? nswap_reserve::estimate(stage1Memo.firstPassSeconds, mainWin.reserveFactor, mainWin.reserveCap) : 0.0;
        phases.winFirst = stage1Memo.firstPassSeconds;
        phases.reserveEstimate = nswapReserveSeconds;
        phases.reserveTimeStop = reserveNswap && phases.winStop == "time";
        if ((stage1Memo.hpwlGuarded || (longPath && wallSeconds() < stage1Until + 2.0)) && oopt.paretoPasses > 0) {
            const DispStats beforePostPareto = displacement(db, legal);
            const double hpwlBeforePostPareto = totalHPWL(db, legal);
            OptimizeOptions postPareto = oopt;
            postPareto.threads = wopt.threads;
            postPareto.paretoPasses = std::max(6, oopt.paretoPasses * 3);
            if (deadline > 0 && predictSched) { postPareto.minMaxGain = paretoOpt.minMaxGain; postPareto.minHpwlGain = paretoOpt.minHpwlGain; }
            postPareto.until = longPath
                ? std::min(endBy, std::max(stage1Until, wallSeconds() + 1.5))
                : stage1Until;
            guarded("post-window pareto", legal, [&] { paretoDisplacement(db, legal, postPareto); }, [&] {
                DispStats d = displacement(db, legal);
                return checkLegal(db, legal).empty() && d.total <= beforePostPareto.total + 1e-5 &&
                       d.max <= beforePostPareto.max + 1e-6 && totalHPWL(db, legal) <= hpwlBeforePostPareto + 1e-5;
            });
            stageH("post-window pareto", legal);
        }
        // the extra steps get 6 s, or 12 s up to 2000 cells (their SA always
        // finishes its schedule, so Stage 1 time does not cost HPWL there). A larger
        // fixed budget would come straight out of the SA on every design whose SA is
        // cut by the time limit (equal time, 3 seeds: 6 s -0.9 %, 16 s -0.1 %).
        if (extraTime < 0)
            extraTime = db.movable.size() <= 2000 ? 12.0 : 6.0;
        // the extra steps moved the total by 0.03 % on ibm05 while the SA cut HPWL by 16.5 %;
        // under the deadline they get at most 6 % of it (3 s at 50 s), or 0 s when legalization
        // already exceeded baseStage1Until (where extra fails to repair and starves Stage 2).
        if (deadline > 0) {
            if (!extraTimeSet && stage1Overtime && !predictSched) extraTime = 0.0;
            else extraTime = std::min(extraTime, 0.06 * deadline);
        }
        if (chainTime < 0) chainTime = std::min(3.0, 0.25 * extraTime);
        // after the max phase the cap search found nothing it could accept on L1 / L2 (2 s each,
        // windows over the total budget or unresolved); only the chains (L2: max -0.9 % in 0.75 s) stay
        bool skipCap = false;
        if (deadline > 0 && predictSched && !extraTimeSet && legalizeStats.maxPhase) {
            extraTime = std::min(extraTime, chainTime);
            skipCap = true;
        }
        const double extraStart = wallSeconds();
        const double extraUntil = extraStart + extraTime;
        bool chainSkipped = false;
        // L1's main DP can consume Stage 1 slack, leaving no neutral swaps while
        // the later chain allowance still runs. This experiment trades that existing
        // allowance for displacement-safe HPWL work; it does not shorten main DP or
        // enlarge the extra budget. Quick/legacy and predicted polish-only paths keep
        // their original schedule. --no-sa remains a Stage 1 diagnostic switch.
        const bool completeNeutral = completeNeutralBudget > 0 && deadline > 0 && maxAfterAbacus && predictSched &&
            lopt.maxComplete && legalizeStats.maxPhase && !polishOnly &&
            chainTime > 0 && extraTime > 0 && nswapTime > 0;
        if (completeNeutral) {
            const double allowance = std::min({completeNeutralBudget, chainTime, extraTime, nswapTime});
            const double neutralUntil = std::min({extraStart + allowance, extraUntil, endBy});
            const double budget = std::max(0.0, neutralUntil - wallSeconds());
            if (lopt.verbose)
                std::fprintf(stderr, "[complete-neutral] replacing chains/cap: allowance %.3f, until %.3f\n",
                             allowance, neutralUntil);
            if (budget > 0) neutralSwaps(legal, budget, neutralUntil);
            stageH("complete neutral", legal);
        } else {
            // Keep 's incumbent intact, then extend its neighbourhood beyond three
            // adjacent rows. Every phase is checked against its immediate predecessor.
            // This is an explicit EXTRA budget, not an equal-total-time comparison.
            const double chainUntil = wallSeconds() + chainTime;
            const DispStats chainStart = displacement(db, legal);
            const double chainT0 = wallSeconds();
            // where the main DP is rate-scored (time-bound designs), the chain phase continues only
            // while its first step (target windows, the cheapest) gains at least the DP's last measured
            // rate (L1 0.07 %/s and L2 nothing in 0.8 / 1.3 s, against the DP's 0.4 - 0.5 %/s)
            const bool probe = chainProbe && mainWin.rateStop > 0 && winRate >= 0;
            for (int round = 0; round < chainRounds && wallSeconds() < chainUntil && !chainSkipped; ++round) {
                const DispStats before = displacement(db, legal);
                auto paretoNow = [&] {
                    const auto after = displacement(db, legal);
                    return checkLegal(db, legal).empty() && after.total <= before.total + 1e-5 && after.max <= before.max + 1e-6;
                };
                long targetMoves = 0;
                if (targetWindows) {
                    WindowOptions targetOpt = wopt;
                    targetOpt.passes = 2;
                    targetOpt.timeLimit = (chainUntil - wallSeconds()) * splitTarget / (chainRounds - round);
                    const double tp = wallSeconds();
                    guarded("global-target windows", legal, [&] {
                        targetMoves = targetedWindowsDisp(db, legal, targetOpt);
                    }, paretoNow);
                    stageH("target windows", legal);
                    if (probe && round == 0) {
                        const double gainRate = (before.total - displacement(db, legal).total) / std::max(1.0, before.total) /
                                                std::max(1e-6, wallSeconds() - tp);
                        if (lopt.verbose)
                            std::fprintf(stderr, "[chain-probe] target windows %.4f%%/s vs window DP %.4f%%/s\n", 100 * gainRate, 100 * winRate);
                        if (gainRate < winRate) { chainSkipped = true; break; }
                    }
                }
                const auto beforeChain = displacement(db, legal);
                ChainResult result;
                copt.seconds = (chainUntil - wallSeconds()) * splitChains / (chainRounds - round);
                copt.seconds = std::max(0.0, copt.seconds);
                guarded("ejection chains", legal, [&] { result = chainDisplacement(db, legal, copt); }, [&] {
                    const auto after = displacement(db, legal);
                    return checkLegal(db, legal).empty() && after.total <= beforeChain.total + 1e-5 && after.max <= beforeChain.max + 1e-6;
                });
                stageH("ejection chains", legal);
                if (result.accepted == 0 && targetMoves == 0) break;
                WindowOptions afterChain = wopt;
                afterChain.timeLimit = std::max(0.0, (chainUntil - wallSeconds()) * splitDP / (chainRounds - round));
                const auto beforeDP = displacement(db, legal);
                guarded("post-chain windows", legal, [&] { windowSweeps(db, legal, afterChain, &stage1Memo); }, [&] {
                    const auto after = displacement(db, legal);
                    return checkLegal(db, legal).empty() && after.total <= beforeDP.total + 1e-5 && after.max <= beforeDP.max + 1e-6;
                });
                stageH("post-chain windows", legal);
                if (lopt.verbose) {
                    const auto after = displacement(db, legal);
                    std::fprintf(stderr, "[chain-round] %d: %ld target windows, %ld chains, longest %d, total %.6f -> %.6f, max %.6f -> %.6f\n",
                                 round + 1, targetMoves, result.accepted, result.longest, before.total, after.total, before.max, after.max);
                }
                stageH("chain round", legal);
            }
            const auto beforeGlobal = displacement(db, legal);
            if (lopt.verbose)
                std::fprintf(stderr, "[chain-phase] total %.6f -> %.6f, max %.6f -> %.6f  %.3fs of %.3fs\n", chainStart.total,
                             beforeGlobal.total, chainStart.max, beforeGlobal.max, wallSeconds() - chainT0, chainTime);
            if (legacyGlobal) {
                guarded("global cap repair", legal, [&] {
                    globalMinimax(db, legal, globalTime, totalRatio, wopt.threads, lopt.verbose);
                }, [&] {
                    const auto after = displacement(db, legal);
                    return checkLegal(db, legal).empty() && after.max <= beforeGlobal.max + 1e-6 &&
                           after.total <= beforeGlobal.total * totalRatio + 1e-5;
                });
            } else {
                const double hpwlBeforeCap = stage1Memo.hpwlGuarded ? totalHPWL(db, legal) : 0;
                const double capRatio = stage1Memo.hpwlGuarded ? 1.0 : totalRatio;
                CapSearchOptions copts;
                copts.seconds = capTime >= 0 ? capTime : std::max(0.0, extraUntil - wallSeconds() - (.1 + 1e-6 * db.movable.size()));
                copts.totalRatio = capRatio;
                copts.threads = wopt.threads;
                copts.dp = wopt;
                copts.dp.passes = 1000;
                copts.verbose = lopt.verbose;
                if (copts.seconds > 0 && !skipCap && !chainSkipped) {
                    guarded("cap search", legal, [&] { capSearch(db, legal, copts, &stage1Memo); }, [&] {
                        const auto after = displacement(db, legal);
                        return checkLegal(db, legal).empty() && after.max <= beforeGlobal.max + 1e-6 &&
                               after.total <= beforeGlobal.total * capRatio + 1e-5 &&
                               (!stage1Memo.hpwlGuarded || totalHPWL(db, legal) <= hpwlBeforeCap + 1e-5);
                    });
                }
            }
        }
        if (lopt.verbose) std::fprintf(stderr, "[extra] budget %.6f used %.6f\n", extraTime, wallSeconds() - extraStart);
        // The last displacement-memo consumer is the cap-search guard above.
        // Neutral swaps and Stage 2 use their own state; retain no per-cell stamps.
        if (releaseStage1Memo) {
            const long before = lopt.verbose ? readVmRssKiB() : -1;
            const double started = lopt.verbose ? wallSeconds() : 0;
            {
                WindowMemo empty;
                std::swap(stage1Memo, empty);
            }
            const int trimmed = releaseTrim ? trimReleasedMemory() : 0;
            if (lopt.verbose)
                std::fprintf(stderr, "[memory-release] rss_kib=%ld->%ld trim=%d/%d seconds=%.6f\n",
                             before, readVmRssKiB(), int(releaseTrim), trimmed, wallSeconds() - started);
        }
        phases.extra = wallSeconds() - extraStart;
        if (phasesLine) hwmExtra = readVmHwmKiB();
        phases.chain = completeNeutral ? "complete-neutral" : chainSkipped ? "probe-skip" : "ran";
        if (!completeNeutral) stageH("cap search", legal);
        // Last step of Stage 1: no displacement step re-shuffles these identities afterwards.
        // Under a deadline, allow neutral swaps up to min(nswapTime, 2.0s, 18% of remaining budget)
        // even when previous steps reached stage1Until, since neutral swaps strictly reduce HPWL
        // at 50M-1000M/s without worsening displacement.
        // The replacement already spent its absolute allowance; do not reallocate it as slack.
        if (nswapTime > 0 && !completeNeutral) {
            double stageSlack = std::max(0.0, stage1Until - extraStart);
            if (longPath && stageSlack < 1.0) {
                const double rem = std::max(0.0, endBy - wallSeconds());
                stageSlack = std::max(stageSlack, std::min(1.2, 0.12 * rem));
            }
            double budget = std::min({nswapTime, 2.0, stageSlack});
            const double beforeReserve = budget;
            // at least the reserved pass on annealed designs (never past the overall deadline)
            if (nswapReserveSeconds > 0)
                budget = nswap_reserve::apply(budget, nswapReserveSeconds,
                    nswap_reserve::allowanceCap(nswapTime, std::max(0.0, endBy - wallSeconds())));
            phases.reserveBudgetIncrease = budget - beforeReserve;
            if (adaptiveNswap && polishOnly && deadline > 0) {
                // Compare the legacy half cap with the full remaining cap; completed-pass rate still stops early.
                budget = (fullNswapBudget ? 1.0 : 0.5) * std::max(0.0, endBy - wallSeconds());
                if (nswapTimeSet) budget = std::min(budget, nswapTime);
            }
            neutralSwaps(legal, budget);
            stageH("neutral swaps", legal);
        }
    }   // Stage 1
    std::string bad = checkLegal(db, legal);
    if (!bad.empty()) { std::fprintf(stderr, "error: legalization is illegal: %s\n", bad.c_str()); return 1; }
    DispStats disp = displacement(db, legal);
    double legalHPWL = totalHPWL(db, legal);
    const double stage1End = wallSeconds();
    if (phasesLine) hwmStage1 = readVmHwmKiB();
    std::printf("Total displacement (legal): %s\n", fmtNum(disp.total).c_str());
    std::printf("Maximum displacement (legal): %s\n", fmtNum(disp.max).c_str());
    std::printf("Total HPWL (legal): %s\n", fmtNum(legalHPWL).c_str());
    std::fflush(stdout);
    if (lopt.verbose) std::fprintf(stderr, "[stage1] done  %.2fs\n", seconds(t0));

    // SA 60 -> polish 30 for every design and lets the SA
    // measure itself instead (SAOptions::starvedAttemptsPerCell): a SA that
    // cannot give each cell 100 attempts keeps 10 % of its limit and the
    // polish gets the rest. An explicit --time is still used as given.
    const double baseStarvedAttempts = sopt.starvedAttemptsPerCell;
    if (saTimeSet && !starvedSet) sopt.starvedAttemptsPerCell = 0;
    // the SA gets what is left before the polish share; the polish ends by endBy.
    // When measured Stage 1 pass/read times prove SA cannot complete enough passes per cell
    // before endBy (so Step 0 would starve at hot T0 and waste time), hand all of left to polish.
    if (deadline > 0) {
        const double left = std::max(0.0, endBy - wallSeconds());
        const double minSaBudget = std::max({25.0 * readSeconds, 8.0 * dtParetoPass, 8.0 * dtNswapPass});
        if (!saTimeSet && !polishTimeSet && left < minSaBudget) {
            polishTime = left;
            sopt.timeLimit = 0;
        } else {
            if (polishTime > left) polishTime = left;
            if (!saTimeSet) sopt.timeLimit = std::max(0.0, left - polishTime);
            else sopt.timeLimit = std::min(sopt.timeLimit, left);
        }
        if (!starvedSet && !saTimeSet && sopt.timeLimit > 0) {
            sopt.starvedAttemptsPerCell *= std::min(1.0, sopt.timeLimit / 60.0);
        }
        if (lopt.verbose)
            std::fprintf(stderr, "[deadline] %.1f s: Stage 2 starts at %.2f s, SA %.2f s, polish %.2f s, ends by %.2f s\n",
                         deadline, wallSeconds(), sopt.timeLimit, polishTime, endBy);
    }
    // V1: replay the production SA/polish shares on one legal placement. Unlike
    // --time this preserves the production starvation calibration; all work is
    // still capped by the original overall deadline. Defaults take no new path.
    double stage2Until = endBy;
    if (stage2BudgetSet) {
        const double replayStart = wallSeconds();
        const double left = std::max(0.0, endBy - replayStart);
        polishTime = std::min(replayPolish, left);
        sopt.timeLimit = std::min(replaySA, std::max(0.0, left - polishTime));
        // Production shares one absolute end point: a complete SA batch or
        // guard that overruns its quota consumes polish time in replay too.
        stage2Until = std::min(endBy, replayStart + sopt.timeLimit + polishTime);
        sopt.starvedAttemptsPerCell = baseStarvedAttempts;
        if (!starvedSet && deadline > 0 && sopt.timeLimit > 0)
            sopt.starvedAttemptsPerCell *= std::min(1.0, sopt.timeLimit / 60.0);
    }
    // ---- Stage 2
    const double stage2Start = wallSeconds();
    if (printOptions) {
        printSAOptions(sopt, "sa-runtime");
        OptionLine line("stage2-runtime");
        line.add("runSA", runSA); line.add("polishTime", polishTime);
        line.add("resolvedSAThreads", saThreads(db, sopt)); line.add("stage2Until", stage2Until);
    }
    Placement final = legal;
    SAResult sa;
    double polishInputHPWL = legalHPWL;
    bool saCommitted = false, polishCommitted = false, stage2Fallback = false;
    if (runSA) {
        auto legalNow = [&] { return checkLegal(db, final).empty(); };
        const double tSA = wallSeconds();
        if (sopt.timeLimit > 0) saCommitted = guarded("SA", final, [&] { sa = anneal(db, final, sopt); }, legalNow);
        phases.sa = wallSeconds() - tSA;
        if (phasesLine) hwmSA = readVmHwmKiB();
        const double tPolish = wallSeconds();
        double beforePolish = totalHPWL(db, final);
        polishInputHPWL = beforePolish;
        auto better = [&] { return checkLegal(db, final).empty() && totalHPWL(db, final) <= beforePolish + 1e-5; };
        // the time a starved SA gave up goes to the polish (same total)
        oopt.until = std::min(wallSeconds() + polishTime + sa.limitCut, stage2Until);
        if (printOptions) printOptimizeOptions(oopt, "polish-runtime");
        polishCommitted = guarded("HPWL polish", final, [&] {
            polishHPWL(db, final, oopt, sopt.tracePath.empty() ? "" : sopt.tracePath + ".polish.csv");
        }, better);
        WindowOptions hpopt = wopt;
        hpopt.passes = hpwlWindowPasses;
        hpopt.timeLimit = std::max(0.0, oopt.until - wallSeconds());
        // an expired step is skipped before its RowMap, placement copy and checks (L2: about 1 s
        // past the deadline in for steps that did nothing)
        if (!oopt.earlyExit || hpopt.timeLimit > 0)
            guarded("HPWL windows", final, [&] { optimizeWindowsHPWL(db, final, hpopt); }, better);
        if (!oopt.earlyExit || wallSeconds() < oopt.until)
            guarded("order DP", final, [&] { reorderWindows(db, final, orderPasses, wopt.verbose, oopt.until); }, better);
        if (lopt.verbose) std::fprintf(stderr, "[polish-total] %.6f -> %.6f\n", beforePolish, totalHPWL(db, final));
        phases.polish = wallSeconds() - tPolish;
        if (phasesLine) hwmPolish = readVmHwmKiB();
    }
    if (!checkLegal(db, final).empty() || totalHPWL(db, final) > legalHPWL + 1e-5) {
        std::fprintf(stderr, "warning: Stage 2 result rejected, keeping the legal placement\n");
        final = legal;
        stage2Fallback = true;
    }
    // Reuse the pre-existing stdout scan; D2 performs no additional HPWL work.
    const double finalHPWL = totalHPWL(db, final);
    std::printf("Total HPWL (SA): %s\n", fmtNum(finalHPWL).c_str());
    if (lopt.verbose) std::fprintf(stderr, "[stage2] done  %.2fs\n", seconds(t0));

    const double stage2End = wallSeconds();
    if (!writeOutput(db, legal, final, output, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    if (phasesLine) hwmWrite = readVmHwmKiB();
    std::fprintf(stderr, "[timing] read=%.6f abacus=%.6f stage1_end=%.6f stage2_start=%.6f stage2_end=%.6f total=%.6f\n",
                 readSeconds, legalizeStats.abacusSeconds, stage1End, stage2Start, stage2End, wallSeconds());
    // why each Stage 1 / Stage 2 step ended and what it took (stdout stays four lines)
    if (phasesLine) {
        const LegalizeStats& L = legalizeStats;
        std::fprintf(stderr, "[phases] max=%d/%.3fs/%s match=%.3fs/%s pipeline=%s", L.maxMoves, L.maxSeconds, L.maxWhy.c_str(),
                     L.matchSeconds, L.matchWhy.c_str(), L.choice.c_str());
        if (L.choice != "single")
            std::fprintf(stderr, "(plain %.1f/%.4f spread %.1f/%.4f score %.6f%s)", L.plainTotal, L.plainMax, L.spreadTotal,
                         L.spreadMax, L.score, L.tieFinish ? (" tie-finished score " + std::to_string(L.finalScore)).c_str() : "");
        std::fprintf(stderr, " finish=%.3fs pareto=%.3fs window=%d/%.3fs/%s/rate%.4f%%/skips%ld chain=%s extra=%.3fs "
                     "nswap=%d/%.3fs/%s sa=%.3fs polish=%.3fs", L.finishSeconds, phases.pareto, phases.winPasses, phases.winSeconds,
                     phases.winStop.c_str(), 100 * phases.winRate, phases.winSkips, phases.chain.c_str(), phases.extra,
                     lastNswap.passes, lastNswapSeconds, lastNswap.stop, phases.sa, phases.polish);
        std::fprintf(stderr, " sa-summary=T0:%.17g,target:%.17g,last:%.17g,min:%.17g,steps:%d/%d/%d,moves:%lld,accepted:%lld,accept:%.9f,sync:%lld,current:%.17g,best:%.17g,stop:%s,complete:%d,threads:%d,limit:%.9f,cut:%.9f,step0:%.9f,projected:%.9f,cold:%.9f,syncmoves:%.9f,committed:%d,laststep:%d,failed:%lld,move_s:%.9f,merge_s:%.9f"
                     " polish-hpwl=%.17g/%.17g/fallback%d sa-budget=%.9f/polish-budget=%.9f polish-only=%d stage2-cap=%.9f/polish-guard-committed=%d",
                     sa.temperature0, sa.temperatureTarget, sa.lastTemperature, sa.minimumTemperature,
                     sa.stepsStarted, sa.stepsCompleted, sa.stepsPlanned, sa.attempts, sa.accepted,
                     double(sa.accepted) / std::max(1LL, sa.attempts), sa.syncRounds, sa.current, sa.best,
                     sa.stopReason, int(sa.scheduleComplete), sa.threads, sa.effectiveTimeLimit, sa.limitCut,
                     sa.step0Seconds, sa.projectedAttemptsPerCell, sa.coldStartRatio, sa.finalSyncMoves, int(saCommitted), sa.lastStep, sa.failed, sa.moveSeconds, sa.mergeSeconds,
                     polishInputHPWL, finalHPWL, int(stage2Fallback), sopt.timeLimit, polishTime, int(polishOnly), stage2Until - stage2Start, int(polishCommitted));
        std::fprintf(stderr, " reserve=%d/estimate%.6f/time-stop%d/grant%.6f window-first=%.6f/window-limit=%.6f"
                     " nswap-work=allowance%.6f/setup%.6f/first%.6f/order%.6f/complete%d/prelude%.6f",
                     int(phases.reserveEligible), phases.reserveEstimate, int(phases.reserveTimeStop), phases.reserveBudgetIncrease,
                     phases.winFirst, phases.winLimit, lastNswapAllowance, lastNswap.setupSeconds,
                     lastNswap.firstPassSeconds, lastNswap.firstOrderSeconds, int(lastNswap.firstPassComplete), lastNswapPrelude);
        std::fprintf(stderr, " hwm_kib=read:%ld,abacus:%ld,max:%ld,match:%ld,finish:%ld,legal:%ld,pareto:%ld,window:%ld,extra:%ld,nswap:%ld,stage1:%ld,sa:%ld,polish:%ld,write:%ld\n",
                     hwmRead, L.hwmAbacusKiB, L.hwmMaxKiB, L.hwmMatchKiB, L.hwmFinishKiB,
                     hwmLegal, hwmPareto, hwmWindow, hwmExtra, hwmNswap, hwmStage1, hwmSA, hwmPolish, hwmWrite);
    }
    return 0;
}

int main(int argc, char** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& e) {std::fprintf(stderr, "error: %s\n", e.what());return 1;}
}
