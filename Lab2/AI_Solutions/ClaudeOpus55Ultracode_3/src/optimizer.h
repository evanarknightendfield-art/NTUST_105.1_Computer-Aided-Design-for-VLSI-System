// optimizer.h — Stage 3：在「永遠合法」的前提下用模擬退火降低 Score
#pragma once
#include <cstdint>
#include <vector>

#include "tree.h"

struct OptParams {
    double timeLimit = 5.0;   // 秒
    uint64_t seed = 1;
    double beta = 0.3;        // 輔助項 σ（arrival time 標準差）的權重
    bool verbose = false;
    // 這次呼叫只跑整個降溫曲線 T0 -> T1 的 [schedFrom, schedTo] 這一段。
    // 平行版同步時接著上一段繼續降溫，不會每次同步都重新加熱。
    double schedFrom = 0.0, schedTo = 1.0;
    // 平行分工：region != nullptr 時，只能改動 (*region)[v] == regionId 的節點（區域根 isRoot 不能移動）
    std::vector<signed char>* region = nullptr;
    const std::vector<char>* isRoot = nullptr;
    const std::vector<int>* roots = nullptr;   // 自己這組的區域根（找「自己區域內」的關鍵路徑用）
    signed char regionId = -1;
};

struct OptReport {
    long long iters = 0, accepted = 0;
    double seconds = 0;
};

// 執行完畢後 T 為搜尋過程中 Score 最低的解
OptReport optimize(Tree& T, const OptParams& prm);
