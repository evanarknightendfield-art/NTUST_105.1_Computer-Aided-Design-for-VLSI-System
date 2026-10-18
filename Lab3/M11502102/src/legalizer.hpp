// legalizer.hpp — Stage 1：合法化，讓總位移和最大位移都小
#pragma once
#include <string>

#include "placedb.hpp"

struct LegalizeOptions {
    std::string method = "tetris";   // 做完 M4 之後改成 "abacus"
    bool refine = false;             // M5（進階）做完之後改成 true
    bool verbose = false;
};

// 有 cell 放不下時回傳 false（err 寫原因）
bool legalize(const PlaceDB& db, Placement& out, const LegalizeOptions& opt, std::string& err);
