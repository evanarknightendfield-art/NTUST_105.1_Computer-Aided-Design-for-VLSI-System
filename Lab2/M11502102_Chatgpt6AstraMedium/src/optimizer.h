// optimizer.h — Stage 2：在「永遠合法」的前提下用模擬退火降低 Score
#pragma once
#include <cstdint>

#include "tree.h"

struct OptParams {
    double timeLimit = 5.0;   // 秒
    uint64_t seed = 1;
    double beta = 0.3;        // 輔助項 σ（arrival time 標準差）的權重
    bool greedy = false;     // 最後一輪僅接受 energy 不增加的操作
    bool verbose = false;
};

struct OptReport {
    long long iters = 0, accepted = 0;
    double seconds = 0;
};

// 執行完畢後 T 為搜尋過程中 Score 最低的解
OptReport optimize(Tree& T, const OptParams& prm);
