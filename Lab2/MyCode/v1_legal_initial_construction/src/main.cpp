
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <unordered_set>
#include <chrono>
#include <iomanip>

struct Bufferlibrary{
    std::string type;
    int       fanout = 0;
    long long length = 0;
    long long cost   = 0;
};

struct Problem{
    // .limit
    int       fanout = 0;            // maximum fanouts for SRC
    long long length = 0;            // maximum length  for SRC
    int       dimx   = 0;            // chip dimension in x-axis
    int       dimy   = 0;            // chip dimension in y-axis
    // .buflib
    std::vector<Bufferlibrary> buflib;
    // .objective
    long long w_skew = 1;
    // .pin
    int       src_x  = 0;            // clock SRC coordinates in x-axis
    int       src_y  = 0;            // clock SRC coordinates in y-axis
    std::vector<int> sink_x, sink_y; // clock sink coordinates

    int cheapestType(int k, long long len) const {
        int best = -1;
        for (int i = 0; i < (int)buflib.size(); ++i) {
            const Bufferlibrary& b = buflib[i];
            if (b.fanout < k || b.length < len) continue;
            if (best == -1 || b.cost < buflib[best].cost) best = i;
        }
        return best;
    }
};

bool readProblem(const std::string& path, Problem& P) {
    std::ifstream in(path);
    if (!in) return false;

    std::string line;
    std::string section;
    int pinCount = 0;

    while (std::getline(in, line)) {
        line = line.substr(0, line.find('#'));
        std::istringstream ss(line);
        std::string tok;
        if (!(ss >> tok)) continue;

        if (tok == ".e") { section = ""; continue; }
        if (tok == ".limit" || tok == ".buflib" || tok == ".objective" || tok == ".pin") {
            section = tok;
            continue;
        }

        if (section == ".limit") {
            if      (tok == "fanout") ss >> P.fanout;
            else if (tok == "length") ss >> P.length;
            else if (tok == ".dimx")  ss >> P.dimx;
            else if (tok == ".dimy")  ss >> P.dimy;
        }
        else if (section == ".buflib") {
            Bufferlibrary b;
            b.type = tok;
            ss >> b.fanout >> b.length >> b.cost;
            P.buflib.push_back(b);
        }
        else if (section == ".objective") {
            if (tok == "w_skew") ss >> P.w_skew;
        }
        else if (section == ".pin") { 
            int x = std::stoi(tok);
            int y = 0;
            ss >> y;
            if (pinCount == 0) {
                P.src_x = x;
                P.src_y = y;
            } else {
                P.sink_x.push_back(x);
                P.sink_y.push_back(y);
            }
            ++pinCount;
        }
    }
    return true;
}

enum Kind { SRC = 0, SINK = 1, BUF = 2 };

struct Node {
    int x = 0, y = 0;
    int parent = -1;
    int kind = BUF;
    std::vector<int> children;
};

class Tree {
public:
    explicit Tree(const Problem& P) {
        num_sinks_ = (int)P.sink_x.size();
        nodes_.resize(num_sinks_ + 1);
        nodes_[0].x = P.src_x;
        nodes_[0].y = P.src_y;
        nodes_[0].kind = SRC;
        for (int i = 1; i <= num_sinks_; ++i) {
            nodes_[i].x = P.sink_x[i - 1];
            nodes_[i].y = P.sink_y[i - 1];
            nodes_[i].kind = SINK;
        }
        for (const Node& v : nodes_) occupied_.insert(key(v.x, v.y));
    }

    int size() const { return (int)nodes_.size(); }
    int numSinks() const { return num_sinks_; }
    const Node& node(int v) const { return nodes_[v]; }
    bool isFree(int x, int y) const { return occupied_.count(key(x, y)) == 0; }

    long long dist(int a, int b) const {
        return std::llabs((long long)nodes_[a].x - nodes_[b].x) +
               std::llabs((long long)nodes_[a].y - nodes_[b].y);
    }

    int addBuffer(int x, int y) {
        Node b;
        b.x = x;
        b.y = y;
        b.kind = BUF;
        nodes_.push_back(b);
        occupied_.insert(key(x, y));
        return (int)nodes_.size() - 1;
    }

