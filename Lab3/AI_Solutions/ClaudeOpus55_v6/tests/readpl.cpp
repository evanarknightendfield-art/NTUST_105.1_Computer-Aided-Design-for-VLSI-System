#include "placedb.hpp"
#include <map>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdio>
bool readPlX(const PlaceDB& db, const std::string& path, Placement& p) {
    std::map<std::string, int> id;
    for (size_t i = 0; i < db.nodes.size(); ++i) id[db.nodes[i].name] = int(i);
    p.seg.assign(db.nodes.size(), -1);
    p.site.assign(db.nodes.size(), 0);
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string n;
        double x, y;
        if (!(ss >> n >> x >> y)) continue;
        auto it = id.find(n);
        if (it == id.end()) continue;
        int v = it->second;
        if (db.nodes[v].fixed) continue;
        bool ok = false;
        for (size_t s = 0; s < db.segs.size() && !ok; ++s) {
            const auto& g = db.segs[s];
            if (std::abs(g.y - y) > 1e-6) continue;
            double k = (x - g.x0) / g.sp;
            int ki = (int)std::lround(k);
            if (std::abs(k - ki) > 1e-6 || ki < 0 || ki + db.sitesOf(v, g) > g.numSites) continue;
            p.seg[v] = int(s);
            p.site[v] = ki;
            ok = true;
        }
        if (!ok) { std::fprintf(stderr, "cannot map %s\n", n.c_str()); return false; }
    }
    for (int v : db.movable) if (p.seg[v] < 0) { std::fprintf(stderr, "%s not in %s\n", db.nodes[v].name.c_str(), path.c_str()); return false; }
    return true;
}
