// annealer.hpp — Stage 2：模擬退火降低總 HPWL，每一步都保持合法
#pragma once
#include <cstdint>
#include <string>

#include "placedb.hpp"

struct SAOptions {
    uint64_t seed = 1;
    double timeLimit = 40;        // 秒，從 anneal() 被呼叫開始算（v3 起由 main 依全程期限算好）
    double movesPerCell = 20000;  // 總嘗試次數上限 = movesPerCell × movable cell 數（v4：實際由時間決定，v3 以前是 2000）
    int steps = 100;              // 溫度個數：T0 → T0 × tEndRatio，等比下降
    double p0 = 0.8;              // 初始溫度下，平均的變差 move 被接受的機率
    double lowEffort = 500;       // v3：時間只夠每個 cell 做不到這麼多次 move 時 ...
    double p0Low = 0.05;          // ... 改用這個 p0（從低溫開始）
    double polishShare = 0.15;     // v4：SA 時間的最後這一段給 polish
    double polishOnlyBelow = 0;   // v4：時間只夠每個 cell 做不到這麼多次 move 時，不做 SA、全部給 polish
    double tEndRatio = 1e-3;
    double pTarget = 0.5;         // v2：move 的目標點取 a 的 optimal region 的機率
    double win0 = 0.05;           // 初始 range window：晶片寬、高的幾分之幾（v2 依接受率調整）
    double minWinCells = 4;       // v2：window 最小 = 這麼多個平均 cell 寬 ...
    double minWinRows = 1.5;      // ... × 這麼多個 row
    bool clampTarget = false;     // optimal region 的目標也限制在 window 裡（實驗用，比較差）
    std::string tracePath;        // 報告要畫圖：每隔一段寫一行 iteration,current,best,...
    bool verbose = false;
};

struct SAResult {
    double initial = 0, best = 0;
    long long attempts = 0, accepted = 0;
    double seconds = 0;
};

// 直接改 P；回傳時 P 是過程中看過最好的合法 placement
SAResult anneal(const PlaceDB& db, Placement& P, const SAOptions& opt);