    void link(int child, int parent) {
        if (nodes_[child].parent >= 0) unlink(child);
        nodes_[parent].children.push_back(child);
        nodes_[child].parent = parent;
    }

    void unlink(int child) {
        int p = nodes_[child].parent;
        if (p < 0) return;
        std::vector<int>& ch = nodes_[p].children;
        for (int i = 0; i < (int)ch.size(); ++i) {
            if (ch[i] == child) {
                ch[i] = ch.back();
                ch.pop_back();
                break;
            }
        }
        nodes_[child].parent = -1;
    }

    void moveTo(int v, int x, int y) {
        occupied_.erase(key(nodes_[v].x, nodes_[v].y));
        occupied_.insert(key(x, y));
        nodes_[v].x = x;
        nodes_[v].y = y;
    }

    bool write(const Problem& P, const std::string& path) const {
        const int n = size();

        std::vector<int> order;
        std::vector<int> depth(n, 0);
        order.push_back(0);
        for (int i = 0; i < (int)order.size(); ++i) {
            int u = order[i];
            for (int c : nodes_[u].children) {
                depth[c] = depth[u] + 1;
                order.push_back(c);
            }
        }

        std::vector<std::string> name(n);
        name[0] = "SRC";
        for (int s = 1; s <= num_sinks_; ++s) name[s] = "S" + std::to_string(s);
        int numBuf = 0;
        for (int v : order) {
            if (nodes_[v].kind == BUF) {
                ++numBuf;
                name[v] = "B" + std::to_string(numBuf);
            }
        }

        int maxDepth = 0;
        for (int v : order) maxDepth = std::max(maxDepth, depth[v]);

        std::ofstream out(path);
        if (!out) return false;

        out << ".buffer " << numBuf << "\n";
        for (int v : order) {
            const Node& b = nodes_[v];
            if (b.kind != BUF) continue;
            long long len = 0;
            for (int c : b.children) len += dist(v, c);
            int t = P.cheapestType((int)b.children.size(), len);
            out << name[v] << " " << P.buflib[t].type << " " << b.x << " " << b.y << "\n";
        }
        out << ".e\n\n";

        out << ".level " << maxDepth << "\n";
        for (int k = 1; k <= maxDepth; ++k) {
            out << k;
            for (int v : order) {
                if (depth[v] != k - 1 || nodes_[v].children.empty()) continue;
                out << " " << name[v] << ":{";
                const std::vector<int>& ch = nodes_[v].children;
                for (int j = 0; j < (int)ch.size(); ++j) {
                    if (j > 0) out << " ";
                    out << name[ch[j]];
                }
                out << "}";
            }
            out << "\n";
        }
        out << ".e\n";
        return true;
    }

private:
    static long long key(int x, int y) { return ((long long)x << 32) | (unsigned int)y; }

    std::vector<Node> nodes_;
    int num_sinks_ = 0;
    std::unordered_set<long long> occupied_;
};

struct Eval {
    bool legal = false;
    std::string why;
    long long tmax = 0, tmin = 0, cost = 0, score = 0;
    int buffers = 0;
};

