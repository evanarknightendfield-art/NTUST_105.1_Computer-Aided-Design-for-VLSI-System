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

    P = Problem{};
    std::string section;
    std::string line;
    int expectedTypes = -1;
    int expectedPins = -1;
    int pinCount = 0;

    while (std::getline(in, line)) {
        // 移除 # 後面的註解。
        auto pos = line.find('#');
        if (pos != std::string::npos) {
            line.erase(pos);
        }

        std::istringstream iss(line);
        std::string token;
        if (!(iss >> token)) continue;  // 空白行直接跳過。

        // 區塊開始與結束。
        if (token == ".limit") {
            section = "limit";
            continue;
        }
        if (token == ".buflib") {
            section = "buflib";
            if (!(iss >> expectedTypes) || expectedTypes <= 0)
                return false;
            continue;
        }
        if (token == ".objective") {
            section = "objective";
            continue;
        }
        if (token == ".pin") {
            section = "pin";
            if (!(iss >> expectedPins) || expectedPins < 2)
                return false;
            continue;
        }
        if (token == ".e") {
            section.clear();
            continue;
        }

        // 依照目前區塊解析這一行。
        if (section == "limit") {
            if (token == "fanout") {
                if (!(iss >> P.srcF)) return false;
            } else if (token == "length") {
                if (!(iss >> P.srcL)) return false;
            } else if (token == ".dimx") {
                if (!(iss >> P.dimx)) return false;
            } else if (token == ".dimy") {
                if (!(iss >> P.dimy)) return false;
            } else {
                return false;
            }
        } else if (section == "buflib") {
            BufType b;
            b.name = token;
            if (!(iss >> b.F >> b.L >> b.C)) return false;
            P.lib.push_back(b);
        } else if (section == "objective") {
            if (token != "w_skew" || !(iss >> P.wSkew))
                return false;
        } else if (section == "pin") {
            // token 已讀走 x，所以重建這一行的串流。
            std::istringstream coords(line);
            int x, y;
            if (!(coords >> x >> y)) return false;

            if (pinCount == 0) {
                P.srcX = x;
                P.srcY = y;
            } else {
                P.sinkX.push_back(x);
                P.sinkY.push_back(y);
            }
            ++pinCount;
        } else {
            return false;
        }
    }

    return !in.bad()
        && expectedTypes == static_cast<int>(P.lib.size())
        && expectedPins == pinCount
        && P.srcF > 0 && P.srcL > 0
        && P.dimx > 0 && P.dimy > 0
        && P.wSkew > 0;
}

// =====================================================================
// Milestone 2：型號選擇、評分、輸出
// =====================================================================

