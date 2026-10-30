// main.cpp — cbi INPUT_FILE OUTPUT_FILE [-t seconds] [-s seed] [-v]
//
// Stage 1：bottom-up 分群建立合法初始解（construct.cpp）
// Stage 2：插入 delay buffer，用少量 cost 換大幅降低 skew（balance.cpp）
// Stage 3：模擬退火降低 Score，全程維持合法（optimizer.cpp）
// 每個階段後都用獨立的 evaluate() 從頭驗證；有問題就退回前一階段的解。
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "balance.h"
#include "construct.h"
#include "optimizer.h"
#include "problem.h"
#include "solution_io.h"
#include "tree.h"

namespace {

void usage(const char* prog) {
    std::fprintf(stderr,
                 "usage: %s INPUT_FILE OUTPUT_FILE [-t seconds] [-s seed] [-v]\n"
                 "  -t  total time limit in seconds (default: 3 + #sinks/250, max 25)\n"
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
    EvalResult cur = e2[pickM];
    Snapshot stage2Sol;
    T.save(stage2Sol);

    // ---------- Stage 3：模擬退火（小測資做多次短跑，每次從目前最佳解出發、換一個 seed） ----------
    if (timeLimit < 0) timeLimit = std::min(25.0, 3.0 + T.N / 250.0);
    double budget = timeLimit - elapsed();
    if (budget > 0.05 && T.N > 0) {
        double perRun = std::max(0.75, T.N / 250.0);
        int runs = (int)std::max(1.0, std::min(8.0, budget / perRun));
        Snapshot bestSol = stage2Sol;
        for (int r = 0; r < runs; ++r) {
            OptParams prm;
            prm.timeLimit = (timeLimit - elapsed()) / (runs - r);
            prm.seed = seed + 7919ULL * r;
            prm.verbose = verbose;
            if (const char* b = std::getenv("CBI_BETA")) prm.beta = std::atof(b);
            T.load(bestSol);
            OptReport rep = optimize(T, prm);
            EvalResult e3 = evaluate(T);
            bool good = e3.legal && T.score() == e3.score;   // 增量計算與完整重算必須一致
            std::fprintf(stderr, "[anneal run %d/%d] score=%lld iterations=%lld accepted=%.1f%% %.0f it/s%s\n", r + 1,
                         runs, e3.score, rep.iters, rep.iters ? 100.0 * rep.accepted / rep.iters : 0.0,
                         rep.iters / std::max(1e-9, rep.seconds), good ? "" : "  (INCONSISTENT, discarded)");
            if (good && e3.score < cur.score) {
                cur = e3;
                T.save(bestSol);
            }
        }
        T.load(bestSol);
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
