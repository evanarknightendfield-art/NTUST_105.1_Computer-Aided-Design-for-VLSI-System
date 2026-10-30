// problem.h — 輸入資料 (Problem) 與 buffer 型號查表 (TypeTable)
#pragma once
#include <algorithm>
#include <string>
#include <vector>

struct BufType {
    std::string name;
    int F = 0;          // fanout 上限
    long long L = 0;    // 所有 outgoing 邊長總和上限
    long long C = 0;    // cost
};

struct Problem {
    int srcF = 0;            // SRC fanout 上限
    long long srcL = 0;      // SRC length 上限
    int dimx = 0, dimy = 0;  // 晶片範圍 (0,0) ~ (dimx,dimy)
    std::vector<BufType> lib;
    long long wSkew = 1;
    int srcX = 0, srcY = 0;
    std::vector<int> sinkX, sinkY;   // S1..SN 存在 index 0..N-1
    int numSinks() const { return (int)sinkX.size(); }
};

// 讀入 .cbi，失敗時回傳 false 並在 err 寫原因
bool readProblem(const std::string& path, Problem& P, std::string& err);

// 給定「children 數 k」與「總邊長 len」，找出最便宜的合法 buffer type。
// 這個查詢在最佳化時會被呼叫上百萬次，所以先預處理：
//   對每個 k，把 F>=k 的型號依 L 排序，再做 cost 的 suffix-min，查詢時二分搜尋。
class TypeTable {
public:
    void build(const std::vector<BufType>& lib);
    // 回傳型號 index，沒有合法型號回傳 -1（最佳化時每秒呼叫上千萬次，所以寫成 inline）
    int pick(int k, long long len) const {
        if (k < 1) k = 1;
        if (k > maxF_) return -1;
        const std::vector<long long>& v = lens_[k];
        auto it = std::lower_bound(v.begin(), v.end(), len);
        if (it == v.end()) return -1;
        return best_[k][it - v.begin()];
    }
    long long maxLen(int k) const;                 // F>=k 的型號裡最大的 L（沒有則 -1）
    long long cost(int t) const { return t < 0 ? 0 : (*lib_)[t].C; }
    const std::string& name(int t) const { return (*lib_)[t].name; }
    int maxF() const { return maxF_; }
    long long minCost() const { return minCost_; }

private:
    const std::vector<BufType>* lib_ = nullptr;
    int maxF_ = 0;
    long long minCost_ = 0;
    std::vector<std::vector<long long>> lens_;   // lens_[k][i]：F>=k 的型號依 L 由小到大排序，第 i 個的 L
    std::vector<std::vector<int>> best_;         // best_[k][i]：lens_[k][i..] 裡 cost 最小的型號
};
