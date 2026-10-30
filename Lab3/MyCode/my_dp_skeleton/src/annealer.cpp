// annealer.cpp — Stage 2（Milestone 6）
//
// 狀態：一個合法的 placement。每個 move 都設計成「做完一定還是合法」，
// 所以 cost 只有 HPWL，不需要 overlap penalty。
#include "annealer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>

#include "rowmap.hpp"

namespace {

struct Rng {   // xoshiro256**：比 std::mt19937 快，SA 會呼叫上億次
    uint64_t s[4];
    explicit Rng(uint64_t seed) {
        for (auto& x : s) {
            seed += 0x9e3779b97f4a7c15ULL;
            uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            x = z ^ (z >> 31);
        }
    }
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t next() {
        uint64_t r = rotl(s[1] * 5, 7) * 9, t = s[1] << 17;
        s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
        s[2] ^= t; s[3] = rotl(s[3], 45);
        return r;
    }
    double u01() { return (next() >> 11) * 0x1.0p-53; }             // [0, 1)
    double uniform(double a, double b) { return a + (b - a) * u01(); }
    int below(int n) { return (int)((next() >> 33) % (uint64_t)n); }  // [0, n)
};

// 一個 move：1 個 cell 搬家，或 2 個 cell 交換。記的是「新位置」。
struct Move {
    int n = 0;
    int v[2], seg[2], site[2];
};

class Annealer {
public:
    Annealer(const PlaceDB& db, Placement& P, const SAOptions& opt)
        : db_(db), P_(P), opt_(opt), rm_(db, P), rng_(opt.seed) {}

    SAResult run() {
        SAResult res;
        // TODO(M6) 照講義 L06_1 的 SA 流程：
        //
        //   cur = 目前總 HPWL；best = cur
        //   T = 初始溫度（例如：先試做幾千個 move，取「變差的 move 平均變差多少」
        //       讓它被接受的機率是 p0：T0 = -avg / ln(p0)）
        //   repeat（每一輪 = 一個溫度）
        //       repeat k 次
        //           propose(m)：產生一個合法的 move，產生不出來就算這次失敗
        //           d = delta(m)（HPWL 變化，只重算受影響的 net）
        //           d <= 0 或 rand < exp(-d / T) → commit(m)，否則還原
        //           如果 cur < best → 記下 best（最後要輸出最好的那個！）
        //       T *= alpha
        //   until 溫度夠低 / 時間到
        //
        // 報告要畫 current 和 best-so-far 對 iteration 的圖：
        //   if (!opt_.tracePath.empty()) 每隔一段 iteration 寫一行 "iteration,current,best"
        //   畫圖：python3 Lab3/Grader/tools/plot_trace.py trace.csv -o trace.png
        //   （plot_trace.py 需要的欄位：iteration,current,best,temperature,accept_rate）
        (void)db_; (void)P_; (void)opt_; (void)rm_; (void)rng_;
        return res;
    }

private:
    // TODO(M6) 一條 net 的 HPWL（用 cell 中心）
    // TODO(M6) propose：
    //   隨機挑一個 cell a，再挑一個目標點（a 附近的隨機點，或 a 的 optimal region）
    //   目標點是空位而且放得下 → 搬過去
    //   目標點上有 cell b → a、b 交換（a 放得進 b 的空間、b 放得進 a 的空間才行）
    //   用 rm_.cellAt / rm_.freeAround 判斷「放得下」
    // TODO(M6) delta / commit / revert

    const PlaceDB& db_;
    Placement& P_;
    SAOptions opt_;
    RowMap rm_;
    Rng rng_;
};

}  // namespace

SAResult anneal(const PlaceDB& db, Placement& P, const SAOptions& opt) {
    return Annealer(db, P, opt).run();
}