// 回傳「fanout >= k 且 L >= len」的型號中 cost 最小的 index；沒有就回傳 -1
int cheapestType(const Problem& P, int k, long long len) {
    int best = -1;

    for (int i = 0; i < static_cast<int>(P.lib.size()); ++i) {
        const BufType& b = P.lib[i];

        // 任一限制不符合，就跳過這個型號。
        if (b.F < k || b.L < len) continue;

        if (best == -1 || b.C < P.lib[best].C) {
            best = i;
        }
    }

    return best;
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

    // 不合法時，回傳原因。
    auto fail = [&](const std::string& reason) {
        e.legal = false;
        e.why = reason;
        return e;
    };

    int n = static_cast<int>(T.nd.size());

    // 1. 檢查基本結構。
    if (T.N <= 0 ||
        T.N != static_cast<int>(P.sinkX.size()) ||
        P.sinkX.size() != P.sinkY.size() ||
        n <= T.N) {
        return fail("invalid node count");
    }

    if (T.nd[0].kind != SRC ||
        T.nd[0].parent != -1 ||
        T.nd[0].x != P.srcX ||
        T.nd[0].y != P.srcY) {
        return fail("invalid SRC");
    }

    for (int i = 1; i < n; ++i) {
        const Node& v = T.nd[i];

        if (i <= T.N) {
            // Sink 的位置必須與輸入一致，而且不能有 children。
            if (v.kind != SINK ||
                v.x != P.sinkX[i - 1] ||
                v.y != P.sinkY[i - 1] ||
                !v.ch.empty()) {
                return fail("invalid sink");
            }
        } else {
            if (v.kind != BUF) {
                return fail("invalid buffer kind");
            }

            if (v.x <= 0 || v.x >= P.dimx ||
                v.y <= 0 || v.y >= P.dimy) {
                return fail("buffer outside interior");
            }

            if (v.ch.empty()) {
                return fail("buffer has no children");
            }

            ++e.numBuf;
        }
    }

    // 2. 依座標排序，檢查是否有兩個節點重疊。
    std::vector<int> order(n);
    for (int i = 0; i < n; ++i) order[i] = i;

    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (T.nd[a].x != T.nd[b].x)
            return T.nd[a].x < T.nd[b].x;
        return T.nd[a].y < T.nd[b].y;
    });

    for (int i = 1; i < n; ++i) {
        const Node& a = T.nd[order[i - 1]];
        const Node& b = T.nd[order[i]];

        if (a.x == b.x && a.y == b.y) {
            return fail("overlapping coordinates");
        }
    }

    // 3. 用 vector 當 BFS queue。
    std::vector<int> queue;
    std::vector<bool> seen(n, false);
    std::vector<long long> arrival(n, 0);

    queue.push_back(0);
    seen[0] = true;

    for (size_t head = 0; head < queue.size(); ++head) {
        int u = queue[head];
        const Node& parent = T.nd[u];
        long long totalLength = 0;

        for (int v : parent.ch) {
            if (v <= 0 || v >= n) {
                return fail("invalid child ID");
            }

            // 重複遇到節點：可能有多個 parent、重複邊或 cycle。
            if (seen[v]) {
                return fail("repeated child or cycle");
            }

            if (T.nd[v].parent != u) {
                return fail("parent-child mismatch");
            }

            long long length = dist(parent, T.nd[v]);
            totalLength += length;
            arrival[v] = arrival[u] + length;

            seen[v] = true;
            queue.push_back(v);
        }

        int k = static_cast<int>(parent.ch.size());

        if (u == 0) {
            if (k > P.srcF || totalLength > P.srcL) {
                return fail("SRC exceeds fanout or length");
            }
        } else if (parent.kind == BUF) {
            int type = cheapestType(P, k, totalLength);

            if (type == -1) {
                return fail("no legal buffer type");
            }

            e.cost += P.lib[type].C;
        }
    }

    // 所有節點都必須從 SRC 走得到。
    if (static_cast<int>(queue.size()) != n) {
        return fail("unreachable nodes");
    }

    // 4. 只用 sinks 的 arrival time 計算 skew。
    e.tmin = LLONG_MAX;
    e.tmax = LLONG_MIN;

    for (int i = 1; i <= T.N; ++i) {
        e.tmin = std::min(e.tmin, arrival[i]);
        e.tmax = std::max(e.tmax, arrival[i]);
    }

    e.score = e.cost + P.wSkew * (e.tmax - e.tmin);
    e.legal = true;
    return e;
}

// 輸出 .buffer 與 .level 兩個區塊（格式見 TUTORIAL）
bool writeOutput(const Problem& P, const Tree& T,
                 const std::string& path) {
    // 先確認樹合法，才能安全地走訪與輸出。
    Eval e = evaluate(P, T);
    if (!e.legal) {
        std::cerr << "cannot write invalid tree: " << e.why << "\n";
        return false;
    }

    int n = static_cast<int>(T.nd.size());
    std::vector<int> queue;
    std::vector<int> depth(n, 0);
    std::vector<std::string> name(n);

    name[0] = "SRC";
    for (int i = 1; i <= T.N; ++i) {
        name[i] = "S" + std::to_string(i);
    }

    // 1. BFS：計算深度，並重新編號 buffers。
    queue.push_back(0);
    int bufferCount = 0;
    int maxDepth = 0;

    for (size_t head = 0; head < queue.size(); ++head) {
        int u = queue[head];

        for (int v : T.nd[u].ch) {
            depth[v] = depth[u] + 1;
            maxDepth = std::max(maxDepth, depth[v]);

            if (T.nd[v].kind == BUF) {
                name[v] = "B" + std::to_string(++bufferCount);
            }

            queue.push_back(v);
        }
    }

    // 2. 把有 children 的 parent，依深度分組。
    std::vector<std::vector<int>> parentsAtDepth(maxDepth);

    for (int u : queue) {
        if (!T.nd[u].ch.empty()) {
            parentsAtDepth[depth[u]].push_back(u);
        }
    }

    std::ofstream out(path);
    if (!out) return false;

    // 3. 輸出 buffer 型號與位置。
    out << ".buffer " << bufferCount << "\n";

    for (int u : queue) {
        const Node& b = T.nd[u];
        if (b.kind != BUF) continue;

        long long totalLength = 0;
        for (int v : b.ch) {
            totalLength += dist(b, T.nd[v]);
        }

        int type = cheapestType(
            P, static_cast<int>(b.ch.size()), totalLength
        );

        out << name[u] << " " << P.lib[type].name
            << " " << b.x << " " << b.y << "\n";
    }

    out << ".e\n\n";

    // 4. 第 k 行，列出深度 k-1 的 parents 與其 children。
    out << ".level " << maxDepth << "\n";

    for (int k = 1; k <= maxDepth; ++k) {
        out << k;

        for (int u : parentsAtDepth[k - 1]) {
            out << " " << name[u] << ":{";

            const auto& children = T.nd[u].ch;
            for (size_t j = 0; j < children.size(); ++j) {
                if (j > 0) out << " ";
                out << name[children[j]];
            }

            out << "}";
        }

        out << "\n";
    }

    out << ".e\n";
    out.close();
    return !out.fail();
}

