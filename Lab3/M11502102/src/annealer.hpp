// annealer.hpp — Stage 2：模擬退火降低總 HPWL，每一步都保持合法
#pragma once
#include <cstdint>
#include <string>

#include "placedb.hpp"

struct SAOptions {
    uint64_t seed = 1;
    double timeLimit = 60;        // 秒
    double movesPerCell = 1000;   // 總嘗試次數 = movesPerCell × movable cell 數
    std::string tracePath;        // 報告要畫圖：每隔一段寫一行 iteration,current,best,...
    bool verbose = false;
    // 你自己加的參數（初始溫度、降溫速率、window 大小……）放這裡
};

struct SAResult {
    double initial = 0, best = 0;
    long long attempts = 0, accepted = 0;
    double seconds = 0;
};

// 直接改 P；回傳時 P 是過程中看過最好的合法 placement
SAResult anneal(const PlaceDB& db, Placement& P, const SAOptions& opt);
