// solution_io.h — 獨立的合法性檢查（不信任增量資料）與輸出檔
#pragma once
#include <string>

#include "tree.h"

struct EvalResult {
    bool legal = false;
    std::string why;        // 不合法的原因
    long long tmax = 0, tmin = 0, cost = 0, score = 0;
    int numBuf = 0, levels = 0;
};

// 只根據每個節點的 parent 與座標，從頭檢查所有 verifier 規則並計分
EvalResult evaluate(const Tree& T);

// 依 BFS 順序輸出 .buffer 與 .level；buffer 編號 B1..Bm 依 BFS 順序
bool writeSolution(const Tree& T, const std::string& path, std::string& err);
