// legalizer.hpp — Stage 1：合法化，讓總位移和最大位移都小
#pragma once
#include <chrono>
#include <chrono>
#include <string>

#include "placedb.hpp"

struct LegalizeOptions {
    std::string method = "abacus";   // "tetris"（v1，比較用）或 "abacus"
    bool totalCost = true;           // Abacus 選 row：v 的位移 + 被合併 cells 的位移變化（false = 只看 v 自己）
    // v3：Abacus 的 (b) cost 要把被合併的 cells 一個一個走過，大設計的熱點太慢。
    //     走過的 cell 數累計到 exactWork 之前都照算；之後只有合併 <= exactCells 個 cell 才照算，更大的用上界
    double exactWork = 1e9;
    int exactCells = 16;
    bool refineMax = true;           // v3：微調最大位移（insert / swap）
    bool refineTotal = true;         // v3：微調總位移
    bool evict = true;               // v3：最大位移卡住時，把擋路的 cell 先移走
    // v3：Stage 1 + Stage 2 要在 runEnd 之前做完；Abacus 做完後，微調最多用掉剩下時間的 refineShare
    std::chrono::steady_clock::time_point runEnd = std::chrono::steady_clock::time_point::max();
    double refineShare = 0.3;
    bool verbose = false;
};

// 有 cell 放不下時回傳 false（err 寫原因）
bool legalize(const PlaceDB& db, Placement& out, const LegalizeOptions& opt, std::string& err);