bool chooseBufferPosition(
    const Problem& P,
    const Tree& T,
    const std::vector<int>& children,
    int& outX,
    int& outY
) {
    if (children.empty() || P.dimx <= 1 || P.dimy <= 1) {
        return false;
    }

    int maxFanout = 0;
    for (const auto& b : P.lib) {
        maxFanout = std::max(maxFanout, b.F);
    }

    if (static_cast<int>(children.size()) > maxFanout) {
        return false;
    }

    // 1. 分別取得 x、y 的中位數。
    std::vector<int> xs, ys;

    for (int v : children) {
        xs.push_back(T.nd[v].x);
        ys.push_back(T.nd[v].y);
    }

    std::sort(xs.begin(), xs.end());
    std::sort(ys.begin(), ys.end());

    int mx = xs[xs.size() / 2];
    int my = ys[ys.size() / 2];

    // Buffer 必須嚴格位於晶片內部。
    mx = std::clamp(mx, 1, P.dimx - 1);
    my = std::clamp(my, 1, P.dimy - 1);

    // 2. 檢查候選位置。
    auto tryPosition = [&](int x, int y) {
        if (x <= 0 || x >= P.dimx ||
            y <= 0 || y >= P.dimy) {
            return false;
        }

        // 不可與 SRC、sink 或既有 buffer 重疊。
        for (const auto& node : T.nd) {
            if (node.x == x && node.y == y) {
                return false;
            }
        }

        Node candidate;
        candidate.x = x;
        candidate.y = y;

        long long totalLength = 0;
        for (int v : children) {
            totalLength += dist(candidate, T.nd[v]);
        }

        if (cheapestType(
                P, static_cast<int>(children.size()), totalLength
            ) == -1) {
            return false;
        }

        outX = x;
        outY = y;
        return true;
    };

    // 3. 先試中位數，再逐圈搜尋附近的位置。
    // 每一圈的 Manhattan 半徑是 radius。
    for (int radius = 0; radius <= 8; ++radius) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int dy = radius - std::abs(dx);

            if (tryPosition(mx + dx, my + dy)) {
                return true;
            }

            if (dy != 0 &&
                tryPosition(mx + dx, my - dy)) {
                return true;
            }
        }
    }

    return false;
}

bool chooseRelayPosition(
    const Problem& P, const Tree& T,
    int child, int& outX, int& outY
) {
    if (P.dimx <= 1 || P.dimy <= 1) return false;

    // Relay 只有一個 child，找出可用型號的最大線長。
    long long maxLength = 0;
    for (const auto& b : P.lib) {
        if (b.F >= 1) {
            maxLength = std::max(maxLength, b.L);
        }
    }

    long long oldDistance = dist(T.nd[child], T.nd[0]);
    if (oldDistance <= 1 || maxLength <= 0) return false;

    auto tryPosition = [&](int x, int y) {
        if (x <= 0 || x >= P.dimx ||
            y <= 0 || y >= P.dimy) {
            return false;
        }

        Node candidate;
        candidate.x = x;
        candidate.y = y;

        // 新的 frontier 節點必須更靠近 SRC。
        if (dist(candidate, T.nd[0]) >= oldDistance) {
            return false;
        }

        // Relay 到 child 的距離必須有合法型號能支撐。
        long long length = dist(candidate, T.nd[child]);
        if (cheapestType(P, 1, length) == -1) {
            return false;
        }

        for (const auto& node : T.nd) {
            if (node.x == x && node.y == y) {
                return false;
            }
        }

        outX = x;
        outY = y;
        return true;
    };

    // 試兩條走法：先沿 x 靠近 SRC，或先沿 y。
    for (int axis = 0; axis < 2; ++axis) {
        int x = T.nd[child].x;
        int y = T.nd[child].y;
        long long remaining =
            std::min(maxLength, oldDistance - 1);

        auto advance = [&](int& coordinate, int target) {
            long long delta = (long long)target - coordinate;
            long long step =
                std::min(remaining, std::llabs(delta));

            coordinate +=
                static_cast<int>(delta > 0 ? step : -step);
            remaining -= step;
        };

        if (axis == 0) {
            advance(x, P.srcX);
            advance(y, P.srcY);
        } else {
            advance(y, P.srcY);
            advance(x, P.srcX);
        }

        x = std::clamp(x, 1, P.dimx - 1);
        y = std::clamp(y, 1, P.dimy - 1);

        // 候選點被占用時，再找附近空位。
        for (int radius = 0; radius <= 32; ++radius) {
            for (int dx = -radius; dx <= radius; ++dx) {
                int dy = radius - std::abs(dx);

                if (tryPosition(x + dx, y + dy)) {
                    return true;
                }

                if (dy != 0 &&
                    tryPosition(x + dx, y - dy)) {
                    return true;
                }
            }
        }
    }

    return false;
}


