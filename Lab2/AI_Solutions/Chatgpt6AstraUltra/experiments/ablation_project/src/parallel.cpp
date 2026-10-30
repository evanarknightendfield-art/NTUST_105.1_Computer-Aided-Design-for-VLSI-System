// parallel.cpp — 平行分工的切割與合併
#include "parallel.h"

#include <algorithm>

bool makeCut(const Tree& T, int axis, double maxFrac, Cut& cut) {
    size_t n = T.nd.size();
    cut.region.assign(n, -1);
    cut.isRoot.assign(n, 0);
    cut.roots[0].clear();
    cut.roots[1].clear();
    const long long limit = std::max<long long>(1, (long long)(T.N * maxFrac));

    // 展開 sink 最多的節點，直到每棵子樹都夠小
    std::vector<int> frontier{0};
    while (true) {
        int pick = -1;
        for (size_t i = 0; i < frontier.size(); ++i) {
            const Node& a = T.nd[frontier[i]];
            if (a.kind == SINK_NODE || a.ch.empty()) continue;
            if (pick < 0 || a.cnt > T.nd[frontier[pick]].cnt) pick = (int)i;
        }
        if (pick < 0 || (frontier[pick] != 0 && T.nd[frontier[pick]].cnt <= limit)) break;
        int v = frontier[pick];
        frontier.erase(frontier.begin() + pick);
        frontier.insert(frontier.end(), T.nd[v].ch.begin(), T.nd[v].ch.end());
    }
    if (frontier.size() < 2) return false;

    // 沿 x 或 y 排序，按 sink 數對半分組
    std::sort(frontier.begin(), frontier.end(), [&](int a, int b) {
        long long ka = axis ? T.nd[a].y : T.nd[a].x, kb = axis ? T.nd[b].y : T.nd[b].x;
        return ka != kb ? ka < kb : a < b;
    });
    long long total = 0, acc = 0;
    for (int r : frontier) total += T.nd[r].cnt;
    for (int r : frontier) {
        int g = (acc * 2 < total) ? 0 : 1;
        acc += T.nd[r].cnt;
        cut.roots[g].push_back(r);
    }
    if (cut.roots[0].empty() || cut.roots[1].empty()) return false;

    for (int g = 0; g < 2; ++g) {
        for (int r : cut.roots[g]) {
            cut.isRoot[r] = 1;
            std::vector<int> st{r};
            while (!st.empty()) {
                int u = st.back();
                st.pop_back();
                cut.region[u] = (signed char)g;
                for (int c : T.nd[u].ch) st.push_back(c);
            }
        }
    }
    return true;
}

bool mergeCut(const Tree& A, const Tree& B, const Cut& cut, Tree& M) {
    M = A;
    // 1. 先把「所有」第 1 組子樹原本的內容拿掉（區域根本身保留）。
    //    必須全部拿完再接：執行緒 1 可能把節點從一棵第 1 組子樹搬到另一棵，
    //    若一棵一棵處理，後面的移除會把前面剛接好的節點又拔掉。
    std::vector<int> desc, st;
    for (int r : cut.roots[1]) {
        st.assign(M.nd[r].ch.begin(), M.nd[r].ch.end());
        while (!st.empty()) {
            int u = st.back();
            st.pop_back();
            desc.push_back(u);
            for (int c : M.nd[u].ch) st.push_back(c);
        }
    }
    for (int u : desc) M.unlink(u);
    for (int u : desc)
        if (M.nd[u].kind == BUF_NODE) M.removeBuffer(u);

    // 2. 把 B 的版本接上來（sink 編號不變，buffer 重新編號）
    for (int r : cut.roots[1]) {
        std::vector<std::pair<int, int>> q{{r, r}};   // (B 裡的節點, M 裡對應的節點)
        for (size_t i = 0; i < q.size(); ++i) {
            int ub = q[i].first, um = q[i].second;
            for (int c : B.nd[ub].ch) {
                if (B.nd[c].kind == SINK_NODE) {
                    M.link(c, um);
                } else {
                    if (!M.usable(B.nd[c].x, B.nd[c].y)) return false;   // 兩邊放到同一個座標
                    int id = M.addBuffer(B.nd[c].x, B.nd[c].y);
                    M.link(id, um);
                    q.push_back({c, id});
                }
            }
        }
    }
    M.recomputeAll();
    return true;
}
