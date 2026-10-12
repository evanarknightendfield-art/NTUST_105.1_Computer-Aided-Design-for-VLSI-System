
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
#include <cmath>
#include <climits>
#include <random>

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

enum Kind { SRC = 0, SINK = 1, BUF = 2, DEAD = 3 };

struct Node {
    int x = 0, y = 0;
    int parent = -1;
    int kind = BUF;
    std::vector<int> children;
    int type = -1;
    long long sumLen = 0;
    long long lo = 0, hi = 0;
    long long cnt = 0;
    double s1 = 0, s2 = 0;
};

class Tree {
public:
    explicit Tree(const Problem& P) : P_(&P) {
        num_sinks_ = (int)P.sink_x.size();
        nodes_.resize(num_sinks_ + 1);
        nodes_[0].x = P.src_x;
        nodes_[0].y = P.src_y;
        nodes_[0].kind = SRC;
        for (int i = 1; i <= num_sinks_; ++i) {
            nodes_[i].x = P.sink_x[i - 1];
            nodes_[i].y = P.sink_y[i - 1];
            nodes_[i].kind = SINK;
            nodes_[i].cnt = 1;
        }
        for (const Node& v : nodes_) occupied_.insert(key(v.x, v.y));
    }

    long long totalCost() const { return total_cost_; }
    long long tmax() const { return nodes_[0].hi; }
    long long tmin() const { return nodes_[0].lo; }
    long long score() const { return total_cost_ + P_->w_skew * (tmax() - tmin()); }

    double sigma() const {
        const Node& r = nodes_[0];
        if (r.cnt == 0) return 0;
        double mean = r.s1 / r.cnt;
        return std::sqrt(std::max(0.0, r.s2 / r.cnt - mean * mean));
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
        occupied_.insert(key(x, y));
        if (!free_.empty()) {
            int id = free_.back();
            free_.pop_back();
            nodes_[id] = b;
            return id;
        }
        nodes_.push_back(b);
        return (int)nodes_.size() - 1;
    }

    void link(int child, int parent) {
        if (nodes_[child].parent >= 0) unlink(child);
        nodes_[parent].children.push_back(child);
        nodes_[child].parent = parent;
        refreshUp(parent);
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
        refreshUp(p);
    }

    void removeBuffer(int b) {
        unlink(b);
        occupied_.erase(key(nodes_[b].x, nodes_[b].y));
        total_cost_ -= typeCost(nodes_[b].type);
        nodes_[b].type = -1;
        nodes_[b].kind = DEAD;
        free_.push_back(b);
    }

    void revive(int b, int parent) {
        free_.pop_back();
        nodes_[b].kind = BUF;
        occupied_.insert(key(nodes_[b].x, nodes_[b].y));
        recompute(b);
        link(b, parent);
    }