// =====================================================================
// Milestone 3：合法初始解
// =====================================================================
// 從最簡單的做起，先求「一定合法」，再求好。提示見 TUTORIAL。
void buildInitial(const Problem& P, Tree& T) {
    std::vector<int> frontier;

    for (int i = 1; i <= T.N; ++i) {
        frontier.push_back(i);
    }

    int round = 0;

    while (!frontier.empty()) {
        ++round;

        // 1. 檢查目前的 frontier 能否全部接到 SRC。
        long long srcLength = 0;
        for (int v : frontier) {
            srcLength += dist(T.nd[0], T.nd[v]);
        }

        if (static_cast<int>(frontier.size()) <= P.srcF &&
            srcLength <= P.srcL) {
            for (int v : frontier) {
                link(T, v, 0);
            }
            return;
        }

        // 2. 計算 Morton key，交錯排列 x、y 的位元。
        std::vector<unsigned long long> keys(T.nd.size());
            
        for (int v : frontier) {
            unsigned int x = static_cast<unsigned int>(T.nd[v].x);
            unsigned int y = static_cast<unsigned int>(T.nd[v].y);
            unsigned long long key = 0;
        
            for (int bit = 0; bit < 32; ++bit) {
                // x 的位元放在第 0、2、4……位。
                key |= static_cast<unsigned long long>(
                    (x >> bit) & 1U
                ) << (2 * bit);
            
                // y 的位元放在第 1、3、5……位。
                key |= static_cast<unsigned long long>(
                    (y >> bit) & 1U
                ) << (2 * bit + 1);
            }
        
            keys[v] = key;
        }
        
        std::sort(
            frontier.begin(), frontier.end(),
            [&](int a, int b) {
                if (keys[a] != keys[b]) return keys[a] < keys[b];
                return a < b;
            }
        );

        std::vector<int> next;
        bool merged = false;

        // 3. 從左到右，貪心地建立合法群組。
        size_t i = 0;

        while (i < frontier.size()) {
            std::vector<int> group = {frontier[i]};
            size_t j = i + 1;

            int bestX = 0;
            int bestY = 0;

            while (j < frontier.size()) {
                group.push_back(frontier[j]);

                int x, y;
                if (!chooseBufferPosition(P, T, group, x, y)) {
                    group.pop_back();
                    break;
                }

                bestX = x;
                bestY = y;
                ++j;
            }

            if (group.size() >= 2) {
                // 找到可合併的群：新增 parent buffer。
                int b = addBuffer(T, bestX, bestY);

                for (int v : group) {
                    link(T, v, b);
                }

                next.push_back(b);
                merged = true;
            } else {
                // 無法與下一個節點合併，先保留到下一輪。
                next.push_back(frontier[i]);
            }

            i = j;
        }

        std::cerr << "[construct] round=" << round
                  << " frontier=" << frontier.size()
                  << " -> " << next.size() << "\n";

        // 基本版尚未處理需要 relay buffer 的情況。
        if (!merged) {
            bool moved = false;

            for (int& v : next) {
                int x, y;

                if (!chooseRelayPosition(P, T, v, x, y)) {
                    continue;
                }

                int b = addBuffer(T, x, y);
                link(T, v, b);

                // 下一輪處理新 buffer，原節點已接在它下面。
                v = b;
                moved = true;
            }

            if (!moved) {
                std::cerr
                    << "construction stalled: no relay position found\n";
                std::exit(1);
            }
        }
        frontier = std::move(next);
    }
}