Eval evaluate(const Problem& P, const Tree& T) {
    Eval e;
    const int n = T.size();
    std::vector<long long> keys;
    for (int v = 0; v < n; ++v)
        keys.push_back(((long long)T.node(v).x << 32) | (unsigned int)T.node(v).y);
    std::sort(keys.begin(), keys.end());
    for (int i = 1; i < n; ++i) {
        if (keys[i] == keys[i - 1]) { e.why = "overlapping coordinates"; return e; }
    }

    for (int v = T.numSinks() + 1; v < n; ++v) {
        const Node& b = T.node(v);
        if (b.x <= 0 || b.x >= P.dimx || b.y <= 0 || b.y >= P.dimy) { e.why = "buffer outside chip"; return e; }
        if (b.children.empty()) { e.why = "buffer without children"; return e; }
        long long len = 0;
        for (int c : b.children) len += T.dist(v, c);
        int t = P.cheapestType((int)b.children.size(), len);
        if (t == -1) { e.why = "buffer over fanout/length limit"; return e; }
        e.cost += P.buflib[t].cost;
        ++e.buffers;
    }

    long long srcLen = 0;
    for (int c : T.node(0).children) srcLen += T.dist(0, c);
    if ((int)T.node(0).children.size() > P.fanout || srcLen > P.length) { e.why = "SRC over limit"; return e; }

    std::vector<long long> arrival(n, 0);
    std::vector<int> queue;
    queue.push_back(0);
    for (int i = 0; i < (int)queue.size(); ++i) {
        int u = queue[i];
        for (int c : T.node(u).children) {
            arrival[c] = arrival[u] + T.dist(u, c);
            queue.push_back(c);
        }
    }
    if ((int)queue.size() != n) { e.why = "some nodes unreachable"; return e; }

    e.tmax = arrival[1];
    e.tmin = arrival[1];
    for (int s = 1; s <= T.numSinks(); ++s) {
        e.tmax = std::max(e.tmax, arrival[s]);
        e.tmin = std::min(e.tmin, arrival[s]);
    }
    e.score = e.cost + P.w_skew * (e.tmax - e.tmin);
    e.legal = true;
    return e;
}

bool insideChip(const Problem& P, long long x, long long y) {
    return x >= 1 && x <= P.dimx - 1 && y >= 1 && y <= P.dimy - 1;
}

long long groupLength(const Tree& T, const std::vector<int>& group, long long x, long long y) {
    long long len = 0;
    for (int v : group) len += std::llabs(x - T.node(v).x) + std::llabs(y - T.node(v).y);
    return len;
}

bool findBufferSpot(const Problem& P, const Tree& T, const std::vector<int>& group, int& bx, int& by) {
    std::vector<int> xs, ys;
    for (int v : group) {
        xs.push_back(T.node(v).x);
        ys.push_back(T.node(v).y);
    }
    std::sort(xs.begin(), xs.end());
    std::sort(ys.begin(), ys.end());
    int mx = xs[xs.size() / 2];
    int my = ys[ys.size() / 2];

    int k = (int)group.size();
    if (P.cheapestType(k, groupLength(T, group, mx, my)) == -1) return false;

    mx = std::max(1, std::min(P.dimx - 1, mx));
    my = std::max(1, std::min(P.dimy - 1, my));
    for (int r = 0; r <= 16; ++r) {
        for (int dx = -r; dx <= r; ++dx) {
            int dy = r - std::abs(dx);
            for (int sy : {1, -1}) {
                if (dy == 0 && sy == -1) continue;
                int x = mx + dx, y = my + sy * dy;
                if (!insideChip(P, x, y) || !T.isFree(x, y)) continue;
                if (P.cheapestType(k, groupLength(T, group, x, y)) == -1) continue;
                bx = x;
                by = y;
                return true;
            }
        }
    }
    return false;
}

bool findRelaySpot(const Problem& P, const Tree& T, int v, int& bx, int& by) {
    long long maxLen = 0;
    for (const Bufferlibrary& b : P.buflib) maxLen = std::max(maxLen, b.length);
    long long d = T.dist(v, 0);
    long long step = std::min(maxLen, d - 1);
    if (step <= 0) return false;

    const Node& a = T.node(v);
    long long tx = a.x, ty = a.y;
    long long mxStep = std::min(step, std::llabs((long long)P.src_x - a.x));
    tx += (P.src_x > a.x) ? mxStep : -mxStep;
    long long myStep = std::min(step - mxStep, std::llabs((long long)P.src_y - a.y));
    ty += (P.src_y > a.y) ? myStep : -myStep;
    tx = std::max(1LL, std::min((long long)P.dimx - 1, tx));
    ty = std::max(1LL, std::min((long long)P.dimy - 1, ty));

    for (int r = 0; r <= 32; ++r) {
        for (int dx = -r; dx <= r; ++dx) {
            int dy = r - std::abs(dx);
            for (int sy : {1, -1}) {
                if (dy == 0 && sy == -1) continue;
                long long x = tx + dx, y = ty + sy * dy;
                if (!insideChip(P, x, y) || !T.isFree((int)x, (int)y)) continue;
                long long toChild = std::llabs(x - a.x) + std::llabs(y - a.y);
                long long toSrc = std::llabs(x - P.src_x) + std::llabs(y - P.src_y);
                if (toChild > maxLen || toSrc >= d) continue;
                bx = (int)x;
                by = (int)y;
                return true;
            }
        }
    }
    return false;
}

