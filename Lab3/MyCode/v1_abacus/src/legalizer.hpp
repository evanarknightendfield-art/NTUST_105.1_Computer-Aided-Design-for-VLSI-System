// legalizer.hpp — Stage 1：合法化，讓總位移和最大位移都小
#pragma once
#include <string>

#include "placedb.hpp"

struct LegalizeOptions {
    std::string method = "abacus";   // "tetris"（v1，比較用）或 "abacus"
    bool totalCost = true;           // Abacus 選 row：v 的位移 + 被合併 cells 的位移變化（false = 只看 v 自己）
    bool refine = false;             // M5（進階）做完之後改成 true
    bool verbose = false;
};

// 有 cell 放不下時回傳 false（err 寫原因）
bool legalize(const PlaceDB& db, Placement& out, const LegalizeOptions& opt, std::string& err);
