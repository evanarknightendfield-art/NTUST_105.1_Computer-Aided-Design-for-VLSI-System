// solution_io.cpp — 合法性檢查與輸出
#include "solution_io.h"

#include <algorithm>
#include <climits>
#include <fstream>
#include <unordered_set>
#include <vector>

namespace {

// 根據 parent 指標重建 children，回傳 BFS 順序（只含從 SRC 走得到的節點）
std::vector<int> bfsOrder(const Tree& T, std::vector<std::vector<int>>& kids) {
    size_t n = T.nd.size();
    kids.assign(n, {});
    for (size_t v = 1; v < n; ++v)
        if (T.nd[v].alive && T.nd[v].parent >= 0) kids[T.nd[v].parent].push_back((int)v);
    for (auto& k : kids) std::sort(k.begin(), k.end());
    std::vector<int> order{0};
    std::vector<char> seen(n, 0);
    seen[0] = 1;
    for (size_t i = 0; i < order.size(); ++i)
        for (int c : kids[order[i]])
            if (!seen[c]) { seen[c] = 1; order.push_back(c); }
    return order;
}

}  // namespace

EvalResult evaluate(const Tree& T) {
    EvalResult r;
    const Problem& P = *T.P;
    size_t n = T.nd.size();

    // 1. 座標不可重疊；buffer 必須在晶片內部
    std::unordered_set<long long> seen;
    for (size_t v = 0; v < n; ++v) {
        const Node& a = T.nd[v];
        if (!a.alive) continue;
        long long k = ((long long)a.x << 32) ^ (a.y & 0xffffffffLL);
        if (!seen.insert(k).second) { r.why = "overlapping coordinates"; return r; }
        if (a.kind == BUF_NODE && !T.inside(a.x, a.y)) { r.why = "buffer on/outside boundary"; return r; }
    }
    // 2. parent 只能是 SRC 或 buffer
    for (size_t v = 1; v < n; ++v) {
        const Node& a = T.nd[v];
        if (!a.alive) continue;
        if (a.parent < 0 || !T.nd[a.parent].alive || T.nd[a.parent].kind == SINK_NODE) {
            r.why = "bad parent of node " + std::to_string(v);
            return r;
        }
    }
    // 3. 每個節點都要從 SRC 走得到（同時保證沒有 cycle）
    std::vector<std::vector<int>> kids;
    std::vector<int> order = bfsOrder(T, kids);
    size_t alive = 0;
    for (size_t v = 0; v < n; ++v) alive += T.nd[v].alive;
    if (order.size() != alive) { r.why = "unreachable nodes / cycle"; return r; }

    // 4. fanout / length；計算 arrival time 與層數
    std::vector<long long> arr(n, 0);
    std::vector<int> depth(n, 0);
    for (int u : order) {
        const Node& a = T.nd[u];
        long long sum = 0;
        for (int c : kids[u]) {
            long long e = T.dist(u, c);
            sum += e;
            arr[c] = arr[u] + e;
            depth[c] = depth[u] + 1;
            r.levels = std::max(r.levels, depth[c]);
        }
        int k = (int)kids[u].size();
        if (a.kind == SRC_NODE) {
            if (k > P.srcF || sum > P.srcL) { r.why = "SRC limit violated"; return r; }
        } else if (a.kind == BUF_NODE) {
            if (k == 0) { r.why = "buffer without child"; return r; }
            int t = T.TT->pick(k, sum);
            if (t < 0) { r.why = "no buffer type fits"; return r; }
            r.cost += T.TT->cost(t);
            ++r.numBuf;
        }
    }
    r.tmax = LLONG_MIN;
    r.tmin = LLONG_MAX;
    for (int i = 1; i <= T.N; ++i) {
        r.tmax = std::max(r.tmax, arr[i]);
        r.tmin = std::min(r.tmin, arr[i]);
    }
    if (T.N == 0) r.tmax = r.tmin = 0;
    r.score = r.cost + P.wSkew * (r.tmax - r.tmin);
    r.legal = true;
    return r;
}

bool writeSolution(const Tree& T, const std::string& path, std::string& err) {
    std::ofstream out(path);
    if (!out) { err = "cannot open output file: " + path; return false; }

    std::vector<std::vector<int>> kids;
    std::vector<int> order = bfsOrder(T, kids);

    // 名稱：SRC、S#、B#（buffer 依 BFS 順序重新編號）
    size_t n = T.nd.size();
    std::vector<std::string> name(n);
    name[0] = "SRC";
    for (int i = 1; i <= T.N; ++i) name[i] = "S" + std::to_string(i);
    std::vector<int> bufs;
    for (int v : order)
        if (T.nd[v].kind == BUF_NODE) {
            bufs.push_back(v);
            name[v] = "B" + std::to_string(bufs.size());
        }

    out << ".buffer " << bufs.size() << "\n";
    for (int b : bufs) {
        long long sum = 0;
        for (int c : kids[b]) sum += T.dist(b, c);
        int t = T.TT->pick((int)kids[b].size(), sum);
        out << name[b] << " " << (t >= 0 ? T.TT->name(t) : std::string("?")) << " "
            << T.nd[b].x << " " << T.nd[b].y << "\n";
    }
    out << ".e\n\n";

    // 層級：第 k 行列出所有深度 k-1 的 parent 與它們的 children
    std::vector<int> depth(n, 0);
    int maxDepth = 0;
    for (int u : order)
        for (int c : kids[u]) {
            depth[c] = depth[u] + 1;
            maxDepth = std::max(maxDepth, depth[c]);
        }
    std::vector<std::string> lines(maxDepth + 1);
    for (int u : order) {
        if (kids[u].empty()) continue;
        std::string& L = lines[depth[u] + 1];
        L += " " + name[u] + ":{";
        for (size_t i = 0; i < kids[u].size(); ++i) L += (i ? " " : "") + name[kids[u][i]];
        L += "}";
    }
    out << ".level " << maxDepth << "\n";
    for (int k = 1; k <= maxDepth; ++k) out << k << lines[k] << "\n";
    out << ".e\n";
    return (bool)out;
}