// =====================================================================
// Milestone 4：在維持合法的前提下降低 Score
// =====================================================================
void optimize(const Problem& P, Tree& T, double seconds) {
    // 沒有時間，或沒有 buffer，就不用最佳化。
    if (seconds <= 0 ||
        static_cast<int>(T.nd.size()) <= T.N + 1) {
        return;
    }

    auto start = std::chrono::steady_clock::now();

    Eval current = evaluate(P, T);
    if (!current.legal) return;

    // 固定 seed，方便重現與除錯。
    std::mt19937 rng(42);

    std::uniform_int_distribution<int> pickBuffer(
        T.N + 1, static_cast<int>(T.nd.size()) - 1
    );
    std::uniform_int_distribution<int> pickScale(0, 5);

    long long trials = 0;
    long long improved = 0;

    while (std::chrono::duration<double>(
               std::chrono::steady_clock::now() - start
           ).count() < seconds) {
        // 1. 隨機挑一個 buffer。
        int u = pickBuffer(rng);

        // 移動半徑：1、2、4、8、16、32。
        int radius = 1 << pickScale(rng);
        std::uniform_int_distribution<int> offset(-radius, radius);

        // 2. 保存原位置，再產生候選位置。
        int oldX = T.nd[u].x;
        int oldY = T.nd[u].y;

        int newX = std::clamp(
            oldX + offset(rng), 1, P.dimx - 1
        );
        int newY = std::clamp(
            oldY + offset(rng), 1, P.dimy - 1
        );

        if (newX == oldX && newY == oldY) continue;

        T.nd[u].x = newX;
        T.nd[u].y = newY;
        ++trials;

        // 3. 移動會影響多條邊，重新驗證整棵樹。
        Eval candidate = evaluate(P, T);

        if (candidate.legal &&
            candidate.score <= current.score) {
            if (candidate.score < current.score) {
                ++improved;
            }
            current = candidate;
        } else {
            // 不合法或分數變差，就還原。
            T.nd[u].x = oldX;
            T.nd[u].y = oldY;
        }
    }

    std::cerr << "[optimize] trials=" << trials
              << " improved=" << improved << "\n";
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

    if (argc >= 4 && std::string(argv[3]) == "--typetest") {
    auto check = [&](int k, long long len) {
        int idx = cheapestType(P, k, len);

        std::cout << "k=" << k << " len=" << len << " -> ";
        if (idx == -1) {
            std::cout << "NONE\n";
        } else {
            std::cout << P.lib[idx].name
                      << " cost=" << P.lib[idx].C << "\n";
        }
    };

    check(3, 90);   // 剛好達到 SMALL 的兩項上限。
    check(4, 90);   // SMALL 的 fanout 不夠。
    check(3, 91);   // SMALL 的線長限制不夠。
    check(9, 190);  // 剛好達到 LARGE 的兩項上限。
    check(10, 90);  // 所有型號的 fanout 都不夠。
    check(1, 191);  // 所有型號的線長限制都不夠。
    return 0;
    }

    if (argc >= 4 &&
        std::string(argv[3]) == "--positiontest") {
        if (P.sinkX.size() != 5) {
            std::cerr << "positiontest requires the example input\n";
            return 1;
    }

    Tree testTree = makeTree(P);
    std::vector<int> children = {1, 3, 5};

    int x, y;
    if (!chooseBufferPosition(P, testTree, children, x, y)) {
        std::cerr << "no position found\n";
        return 1;
    }

    Node candidate;
    candidate.x = x;
    candidate.y = y;

    long long totalLength = 0;
    for (int v : children) {
        totalLength += dist(candidate, testTree.nd[v]);
    }

    int type = cheapestType(
        P, static_cast<int>(children.size()), totalLength
    );

    std::cout << "position=(" << x << "," << y << ")"
              << " length=" << totalLength
              << " type=" << P.lib[type].name << "\n";
        return 0;
    }

    if (argc >= 4 && std::string(argv[3]) == "--selftest") return selfTest(P, argv[2]);

    Tree T = makeTree(P);
    buildInitial(P, T);
    Eval e1 = evaluate(P, T);
    std::fprintf(stderr, "[stage1] legal=%d buffers=%d cost=%lld skew=%lld score=%lld %s\n", e1.legal, e1.numBuf,
                 e1.cost, e1.tmax - e1.tmin, e1.score, e1.why.c_str());

    Tree backup = T;
    double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    
    double optSeconds = std::min(
    5.0, std::max(0.0, 55.0 - used)
    );
    optimize(P, T, optSeconds);

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