long long mortonKey(int x, int y) {
    long long key = 0;
    for (int bit = 0; bit < 31; ++bit) {
        key |= (long long)((x >> bit) & 1) << (2 * bit);
        key |= (long long)((y >> bit) & 1) << (2 * bit + 1);
    }
    return key;
}

bool buildInitial(const Problem& P, Tree& T) {
    std::vector<int> frontier;
    for (int s = 1; s <= T.numSinks(); ++s) frontier.push_back(s);

    for (int round = 0; round < 100000; ++round) {
        long long srcLen = 0;
        for (int v : frontier) srcLen += T.dist(0, v);
        if ((int)frontier.size() <= P.fanout && srcLen <= P.length) {
            for (int v : frontier) T.link(v, 0);
            return true;
        }

        std::sort(frontier.begin(), frontier.end(), [&](int a, int b) {
            return mortonKey(T.node(a).x, T.node(a).y) < mortonKey(T.node(b).x, T.node(b).y);
        });

        std::vector<int> next;
        bool merged = false;
        int i = 0;
        while (i < (int)frontier.size()) {
            std::vector<int> group = {frontier[i]};
            int bx = 0, by = 0;
            int j = i + 1;
            while (j < (int)frontier.size()) {
                group.push_back(frontier[j]);
                int x, y;
                if (!findBufferSpot(P, T, group, x, y)) {
                    group.pop_back();
                    break;
                }
                bx = x;
                by = y;
                ++j;
            }
            if (group.size() >= 2) {
                int b = T.addBuffer(bx, by);
                for (int v : group) T.link(v, b);
                next.push_back(b);
                merged = true;
            } else {
                next.push_back(frontier[i]);
            }
            i = j;
        }

        if (!merged) {
            bool moved = false;
            for (int& v : next) {
                int x, y;
                if (!findRelaySpot(P, T, v, x, y)) continue;
                int b = T.addBuffer(x, y);
                T.link(v, b);
                v = b;
                moved = true;
            }
            if (!moved) return false;
        }
        frontier = next;
    }
    return false;
}

void logStage(const char* name, const Eval& e, std::chrono::steady_clock::time_point start) {
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cerr << "[" << name << "] legal=" << e.legal << " buffers=" << e.buffers << " cost=" << e.cost
              << " skew=" << e.tmax - e.tmin << " T_max=" << e.tmax << " T_min=" << e.tmin
              << " score=" << e.score << " time=" << std::fixed << std::setprecision(2) << sec << "s";
    if (!e.legal) std::cerr << " why=" << e.why;
    std::cerr << "\n";
}

int main(int argc, char** argv) {
    auto start = std::chrono::steady_clock::now();
    if (argc < 3) {
        std::cerr << "error: missing arguments\n";
        std::cerr << "usage: " << argv[0] << " INPUT_FILE OUTPUT_FILE\n";
        return 1;
    }
    Problem P;
    if (!readProblem(argv[1], P)) {
        std::cerr << "error: cannot read " << argv[1] << "\n";
        return 1;
    }

    Tree T(P);
    if (!buildInitial(P, T)) {
        std::cerr << "error: cannot build a legal tree\n";
        return 1;
    }
    Eval e = evaluate(P, T);
    logStage("stage1", e, start);

    if (!T.write(P, argv[2])) {
        std::cerr << "error: cannot write " << argv[2] << "\n";
        return 1;
    }
    std::cout << "T_max: " << e.tmax << ", T_min: " << e.tmin << ", Score: " << e.score << "\n";
    return 0;
}
