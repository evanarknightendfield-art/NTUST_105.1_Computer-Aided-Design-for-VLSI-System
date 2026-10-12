// cbi — Clock Buffer Insertion（你的版本）
//
// 依照 ../TUTORIAL.md 的 Milestone 1 ~ 4，把標了 TODO 的函式補完。
// 每完成一個 Milestone，就用 TUTORIAL 裡的「檢查點」指令確認。
//
// 節點編號約定（之後全部程式都靠這個）：
//   0         = SRC
//   1 .. N    = sinks S1 .. SN
//   N+1 ..    = buffers（輸出時再重新編號成 B1 .. Bm）
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

// =====================================================================
// 資料結構
// =====================================================================
struct BufType {
    std::string name;
    int F = 0;          // fanout 上限
    long long L = 0;    // outgoing 總長上限
    long long C = 0;    // cost
};

struct Problem {
    int srcF = 0;            // SRC fanout 上限
    long long srcL = 0;      // SRC length 上限
    int dimx = 0, dimy = 0;
    std::vector<BufType> lib;
    long long wSkew = 1;
    int srcX = 0, srcY = 0;
    std::vector<int> sinkX, sinkY;   // S1 存在 index 0
};

enum Kind { SRC = 0, SINK = 1, BUF = 2 };

struct Node {
    int x = 0, y = 0;
    int parent = -1;
    int kind = BUF;
    std::vector<int> ch;   // children
};

struct Tree {
    std::vector<Node> nd;
    int N = 0;   // sink 數
};

long long dist(const Node& a, const Node& b) {
    return std::llabs((long long)a.x - b.x) + std::llabs((long long)a.y - b.y);
}

// ---- 已經寫好的小工具（Milestone 3、4 會用到）----

// 建立只有 SRC 與 sinks 的樹
Tree makeTree(const Problem& P) {
    Tree T;
    T.N = (int)P.sinkX.size();
    T.nd.resize(T.N + 1);
    T.nd[0].x = P.srcX;
    T.nd[0].y = P.srcY;
    T.nd[0].kind = SRC;
    for (int i = 1; i <= T.N; ++i) {
        T.nd[i].x = P.sinkX[i - 1];
        T.nd[i].y = P.sinkY[i - 1];
        T.nd[i].kind = SINK;
    }
    return T;
}

int addBuffer(Tree& T, int x, int y) {
    Node b;
    b.x = x;
    b.y = y;
    b.kind = BUF;
    T.nd.push_back(b);
    return (int)T.nd.size() - 1;
}

void link(Tree& T, int child, int parent) {
    T.nd[parent].ch.push_back(child);
    T.nd[child].parent = parent;
}

// =====================================================================
// Milestone 1：讀檔
// =====================================================================
// 提示：
//   - 一行一行讀，先把 '#' 之後的字刪掉，再用 istringstream 切 token
//   - 看到 .limit / .buflib / .objective / .pin 就記住「現在在哪個區塊」，看到 .e 就離開
//   - .dimx / .dimy 在範例裡放在 .limit 區塊內
//   - .pin 的第一個座標是 SRC，其餘依序是 S1, S2, ...
bool readProblem(const std::string& path, Problem& P) {
    std::ifstream in(path);
    if (!in) return false;
    // TODO(M1)
    (void)P;
    return false;
}

// =====================================================================
// Milestone 2：型號選擇、評分、輸出
// =====================================================================

// 回傳「fanout >= k 且 L >= len」的型號中 cost 最小的 index；沒有就回傳 -1
int cheapestType(const Problem& P, int k, long long len) {
    // TODO(M2)
    (void)P; (void)k; (void)len;
    return -1;
}

struct Eval {
    bool legal = false;
    std::string why;   // 不合法的原因（debug 用）
    long long tmax = 0, tmin = 0, cost = 0, score = 0;
    int numBuf = 0;
};

// 從頭檢查 verifier 的所有規則並計分（規則清單見 TUTORIAL 的「規則總整理」）
Eval evaluate(const Problem& P, const Tree& T) {
    Eval e;
    // TODO(M2)
    //   1. 座標不可重疊；buffer 必須 1 <= x <= dimx-1, 1 <= y <= dimy-1
    //   2. 從 SRC 做 BFS，所有 sink 和 buffer 都要走得到
    //   3. 每個 buffer 至少 1 個 child；SRC 檢查 srcF / srcL；buffer 用 cheapestType 檢查
    //   4. BFS 時順便累加 arrival time，算 T_max, T_min, cost, score
    (void)P; (void)T;
    e.why = "evaluate() not implemented";
    return e;
}

