// problem.cpp — 解析 .cbi 輸入檔、建立 buffer 型號查表
#include "problem.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace {

// 去掉 '#' 之後的註解與前後空白
std::string clean(const std::string& line) {
    std::string s = line.substr(0, line.find('#'));
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> tokens(const std::string& s) {
    std::istringstream ss(s);
    std::vector<std::string> t;
    std::string w;
    while (ss >> w) t.push_back(w);
    return t;
}

}  // namespace

bool readProblem(const std::string& path, Problem& P, std::string& err) {
    std::ifstream in(path);
    if (!in) {
        err = "cannot open input file: " + path;
        return false;
    }
    std::vector<std::vector<int>> pins;
    std::string section;   // 目前所在的區塊（.limit / .buflib / .objective / .pin），"" 代表不在區塊內
    std::string line;
    try {
        while (std::getline(in, line)) {
            std::vector<std::string> t = tokens(clean(line));
            if (t.empty()) continue;
            const std::string& h = t[0];
            if (h == ".e") { section.clear(); continue; }
            if (h == ".limit" || h == ".buflib" || h == ".objective" || h == ".pin") { section = h; continue; }
            // .dimx / .dimy 範例放在 .limit 裡，但也允許出現在外面
            if (h == ".dimx" && t.size() >= 2) { P.dimx = std::stoi(t[1]); continue; }
            if (h == ".dimy" && t.size() >= 2) { P.dimy = std::stoi(t[1]); continue; }

            if (section == ".limit" && t.size() >= 2) {
                if (h == "fanout") P.srcF = std::stoi(t[1]);
                else if (h == "length") P.srcL = std::stoll(t[1]);
            } else if (section == ".buflib" && t.size() >= 4) {
                BufType b;
                b.name = h;
                b.F = std::stoi(t[1]);
                b.L = std::stoll(t[2]);
                b.C = std::stoll(t[3]);
                P.lib.push_back(b);
            } else if (section == ".objective" && t.size() >= 2) {
                if (h == "w_skew") P.wSkew = std::stoll(t[1]);
            } else if (section == ".pin" && t.size() >= 2) {
                pins.push_back({std::stoi(t[0]), std::stoi(t[1])});
            }
        }
    } catch (const std::exception& e) {
        err = std::string("parse error near line: ") + line;
        return false;
    }
    if (pins.empty()) { err = "no .pin data"; return false; }
    if (P.lib.empty()) { err = "empty .buflib"; return false; }
    if (P.dimx <= 0 || P.dimy <= 0) { err = "bad chip dimension"; return false; }
    P.srcX = pins[0][0];
    P.srcY = pins[0][1];
    for (size_t i = 1; i < pins.size(); ++i) {
        P.sinkX.push_back(pins[i][0]);
        P.sinkY.push_back(pins[i][1]);
    }
    return true;
}

void TypeTable::build(const std::vector<BufType>& lib) {
    lib_ = &lib;
    maxF_ = 0;
    minCost_ = -1;
    for (const BufType& t : lib) {
        if (t.F <= 0 || t.L < 0) continue;
        maxF_ = std::max(maxF_, t.F);
        if (minCost_ < 0 || t.C < minCost_) minCost_ = t.C;
    }
    if (minCost_ < 0) minCost_ = 0;
    lens_.assign(maxF_ + 1, {});
    best_.assign(maxF_ + 1, {});
    for (int k = 1; k <= maxF_; ++k) {
        std::vector<int> ids;
        for (int i = 0; i < (int)lib.size(); ++i)
            if (lib[i].F >= k && lib[i].L >= 0) ids.push_back(i);
        std::sort(ids.begin(), ids.end(), [&](int a, int b) { return lib[a].L < lib[b].L; });
        int m = (int)ids.size();
        lens_[k].resize(m);
        best_[k].resize(m);
        int cur = -1;
        for (int i = m - 1; i >= 0; --i) {
            int t = ids[i];
            if (cur < 0 || lib[t].C < lib[cur].C) cur = t;
            lens_[k][i] = lib[t].L;
            best_[k][i] = cur;
        }
    }
}

int TypeTable::pick(int k, long long len) const {
    if (k < 1) k = 1;
    if (k > maxF_) return -1;
    const std::vector<long long>& v = lens_[k];
    auto it = std::lower_bound(v.begin(), v.end(), len);
    if (it == v.end()) return -1;
    return best_[k][it - v.begin()];
}

long long TypeTable::maxLen(int k) const {
    if (k < 1) k = 1;
    if (k > maxF_ || lens_[k].empty()) return -1;
    return lens_[k].back();
}
