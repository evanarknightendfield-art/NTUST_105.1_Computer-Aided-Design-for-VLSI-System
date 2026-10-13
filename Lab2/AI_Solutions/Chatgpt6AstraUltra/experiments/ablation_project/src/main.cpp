// main.cpp — cbi INPUT_FILE OUTPUT_FILE [-t seconds] [-s seed] [-v]
//
// Stage 1：bottom-up 分群建立合法初始解（construct.cpp）
// Stage 2：插入 delay buffer，用少量 cost 換大幅降低 skew（balance.cpp）
// Stage 3：模擬退火降低 Score，全程維持合法（optimizer.cpp）；2 條退火鏈平行跑
// 每個階段後都用獨立的 evaluate() 從頭驗證；有問題就退回前一階段的解。
#include <chrono>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "balance.h"
#include "construct.h"
#include "optimizer.h"
#include "parallel.h"
#include "problem.h"
#include "solution_io.h"
#include "tree.h"

namespace {

void usage(const char* prog) {
    std::fprintf(stderr,
                 "usage: %s INPUT_FILE OUTPUT_FILE [-t seconds] [-s seed] [-v]\n"
                 "  -t  total time limit in seconds (default: 10 + #sinks/100, max 30; 5 for <= 12 sinks)\n"
                 "  -s  random seed (default 1)\n"
                 "  -v  verbose progress on stderr\n"
                 "  -d  PREFIX: also write PREFIX.stage1.cbi / PREFIX.stage2.cbi (for figures)\n",
                 prog);
}

void logStage(const char* name, const EvalResult& e, double sec) {
    std::fprintf(stderr, "[%s] legal=%d buffers=%d cost=%lld skew=%lld T_max=%lld T_min=%lld score=%lld levels=%d time=%.2fs%s%s\n",
                 name, e.legal, e.numBuf, e.cost, e.tmax - e.tmin, e.tmax, e.tmin, e.score, e.levels, sec,
                 e.legal ? "" : " why=", e.why.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    using clk = std::chrono::steady_clock;
    auto t0 = clk::now();
    auto elapsed = [&]() { return std::chrono::duration<double>(clk::now() - t0).count(); };

    if (argc < 3) {
        std::fprintf(stderr, "error: missing arguments\n");
        usage(argv[0]);
        return 1;
    }
    std::string inPath = argv[1], outPath = argv[2];
    double timeLimit = -1;
    unsigned long long seed = 1;
    bool verbose = false;
    std::string dumpPrefix;   // -d PREFIX：另外輸出各階段的解 PREFIX.stage1.cbi / .stage2.cbi
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-t" && i + 1 < argc) timeLimit = std::atof(argv[++i]);
        else if (a == "-s" && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "-v") verbose = true;
        else if (a == "-d" && i + 1 < argc) dumpPrefix = argv[++i];
        else { usage(argv[0]); return 1; }
    }

    Problem P;
    std::string err;
    if (!readProblem(inPath, P, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    TypeTable TT;
    TT.build(P.lib);
    Tree T;
    T.init(P, TT);
    std::fprintf(stderr, "[input] sinks=%d chip=%dx%d buflib=%zu w_skew=%lld SRC(F=%d,L=%lld)\n", T.N, P.dimx, P.dimy,
                 P.lib.size(), P.wSkew, P.srcF, P.srcL);

    // ---------- Stage 1：建構（兩種收尾方式都做） + Stage 2：插 delay buffer ----------
    Tree cand[2];
    EvalResult e1[2], e2[2];
    bool ok[2] = {false, false};
    for (int m = 0; m < 2; ++m) {
        cand[m] = T;
        ok[m] = buildInitial(cand[m], m == 1);
        if (!ok[m]) continue;
        e1[m] = evaluate(cand[m]);
        logStage(m ? "stage1-construct(single-root)" : "stage1-construct(attach-early)", e1[m], elapsed());
        Snapshot before;
        cand[m].save(before);
        int pads = balanceSkew(cand[m]);
        e2[m] = evaluate(cand[m]);
        if (!e2[m].legal || e2[m].score > e1[m].score) {   // 理論上不會發生，保險起見
            cand[m].load(before);
            e2[m] = e1[m];
        }
        std::fprintf(stderr, "  (+%d delay buffers)\n", pads);
        logStage(m ? "stage2-balance(single-root)" : "stage2-balance(attach-early)", e2[m], elapsed());
    }
    if (!ok[0] && !ok[1]) {
        std::fprintf(stderr, "error: failed to build a legal initial tree\n");
        writeSolution(T, outPath, err);
        return 1;
    }
    int pickM = (!ok[0] || (ok[1] && e2[1].score < e2[0].score)) ? 1 : 0;
    T = cand[pickM];
    if (!dumpPrefix.empty()) {
        writeSolution(T, dumpPrefix + ".stage2.cbi", err);
        Tree s1 = T;   // 重新建一次同一種 Stage 1 的解來輸出
        s1.init(P, TT);
        buildInitial(s1, pickM == 1);
        writeSolution(s1, dumpPrefix + ".stage1.cbi", err);
    }
    // Compare the FLUTE-inspired construction portfolio by the actual
    // independently verified CBI score. It is an ablation toggle initially.
    if (const char* e = std::getenv("CBI_SPATIAL")) if (std::atoi(e) != 0) {
        for (int mode = 0; mode < 2; ++mode) {
            Tree spatial; spatial.init(P, TT);
            if (!buildInitialSpatial(spatial, mode == 1)) continue;
            EvalResult first = evaluate(spatial);
            if (!first.legal) continue;
            balanceSkew(spatial);
            EvalResult candidate = evaluate(spatial);
            logStage("spatial-portfolio", candidate, elapsed());
            if (candidate.legal && candidate.score < e2[pickM].score) {
                T = std::move(spatial);
                e2[pickM] = candidate;
            }
        }
    }
    EvalResult cur = e2[pickM];
    Snapshot stage2Sol;
    T.save(stage2Sol);

    // ---------- Stage 3：平行模擬退火 ----------
    // 外層 run：小測資做多次重啟（每次完整降溫一次，從目前最佳解出發）。
    // 內層 epoch：nThreads 條退火鏈（不同 seed）各跑降溫曲線的同一段，
    //             段末挑分數最好的鏈，所有鏈從它接著跑下一段（不重新加熱）。
    // 評分 VM 有 2 個核心（qemu -smp 2），所以最多用 2 條鏈；單核心機器自動退回單執行緒。
    // 預設時間：只有 Score 計分、每筆上限 60 秒，所以用到最多 30 秒（留一半給較慢的評分機）
    if (timeLimit < 0) timeLimit = T.N <= 12 ? 5.0 : std::min(30.0, 10.0 + T.N / 100.0);
    double budget = timeLimit - elapsed();
    if (budget > 0.05 && T.N > 0) {
        int nThreads = (int)std::max(1u, std::min(2u, std::thread::hardware_concurrency()));
        if (const char* e = std::getenv("CBI_THREADS")) nThreads = std::max(1, std::atoi(e));   // 實驗用
        // 同步間隔（秒）：每次同步要複製 / 合併整棵樹（O(N)），超大測資拉長間隔以免同步本身吃掉太多時間
        double syncEvery = std::min(5.0, std::max(2.0, T.N / 10000.0));
        if (const char* e = std::getenv("CBI_SYNC")) syncEvery = std::max(0.05, std::atof(e));   // 實驗用
        // 大測資用「分工」：兩條執行緒各負責一半的子樹，epoch 末合併（等於真的 2 倍搜尋量）。
        // 只在 buffer 線長上限相對晶片很短時使用：這時樹很深、局部結構多，分工最划算；
        // 線長上限很長時樹很淺，skew 主要靠被凍結的上層調整，分工反而沒幫助（實驗見 README）。
        long long maxL = 0;
        for (const BufType& bt : P.lib) maxL = std::max(maxL, bt.L);
        int decompMinN = 1500;
        double decompMaxL = 0.65;
        if (const char* e = std::getenv("CBI_DECOMP_N")) decompMinN = std::atoi(e);      // 實驗用
        if (const char* e = std::getenv("CBI_DECOMP_L")) decompMaxL = std::atof(e);      // 實驗用
        bool decomp = nThreads >= 2 && T.N >= decompMinN &&
                      maxL <= decompMaxL * std::max(P.dimx, P.dimy);
        int merges = 0, mergeFail = 0;
        double perRun = std::max(0.75, T.N / 250.0);
        int runs = (int)std::max(1.0, std::min(8.0, budget / perRun));
        if (const char* e = std::getenv("CBI_RUNS")) runs = std::max(1, std::atoi(e));   // 實驗用
        Snapshot bestSol = stage2Sol;
        std::vector<Tree> chain(nThreads, T);
        std::vector<OptReport> rep(nThreads);
        long long totalIters = 0;
        for (int r = 0; r < runs; ++r) {
            double runTime = (timeLimit - elapsed()) / (runs - r);
            double runEnd = elapsed() + runTime;
            int epochs = nThreads > 1 ? std::max(1, (int)std::lround(runTime / syncEvery)) : 1;
            Snapshot from = bestSol;   // 這一段的起點
            for (int ep = 0; ep < epochs; ++ep) {
                OptParams prm;
                prm.timeLimit = (runEnd - elapsed()) / (epochs - ep);
                prm.schedFrom = (double)ep / epochs;
                prm.schedTo = (double)(ep + 1) / epochs;
                prm.verbose = verbose;
                if (const char* b = std::getenv("CBI_BETA")) prm.beta = std::atof(b);
                for (int k = 0; k < nThreads; ++k) chain[k].load(from);
                Cut cut;
                // 前 70% 的 epoch 中，偶數 epoch 分工（加速樹的大部分）；奇數 epoch 兩條鏈都看整棵樹，
                // 才能調整被凍結的上層（對 skew 很重要）。最後 30% 低溫收尾一律看整棵樹，修好合併造成的 skew。
                double decompUntil = 0.7;
                if (const char* e = std::getenv("CBI_DECOMP_UNTIL")) decompUntil = std::atof(e);   // 實驗用
                bool useCut = decomp && ep % 2 == 0 && ep < decompUntil * epochs &&
                              makeCut(chain[0], (ep / 2) % 2, 1.0 / 3.0, cut);
                std::vector<std::vector<signed char>> reg(useCut ? 2 : 0, cut.region);
                auto work = [&](int k) {
                    OptParams q = prm;
                    q.seed = seed + 7919ULL * r + 104729ULL * k + 1299709ULL * ep;
                    if (useCut && k < 2) {
                        q.region = &reg[k];
                        q.isRoot = &cut.isRoot;
                        q.roots = &cut.roots[k];
                        q.regionId = (signed char)k;
                    }
                    rep[k] = optimize(chain[k], q);
                };
                std::vector<std::thread> pool;
                try {
                    for (int k = 1; k < nThreads; ++k) pool.emplace_back(work, k);
                } catch (const std::exception&) {   // 開不了執行緒就少跑幾條，不影響正確性
                }
                work(0);
                for (auto& th : pool) th.join();
                int ran = 1 + (int)pool.size();
                Tree mergedTree;
                bool hasMerged = false;
                if (useCut && ran >= 2) {   // Keep both individual chains as well as the merged candidate.
                    Tree& M = mergedTree;
                    if (mergeCut(chain[0], chain[1], cut, M)) {
                        hasMerged = true;
                        ++merges;
                    } else {
                        ++mergeFail;
                    }
                }
                int bestK = -1;
                long long bestEp = 0;
                for (int k = 0; k < ran + (hasMerged ? 1 : 0); ++k) {
                    Tree& candidate = k < ran ? chain[k] : mergedTree;
                    EvalResult e3 = evaluate(candidate);
                    if (k < ran) totalIters += rep[k].iters;
                    if (!e3.legal || candidate.score() != e3.score) {   // 增量計算與完整重算必須一致
                        std::fprintf(stderr, "warning: chain %d inconsistent (legal=%d %s), discarded\n", k, e3.legal,
                                     e3.why.c_str());
                        continue;
                    }
                    if (bestK < 0 || e3.score < bestEp) { bestK = k; bestEp = e3.score; }
                    if (e3.score < cur.score) {
                        cur = e3;
                        candidate.save(bestSol);
                    }
                }
                if (bestK >= 0) (bestK < ran ? chain[bestK] : mergedTree).save(from);
                if (verbose)
                    std::fprintf(stderr, "[anneal run %d/%d epoch %d/%d] best chain %d score=%lld\n", r + 1, runs,
                                 ep + 1, epochs, bestK, bestEp);
            }
        }
        T.load(bestSol);
        std::fprintf(stderr, "[anneal] threads=%d runs=%d decomposition=%d (merged %d, failed %d) iterations=%lld\n",
                     nThreads, runs, (int)decomp, merges, mergeFail, totalIters);
        logStage("stage3-anneal", cur, elapsed());
    }
    EvalResult final = evaluate(T);

    if (!writeSolution(T, outPath, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    std::printf("T_max: %lld, T_min: %lld, Score: %lld\n", final.tmax, final.tmin, final.score);
    return final.legal ? 0 : 2;
}