// 輸出 .buffer 與 .level 兩個區塊（格式見 TUTORIAL）
bool writeOutput(const Problem& P, const Tree& T, const std::string& path) {
    std::ofstream out(path);
    if (!out) return false;
    // TODO(M2)
    //   - 用 BFS 順序把 buffer 重新編號成 B1, B2, ...（verifier 要求連續編號）
    //   - 第 k 行列出所有「深度 k-1 的 parent」：  k P1:{c1 c2} P2:{c3}
    //   - .level 後面的數字 = 最大深度
    (void)P; (void)T;
    return true;
}

// =====================================================================
// Milestone 3：合法初始解
// =====================================================================
// 從最簡單的做起，先求「一定合法」，再求好。提示見 TUTORIAL。
void buildInitial(const Problem& P, Tree& T) {
    // TODO(M3)
    (void)P; (void)T;
}

// =====================================================================
// Milestone 4：在維持合法的前提下降低 Score
// =====================================================================
void optimize(const Problem& P, Tree& T, double seconds) {
    // TODO(M4)
    (void)P; (void)T; (void)seconds;
}

// =====================================================================
// 以下已經寫好，不用改：自我測試與 main
// =====================================================================

// 手動建出範例 output1.cbi 的樹，檢查 evaluate / writeOutput（應該得到 141 / 36 / 131）
int selfTest(const Problem& P, const std::string& outPath) {
    Tree T = makeTree(P);
    if (T.N != 5) {
        std::fprintf(stderr, "selftest 只能用範例 input.cbi（5 個 sinks）\n");
        return 1;
    }
    int b1 = addBuffer(T, 60, 61);
    int b2 = addBuffer(T, 77, 38);
    link(T, b1, 0);
    link(T, b2, 0);
    link(T, 1, b1); link(T, 3, b1); link(T, 5, b1);   // B1:{S1 S3 S5}
    link(T, 2, b2); link(T, 4, b2);                   // B2:{S2 S4}
    Eval e = evaluate(P, T);
    std::printf("selftest: legal=%d T_max=%lld T_min=%lld cost=%lld Score=%lld %s\n", e.legal, e.tmax, e.tmin, e.cost,
                e.score, e.why.c_str());
    std::printf("expected: legal=1 T_max=141 T_min=36 cost=26 Score=131\n");
    writeOutput(P, T, outPath);
    return 0;
}

int main(int argc, char** argv) {
    auto t0 = std::chrono::steady_clock::now();
    if (argc < 3) {
        std::fprintf(stderr, "error: missing arguments\nusage: %s INPUT_FILE OUTPUT_FILE\n", argv[0]);
        return 1;
    }
    Problem P;
    if (!readProblem(argv[1], P)) {
        std::fprintf(stderr, "error: cannot read %s\n", argv[1]);
        return 1;
    }
    if (argc >= 4 && std::string(argv[3]) == "--selftest") return selfTest(P, argv[2]);

    Tree T = makeTree(P);
    buildInitial(P, T);
    Eval e1 = evaluate(P, T);
    std::fprintf(stderr, "[stage1] legal=%d buffers=%d cost=%lld skew=%lld score=%lld %s\n", e1.legal, e1.numBuf,
                 e1.cost, e1.tmax - e1.tmin, e1.score, e1.why.c_str());

    Tree backup = T;
    double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    optimize(P, T, 5.0 - used);
    Eval e2 = evaluate(P, T);
    if (!e2.legal || e2.score > e1.score) {   // 最佳化出問題就退回初始解，絕對不要交出不合法的檔案
        T = backup;
        e2 = e1;
    }
    std::fprintf(stderr, "[stage2] legal=%d buffers=%d cost=%lld skew=%lld score=%lld\n", e2.legal, e2.numBuf, e2.cost,
                 e2.tmax - e2.tmin, e2.score);

    if (!writeOutput(P, T, argv[2])) {
        std::fprintf(stderr, "error: cannot write %s\n", argv[2]);
        return 1;
    }
    std::printf("T_max: %lld, T_min: %lld, Score: %lld\n", e2.tmax, e2.tmin, e2.score);
    return 0;
}
