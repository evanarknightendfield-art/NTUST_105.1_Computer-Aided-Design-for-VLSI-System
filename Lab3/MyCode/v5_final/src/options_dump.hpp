#pragma once
#include "diagnostics.hpp"
#include "legalizer.hpp"
#include "annealer.hpp"
#include "optimizer.hpp"
#include "window.hpp"
#include "chain.hpp"

// Keep a complete, named snapshot of each option object; runtime scopes below
// report changes made by automatic scheduling immediately before execution.
inline void printLegalizeOptions(const LegalizeOptions& o, const char* scope = "legal") {
    OptionLine line(scope);
    line.add("method", o.method);
    line.add("totalCost", o.totalCost);
    line.add("refineMax", o.refineMax);
    line.add("refineTotal", o.refineTotal);
    line.add("evict", o.evict);
    line.add("matching", o.matching);
    line.add("rowOpt", o.rowOpt);
    line.add("spread", o.spread);
    line.add("binRows", o.binRows);
    line.add("binCells", o.binCells);
    line.add("spreadRounds", o.spreadRounds);
    line.add("sweepRows", o.sweepRows);
    line.add("sweepSites", o.sweepSites);
    line.add("totalTol", o.totalTol);
    line.add("finishRounds", o.finishRounds);
    line.add("swapPush", o.swapPush);
    line.add("windowDP", o.windowDP);
    line.add("windowCells", o.windowCells);
    line.add("windowCells3", o.windowCells3);
    line.add("until", o.until);
    line.add("threads", o.threads);
    line.add("parallelPipelines", o.parallelPipelines);
    line.add("tieBand", o.tieBand);
    line.add("maxUntil", o.maxUntil);
    line.add("maxShare", o.maxShare);
    line.add("maxRate", o.maxRate);
    line.add("maxFloor", o.maxFloor);
    line.add("maxTail", o.maxTail);
    line.add("maxPrune", o.maxPrune);
    line.add("abacusFast", o.abacusFast);
    line.add("maxFast", o.maxFast);
    line.add("maxThreads", o.maxThreads);
    line.add("maxThreadsResolved", o.maxThreads > 0 ? o.maxThreads : o.threads);
    line.add("releaseMaxScratch", o.releaseMaxScratch);
    line.add("maxPolicyMinSeconds", o.maxPolicyMinSeconds);
    line.add("maxComplete", o.maxComplete);
    line.add("maxCheckMatching", o.maxCheckMatching);
    line.add("maxCheckMoves", o.maxCheckMoves);
    line.add("abacusExactWork", o.abacusExactWork);
    line.add("verbose", o.verbose);
    line.add("collectHwm", o.collectHwm);
    std::ostringstream modes;
    for (const auto& mode : o.maxCheckModes) modes << mode.first << ':' << mode.second << ',';
    line.add("maxCheckModes", modes.str());
}

inline void printSAOptions(const SAOptions& o, const char* scope = "sa") {
    OptionLine line(scope);
    line.add("seed", o.seed);
    line.add("timeLimit", o.timeLimit);
    line.add("movesPerCell", o.movesPerCell);
    line.add("minMoves", o.minMoves);
    line.add("maxMoves", o.maxMoves);
    line.add("steps", o.steps);
    line.add("p0", o.p0);
    line.add("tEndRatio", o.tEndRatio);
    line.add("pTarget", o.pTarget);
    line.add("win0", o.win0);
    line.add("maxWin", o.maxWin);
    line.add("pGlobalSwap", o.pGlobalSwap);
    line.add("pRowOpt", o.pRowOpt);
    line.add("rowWin", o.rowWin);
    line.add("pIsm", o.pIsm);
    line.add("ismSize", o.ismSize);
    line.add("ismReach", o.ismReach);
    line.add("polishFrom", o.polishFrom);
    line.add("minWinCells", o.minWinCells);
    line.add("minWinRows", o.minWinRows);
    line.add("clampTarget", o.clampTarget);
    line.add("pReorder", o.pReorder);
    line.add("coldReorder", o.coldReorder);
    line.add("nearTries", o.nearTries);
    line.add("threads", o.threads);
    line.add("tStartRatio", o.tStartRatio);
    line.add("autoPerCell", o.autoPerCell);
    line.add("autoExp", o.autoExp);
    line.add("autoMinRatio", o.autoMinRatio);
    line.add("autoSync", o.autoSync);
    line.add("forceThreads", o.forceThreads);
    line.add("minBandRows", o.minBandRows);
    line.add("syncMoves", o.syncMoves);
    line.add("starvedAttemptsPerCell", o.starvedAttemptsPerCell);
    line.add("starvedMinFraction", o.starvedMinFraction);
    line.add("tracePath", o.tracePath);
    line.add("verbose", o.verbose);
}

inline void printOptimizeOptions(const OptimizeOptions& o, const char* scope = "optimize") {
    OptionLine line(scope);
    line.add("paretoPasses", o.paretoPasses);
    line.add("polishPasses", o.polishPasses);
    line.add("groupSize", o.groupSize);
    line.add("verbose", o.verbose);
    line.add("until", o.until);
    line.add("threads", o.threads);
    line.add("minMaxGain", o.minMaxGain);
    line.add("minHpwlGain", o.minHpwlGain);
    line.add("earlyExit", o.earlyExit);
    line.add("boxCache", o.boxCache);
    line.add("partialPass", o.partialPass);
    line.add("spatialTraversal", o.spatialTraversal);
    line.add("reuseRowMap", o.reuseRowMap);
    line.add("finalCheck", o.finalCheck);
    line.add("polishStatsEnabled", o.polishStats != nullptr);
}

inline void printWindowOptions(const WindowOptions& o, const char* scope = "window") {
    OptionLine line(scope);
    line.add("passes", o.passes);
    line.add("perRow", o.perRow);
    line.add("maxSites", o.maxSites);
    line.add("timeLimit", o.timeLimit);
    line.add("tol", o.tol);
    line.add("rows", o.rows);
    line.add("perRow3", o.perRow3);
    line.add("threads", o.threads);
    line.add("verbose", o.verbose);
    line.add("guardHPWL", o.guardHPWL);
    line.add("lowDispGainTol", o.lowDispGainTol);
    line.add("maxHpwlRate", o.maxHpwlRate);
    line.add("minGuardPasses", o.minGuardPasses);
    line.add("rateStop", o.rateStop);
    line.add("hpwlWeight", o.hpwlWeight);
    line.add("ratePerNeighborhood", o.ratePerNeighborhood);
    line.add("reuseScratch", o.reuseScratch);
    line.add("rowKernel", o.rowKernel);
    line.add("incrementalHpwl", o.incrementalHpwl);
    line.add("checkHpwl", o.checkHpwl);
    line.add("lowerBoundSkip", o.lowerBoundSkip);
    line.add("reserveFactor", o.reserveFactor);
    line.add("reserveCap", o.reserveCap);
    line.add("activeEnabled", o.active != nullptr);
}

inline void printChainOptions(const ChainOptions& o, const char* scope = "chain") {
    OptionLine line(scope);
    line.add("passes", o.passes);
    line.add("depth", o.depth);
    line.add("branches", o.branches);
    line.add("expansions", o.expansions);
    line.add("roots", o.roots);
    line.add("maxBlockers", o.maxBlockers);
    line.add("seconds", o.seconds);
    line.add("verbose", o.verbose);
}


