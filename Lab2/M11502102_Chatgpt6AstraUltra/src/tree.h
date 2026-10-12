// tree.h — clock tree 的資料結構
//
// 節點編號：0 = SRC，1..N = sinks S1..SN，N+1 以後 = buffers（可動態新增 / 刪除）
//
// 每個節點維護「子樹統計」：
//   lo / hi : 從這個節點往下走到它子樹裡所有 sink 的最短 / 最長路徑長（latency）
//   cnt     : 子樹裡的 sink 數
//   s1 / s2 : 子樹裡 sink latency 的總和 / 平方和（用來算標準差 σ 當作最佳化的輔助指標）
// 因此整棵樹的 T_max = nd[0].hi、T_min = nd[0].lo、skew = hi - lo。
//
// 修改一個節點後，只需要沿著祖先往上重算（refreshUp），每層 O(fanout)。
//
// 交易（transaction）：begin() 之後的所有修改都會記在 journal，
// rollback() 可以完整還原，commit() 則確定修改。最佳化就是「試做 → 評估 → 接受或還原」。
#pragma once
#include <cstdint>
#include <cstdlib>
#include <unordered_set>
#include <utility>
#include <vector>

#include "problem.h"

enum NodeKind { SRC_NODE = 0, SINK_NODE = 1, BUF_NODE = 2 };

struct Node {
    int x = 0, y = 0;
    int parent = -1;
    int kind = BUF_NODE;
    int type = -1;          // buffer 型號（由 recompute 自動挑最便宜的合法型號）
    bool alive = false;
    std::vector<int> ch;    // children
    long long sumLen = 0;   // Σ d(this, child)
    long long lo = 0, hi = 0;
    long long cnt = 0;
    double s1 = 0, s2 = 0;
};

inline long long manhattan(long long x1, long long y1, long long x2, long long y2) {
    return std::llabs(x1 - x2) + std::llabs(y1 - y2);
}

// 已佔用座標表：晶片不太大時用 bitmap（查詢只要一次位元運算），否則退回 hash set。
// 位在晶片範圍外的座標（理論上不會有）一律放進 hash set。
class Occupancy {
public:
    void reset(int dimx, int dimy) {
        W_ = (long long)dimx + 1;
        H_ = (long long)dimy + 1;
        useBits_ = W_ > 0 && H_ > 0 && W_ * H_ <= (1LL << 28);   // 最多 2^28 bits = 32 MB
        bits_.assign(useBits_ ? (size_t)((W_ * H_ + 63) / 64) : 0, 0);
        set_.clear();
    }
    bool has(long long x, long long y) const {
        if (inBits(x, y)) {
            long long i = x * H_ + y;
            return (bits_[(size_t)(i >> 6)] >> (i & 63)) & 1ULL;
        }
        return set_.count(key(x, y)) != 0;
    }
    void insert(long long x, long long y) {
        if (inBits(x, y)) {
            long long i = x * H_ + y;
            bits_[(size_t)(i >> 6)] |= 1ULL << (i & 63);
        } else {
            set_.insert(key(x, y));
        }
    }
    void erase(long long x, long long y) {
        if (inBits(x, y)) {
            long long i = x * H_ + y;
            bits_[(size_t)(i >> 6)] &= ~(1ULL << (i & 63));
        } else {
            set_.erase(key(x, y));
        }
    }

private:
    bool inBits(long long x, long long y) const { return useBits_ && x >= 0 && y >= 0 && x < W_ && y < H_; }
    static long long key(long long x, long long y) { return (x << 32) ^ (y & 0xffffffffLL); }
    long long W_ = 0, H_ = 0;
    bool useBits_ = false;
    std::vector<uint64_t> bits_;
    std::unordered_set<long long> set_;
};

// 只存「決定一棵樹」所需的最少資訊，載入時再重建 children 與統計
struct Snapshot {
    std::vector<int> x, y, parent, kind;
    std::vector<char> alive;
};

class Tree {
public:
    const Problem* P = nullptr;
    const TypeTable* TT = nullptr;
    std::vector<Node> nd;
    int N = 0;                 // sink 數
    long long totalCost = 0;   // 所有 buffer 的 cost 總和
    int numBuf = 0;

    void init(const Problem& p, const TypeTable& tt);

    long long dist(int a, int b) const { return manhattan(nd[a].x, nd[a].y, nd[b].x, nd[b].y); }
    bool inside(long long x, long long y) const {   // verifier 要求 0 < x < dimx, 0 < y < dimy
        return x >= 1 && x <= P->dimx - 1 && y >= 1 && y <= P->dimy - 1;
    }
    bool isFree(long long x, long long y) const { return !occ_.has(x, y); }
    bool usable(long long x, long long y) const { return inside(x, y) && isFree(x, y); }

    // ---- 基本編輯操作（在 transaction 中會自動記錄） ----
    int addBuffer(int x, int y);       // 新增一個還沒連線的 buffer
    void removeBuffer(int b);          // 刪除 buffer（必須已無 parent、無 children）
    void link(int c, int p);           // c 成為 p 的 child
    void unlink(int c);                // 把 c 從它的 parent 拔下
    void moveTo(int b, int x, int y);  // 移動 buffer

    bool recompute(int v);             // 由 children 重算 v 的統計與型號；統計有變回傳 true
    void refreshUp(int v);             // 從 v 往上重算到統計不再改變為止
    void recomputeAll();               // 整棵樹重算

    bool isAncestorOrSelf(int a, int v) const;
    bool attachedToRoot(int v) const;

    // ---- transaction ----
    void begin();
    bool touchedLegal() const;   // 這次交易碰過的節點是否都合法
    // 平行分工用：這次交易「結構上」改到的節點（位置、parent、children、存活）是否都屬於區域 k，
    // 且沒有移動 / 換掉區域根（isRoot）。新長出來的節點自動標成區域 k。
    bool touchedOnlyRegion(std::vector<signed char>& region, const std::vector<char>& isRoot, signed char k) const;
    void commit();
    void rollback();

    // ---- 目標函數 ----
    long long tmax() const { return nd[0].hi; }
    long long tmin() const { return nd[0].lo; }
    long long skew() const { return nd[0].hi - nd[0].lo; }
    long long score() const { return totalCost + P->wSkew * skew(); }
    double sigma() const;

    bool nodeLegal(int v) const;

    void save(Snapshot& s) const;
    void load(const Snapshot& s);

    // Record every accepted best without copying the entire tree each time.
    // Only structural changes since the preceding checkpoint need to be copied.
    void startCheckpoints(Snapshot& s);
    void saveCheckpoint(Snapshot& s);
    void stopCheckpoints();

private:
    void touch(int v);
    void trackChanges();
    bool tracking_ = false;
    bool savedTrial_ = false;
    std::vector<unsigned char> dirtyMark_;
    std::vector<int> dirty_;
    void occInsert(int x, int y);
    void occErase(int x, int y);

    Occupancy occ_;                       // 所有已被佔用的座標（SRC、sinks、buffers）
    std::vector<int> freeList_;           // 已刪除、可重複使用的節點編號

    bool inTx_ = false;
    int stamp_ = 1;
    std::vector<int> mark_;
    std::vector<std::pair<int, Node>> jNode_;          // 被修改節點的原始內容
    struct OccOp { int x, y; char ins; };
    std::vector<OccOp> jOcc_;                          // ins: 1 = insert, 0 = erase
    std::vector<std::pair<int, char>> jFree_;          // 0 = 從 free list 取出, 1 = 放回, 2 = 新開節點
    long long jCost_ = 0;
    int jNumBuf_ = 0;
};