    void moveTo(int v, int x, int y) {
        occupied_.erase(key(nodes_[v].x, nodes_[v].y));
        occupied_.insert(key(x, y));
        nodes_[v].x = x;
        nodes_[v].y = y;
        refreshUp(v);
        if (nodes_[v].parent >= 0) refreshUp(nodes_[v].parent);
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
    long long typeCost(int t) const { return t < 0 ? 0 : P_->buflib[t].cost; }

    bool recompute(int v) {
        Node& n = nodes_[v];
        long long lo = 0, hi = 0, cnt = 0, sum = 0;
        double s1 = 0, s2 = 0;
        if (n.kind == SINK) {
            cnt = 1;
        } else {
            lo = LLONG_MAX;
            hi = LLONG_MIN;
            for (int c : n.children) {
                const Node& m = nodes_[c];
                long long e = std::llabs((long long)n.x - m.x) + std::llabs((long long)n.y - m.y);
                sum += e;
                if (m.cnt == 0) continue;
                lo = std::min(lo, e + m.lo);
                hi = std::max(hi, e + m.hi);
                cnt += m.cnt;
                double de = (double)e;
                s1 += m.s1 + (double)m.cnt * de;
                s2 += m.s2 + 2.0 * de * m.s1 + (double)m.cnt * de * de;
            }
            if (cnt == 0) lo = hi = 0;
        }
        n.sumLen = sum;
        if (n.kind == BUF) {
            int t = n.children.empty() ? -1 : P_->cheapestType((int)n.children.size(), sum);
            total_cost_ += typeCost(t) - typeCost(n.type);
            n.type = t;
        }
        bool changed = lo != n.lo || hi != n.hi || cnt != n.cnt || s1 != n.s1 || s2 != n.s2;
        n.lo = lo;
        n.hi = hi;
        n.cnt = cnt;
        n.s1 = s1;
        n.s2 = s2;
        return changed;
    }

    void refreshUp(int v) {
        while (v >= 0) {
            if (!recompute(v)) break;
            v = nodes_[v].parent;
        }
    }

    static long long key(int x, int y) { return ((long long)x << 32) | (unsigned int)y; }

    std::vector<Node> nodes_;
    int num_sinks_ = 0;
    std::unordered_set<long long> occupied_;
    std::vector<int> free_;
    const Problem* P_ = nullptr;
    long long total_cost_ = 0;
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
    int alive = 0;
    for (int v = 0; v < n; ++v) {
        if (T.node(v).kind == DEAD) continue;
        keys.push_back(((long long)T.node(v).x << 32) | (unsigned int)T.node(v).y);
        ++alive;
    }
    std::sort(keys.begin(), keys.end());
    for (int i = 1; i < alive; ++i) {
        if (keys[i] == keys[i - 1]) { e.why = "overlapping coordinates"; return e; }
    }

    for (int v = T.numSinks() + 1; v < n; ++v) {
        const Node& b = T.node(v);
        if (b.kind == DEAD) continue;
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
    if ((int)queue.size() != alive) { e.why = "some nodes unreachable"; return e; }

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

bool nodeOK(const Problem& P, const Tree& T, int v) {
    const Node& a = T.node(v);
    if (a.kind == DEAD || a.kind == SINK) return true;
    if (a.kind == SRC) return (int)a.children.size() <= P.fanout && a.sumLen <= P.length;
    return insideChip(P, a.x, a.y) && !a.children.empty() && a.type >= 0;
}

int criticalNode(const Tree& T, bool minPath, std::mt19937& rng) {
    std::vector<int> path;
    int u = 0;
    while (!T.node(u).children.empty()) {
        int next = -1;
        for (int c : T.node(u).children) {
            long long e = T.dist(u, c);
            if (T.node(c).cnt == 0) continue;
            if (minPath ? e + T.node(c).lo == T.node(u).lo : e + T.node(c).hi == T.node(u).hi) {
                next = c;
                break;
            }
        }
        if (next < 0) break;
        path.push_back(next);
        u = next;
    }
    if (path.empty()) return -1;
    return path[rng() % path.size()];
}

void optimize(const Problem& P, Tree& T, double seconds, unsigned seed) {
    auto start = std::chrono::steady_clock::now();
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    const double beta = 0.3;

    long long minCost = P.buflib[0].cost;
    for (const Bufferlibrary& b : P.buflib) minCost = std::min(minCost, b.cost);
    double c = (double)std::max(1LL, minCost);
    double f0 = std::min(0.3, std::max(0.03, 0.3 * std::sqrt(50.0 / std::max(1, T.numSinks()))));
    double T0 = f0 * c;
    double T1 = std::min(T0, 0.01 * std::min(c, (double)P.w_skew));
    double temp = T0;

    auto energy = [&]() { return (double)T.score() + beta * (double)P.w_skew * T.sigma(); };
    double current = energy();
    Tree best = T;
    long long bestScore = T.score();
    bool bestDirty = false;

    int maxLog = 0;
    while ((1 << (maxLog + 1)) <= std::max(P.dimx, P.dimy) / 2) ++maxLog;

    auto accept = [&]() {
        double next = energy();
        double d = next - current;
        if (d > 0 && uni(rng) >= std::exp(-d / temp)) return false;
        current = next;
        if (T.score() < bestScore) {
            bestScore = T.score();
            bestDirty = true;
        }
        return true;
    };

    long long iter = 0, accepted = 0;
    while (true) {
        if ((iter & 1023) == 0) {
            double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (used >= seconds) break;
            temp = T0 * std::pow(T1 / T0, used / seconds);
            if (bestDirty) {
                best = T;
                bestDirty = false;
            }
        }
        ++iter;
        int move = (int)(rng() % 10);
        int v = -1;
        if (rng() % 10 < 3) v = criticalNode(T, move < 2 ? true : (rng() % 2 == 0), rng);
        if (v < 0) v = 1 + (int)(rng() % (T.size() - 1));
        if (T.node(v).kind == DEAD) continue;

        if (move < 2) {
            int p = T.node(v).parent;
            int r = 1 << (rng() % (maxLog + 1));
            int x = T.node(v).x + (int)(rng() % (2 * r + 1)) - r;
            int y = T.node(v).y + (int)(rng() % (2 * r + 1)) - r;
            if (!insideChip(P, x, y) || !T.isFree(x, y)) continue;
            int nb = T.addBuffer(x, y);
            T.link(v, nb);
            T.link(nb, p);
            if (nodeOK(P, T, nb) && nodeOK(P, T, p) && accept()) {
                ++accepted;
                continue;
            }
            T.link(v, p);
            T.removeBuffer(nb);
        } else if (move < 6) {
            if (T.node(v).kind != BUF) continue;
            int r = 1 << (rng() % (maxLog + 1));
            int x = T.node(v).x + (int)(rng() % (2 * r + 1)) - r;
            int y = T.node(v).y + (int)(rng() % (2 * r + 1)) - r;
            if (!insideChip(P, x, y) || !T.isFree(x, y)) continue;
            int oldX = T.node(v).x, oldY = T.node(v).y;
            T.moveTo(v, x, y);
            if (nodeOK(P, T, v) && nodeOK(P, T, T.node(v).parent) && accept()) {
                ++accepted;
                continue;
            }
            T.moveTo(v, oldX, oldY);
        } else {
            int p = T.node(v).parent;
            int gp = T.node(p).parent;
            if (gp < 0) continue;
            std::vector<int> cand = {gp};
            for (int ch : T.node(gp).children)
                if (ch != p && T.node(ch).kind == BUF) cand.push_back(ch);
            int q = cand[rng() % cand.size()];

            T.link(v, q);
            std::vector<std::pair<int, int>> removed;
            int u = p;
            while (T.node(u).kind == BUF && T.node(u).children.empty()) {
                int up = T.node(u).parent;
                T.removeBuffer(u);
                removed.push_back({u, up});
                u = up;
            }
            if (nodeOK(P, T, q) && accept()) {
                ++accepted;
                continue;
            }
            for (int i = (int)removed.size() - 1; i >= 0; --i) T.revive(removed[i].first, removed[i].second);
            T.link(v, p);
        }
    }
    if (T.score() <= bestScore) best = T;
    T = best;
    std::cerr << "[optimize] iterations=" << iter << " accepted=" << accepted << " T0=" << T0 << " T1=" << T1 << "\n";
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

    double seconds = std::min(20.0, 2.0 + T.numSinks() / 500.0);
    int runs = T.numSinks() <= 50 ? 4 : 1;
    Tree best = T;
    for (int r = 0; r < runs; ++r) {
        T = best;
        optimize(P, T, seconds / runs, 1 + 7919u * r);
        Eval e2 = evaluate(P, T);
        if (e2.legal && e2.score == T.score() && e2.score < e.score) {
            e = e2;
            best = T;
        }
    }
    T = best;
    logStage("stage2", e, start);

    if (!T.write(P, argv[2])) {
        std::cerr << "error: cannot write " << argv[2] << "\n";
        return 1;
    }
    std::cout << "T_max: " << e.tmax << ", T_min: " << e.tmin << ", Score: " << e.score << "\n";
    return 0;
}
