// placedb.cpp — Bookshelf reader/writer, row segmentation and metrics.
#include "placedb.hpp"

#include <chrono>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Splits a line into tokens; ':' is always its own token so "a:b" and "a : b" read the same.
std::vector<std::string> tokens(std::string line) {
    size_t hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);
    std::string spaced;
    for (char c : line) {
        if (c == ':') spaced += " : ";
        else spaced += c;
    }
    std::istringstream in(spaced);
    std::vector<std::string> t;
    std::string s;
    while (in >> s) t.push_back(s);
    return t;
}

bool isHeader(const std::vector<std::string>& t) {
    return t.empty() || t[0] == "UCLA";
}

std::string dirOf(const std::string& path) {
    size_t p = path.find_last_of('/');
    return p == std::string::npos ? "" : path.substr(0, p + 1);
}

std::string baseOf(const std::string& path) {
    size_t p = path.find_last_of('/');
    return p == std::string::npos ? path : path.substr(p + 1);
}

bool fileExists(const std::string& path) {
    std::ifstream f(path);
    return (bool)f;
}

bool copyFile(const std::string& from, const std::string& to) {
    if (from == to) return true;
    std::ifstream in(from, std::ios::binary);
    std::ofstream out(to, std::ios::binary);
    if (!in || !out) return false;
    out << in.rdbuf();
    return (bool)out;
}

}  // namespace

std::string fmtNum(double v) {
    if (std::fabs(v - std::round(v)) < 1e-6) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%lld", (long long)std::llround(v));
        return buf;
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

int PlaceDB::nearestRow(double y) const {
    // rows are sorted by y; pick the row whose bottom is closest to y
    int lo = 0, hi = (int)rows.size() - 1;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (rows[mid].y < y) lo = mid + 1;
        else hi = mid;
    }
    if (lo > 0 && std::fabs(rows[lo - 1].y - y) <= std::fabs(rows[lo].y - y)) --lo;
    return lo;
}

bool PlaceDB::read(const std::string& inputPrefix, std::string& err) {
    // Accept "toy", "toy.aux", or a directory "bench/toy" containing toy.aux.
    auxPath = inputPrefix;
    if (auxPath.size() < 4 || auxPath.substr(auxPath.size() - 4) != ".aux") {
        if (fileExists(auxPath + ".aux")) {
            auxPath += ".aux";
        } else {
            std::string d = auxPath;
            while (!d.empty() && d.back() == '/') d.pop_back();
            auxPath = d + "/" + baseOf(d) + ".aux";
        }
    }
    std::ifstream aux(auxPath);
    if (!aux) { err = "cannot open " + inputPrefix + ".aux"; return false; }
    std::string dir = dirOf(auxPath), line;
    while (std::getline(aux, line)) {
        for (const std::string& t : tokens(line)) {
            std::string l = lower(t);
            auto ends = [&](const char* ext) {
                std::string e = ext;
                return l.size() > e.size() && l.substr(l.size() - e.size()) == e;
            };
            if (ends(".nodes")) nodesFile = dir + t;
            else if (ends(".nets")) netsFile = dir + t;
            else if (ends(".wts")) wtsFile = dir + t;
            else if (ends(".pl")) plFile = dir + t;
            else if (ends(".scl")) sclFile = dir + t;
        }
    }
    if (nodesFile.empty() || netsFile.empty() || plFile.empty() || sclFile.empty()) {
        err = "aux file must list .nodes, .nets, .pl and .scl";
        return false;
    }

    // ---- .nodes
    std::unordered_map<std::string, int> id;
    {
        std::ifstream in(nodesFile);
        if (!in) { err = "cannot open " + nodesFile; return false; }
        while (std::getline(in, line)) {
            auto t = tokens(line);
            if (isHeader(t) || lower(t[0]).rfind("num", 0) == 0) continue;
            if (t.size() < 3) continue;
            Node n;
            n.name = t[0];
            n.w = std::stod(t[1]);
            n.h = std::stod(t[2]);
            n.fixed = t.size() > 3 && lower(t[3]).rfind("terminal", 0) == 0;
            id[n.name] = (int)nodes.size();
            nodes.push_back(n);
        }
    }

    // ---- .pl
    {
        std::ifstream in(plFile);
        if (!in) { err = "cannot open " + plFile; return false; }
        while (std::getline(in, line)) {
            auto t = tokens(line);
            if (isHeader(t) || t.size() < 3) continue;
            auto it = id.find(t[0]);
            if (it == id.end()) continue;
            Node& n = nodes[it->second];
            n.gx = std::stod(t[1]);
            n.gy = std::stod(t[2]);
            for (size_t k = 3; k < t.size(); ++k) {
                if (t[k] == ":") continue;
                if (t[k][0] == '/') {
                    n.plSuffix = t[k];
                    if (lower(t[k]).rfind("/fixed", 0) == 0) n.fixed = true;
                } else {
                    n.orient = t[k];
                }
            }
        }
    }

    // ---- .nets (only connectivity is needed: pins are assumed at cell centers)
    {
        std::ifstream in(netsFile);
        if (!in) { err = "cannot open " + netsFile; return false; }
        nodeNets.assign(nodes.size(), {});
        int remaining = 0;
        std::vector<int> cur;
        auto flush = [&]() {
            std::sort(cur.begin(), cur.end());
            cur.erase(std::unique(cur.begin(), cur.end()), cur.end());
            int nid = (int)nets.size();
            for (int v : cur) nodeNets[v].push_back(nid);
            nets.push_back(cur);
            cur.clear();
        };
        while (std::getline(in, line)) {
            auto t = tokens(line);
            if (isHeader(t)) continue;
            if (lower(t[0]) == "netdegree") {
                if (remaining > 0 || !cur.empty()) flush();   // tolerate short nets
                remaining = t.size() >= 3 ? std::stoi(t[2]) : 0;
                if (remaining == 0) flush();
                continue;
            }
            if (lower(t[0]).rfind("num", 0) == 0) continue;
            if (remaining <= 0) continue;
            auto it = id.find(t[0]);
            if (it == id.end()) { err = "unknown node in nets: " + t[0]; return false; }
            cur.push_back(it->second);
            if (--remaining == 0) flush();
        }
        if (!cur.empty()) flush();
    }

    // ---- .scl
    struct SubRow { double y, h, x0, sp; int n; };
    std::vector<SubRow> subrows;
    {
        std::ifstream in(sclFile);
        if (!in) { err = "cannot open " + sclFile; return false; }
        double y = 0, h = 0, sw = 1, sp = 0;
        while (std::getline(in, line)) {
            auto t = tokens(line);
            if (isHeader(t)) continue;
            std::string k = lower(t[0]);
            auto val = [&](size_t i) { return i < t.size() ? std::stod(t[i]) : 0.0; };
            if (k == "corerow") { y = h = 0; sw = 1; sp = 0; }
            else if (k == "coordinate") y = val(2);
            else if (k == "height") h = val(2);
            else if (k == "sitewidth") sw = val(2);
            else if (k == "sitespacing") sp = val(2);
            else if (k == "subroworigin") {
                SubRow r{y, h, val(2), sp > 0 ? sp : sw, 0};
                for (size_t i = 3; i < t.size(); ++i)
                    if (lower(t[i]) == "numsites") r.n = (int)val(i + 2);
                subrows.push_back(r);
            }
        }
    }
    if (subrows.empty()) { err = "no rows in " + sclFile; return false; }
    std::sort(subrows.begin(), subrows.end(),
              [](const SubRow& a, const SubRow& b) { return a.y != b.y ? a.y < b.y : a.x0 < b.x0; });
    siteSpacing = subrows[0].sp;

    // Fixed objects with area are obstacles: cut them out of the subrows.
    std::vector<int> obstacles;
    for (int v = 0; v < (int)nodes.size(); ++v)
        if (nodes[v].fixed && nodes[v].w > 0 && nodes[v].h > 0) obstacles.push_back(v);

    for (const SubRow& r : subrows) {
        if (rows.empty() || std::fabs(rows.back().y - r.y) > 1e-9) rows.push_back(Row{r.y, r.h, {}});
        std::vector<char> blocked(r.n, 0);
        for (int v : obstacles) {
            const Node& o = nodes[v];
            if (!(o.gy < r.y + r.h - 1e-9 && r.y < o.gy + o.h - 1e-9)) continue;
            long lo = (long)std::floor((o.gx - r.x0) / r.sp + 1e-9);
            long hi = (long)std::ceil((o.gx + o.w - r.x0) / r.sp - 1e-9) - 1;
            for (long k = std::max(0L, lo); k <= std::min((long)r.n - 1, hi); ++k) blocked[k] = 1;
        }
        for (int k = 0; k < r.n;) {
            if (blocked[k]) { ++k; continue; }
            int e = k;
            while (e < r.n && !blocked[e]) ++e;
            Segment s;
            s.row = (int)rows.size() - 1;
            s.y = r.y;
            s.height = r.h;
            s.x0 = r.x0 + k * r.sp;
            s.sp = r.sp;
            s.numSites = e - k;
            rows.back().segs.push_back((int)segs.size());
            segs.push_back(s);
            k = e;
        }
    }

    for (int v = 0; v < (int)nodes.size(); ++v)
        if (!nodes[v].fixed) movable.push_back(v);

    if (segs.empty() && !movable.empty()) {err = "no free sites";return false;}
    uniformSites = false;
    bool same = true;
    for (const Segment& s : segs) same = same && s.sp == segs[0].sp;
    if (same && !segs.empty()) {
        wSites.resize(nodes.size());
        for (int v = 0; v < (int)nodes.size(); ++v) wSites[v] = sitesOf(v, segs[0]);
        uniformSites = true;
    }
    return true;
}

double PlaceDB::utilization() const {
    double cells = 0, sites = 0;
    for (int v : movable) cells += nodes[v].w;
    for (const Segment& s : segs) sites += s.numSites * s.sp;
    return sites > 0 ? cells / sites : 1;
}

double displacementOf(const PlaceDB& db, const Placement& p, int v) {
    return std::fabs(p.x(db, v) - db.nodes[v].gx) + std::fabs(p.y(db, v) - db.nodes[v].gy);
}

DispStats displacement(const PlaceDB& db, const Placement& p) {
    DispStats d;
    for (int v : db.movable) {
        double dv = displacementOf(db, p, v);
        d.total += dv;
        if (dv > d.max) { d.max = dv; d.argmax = v; }
    }
    return d;
}

double totalHPWL(const PlaceDB& db, const Placement& p) {
    double sum = 0;
    for (const auto& net : db.nets) {
        if (net.size() < 2) continue;
        double xl = 1e300, xh = -1e300, yl = 1e300, yh = -1e300;
        for (int v : net) {
            double cx = p.x(db, v) + db.nodes[v].w / 2, cy = p.y(db, v) + db.nodes[v].h / 2;
            xl = std::min(xl, cx); xh = std::max(xh, cx);
            yl = std::min(yl, cy); yh = std::max(yh, cy);
        }
        sum += (xh - xl) + (yh - yl);
    }
    return sum;
}

std::string checkLegal(const PlaceDB& db, const Placement& p) {
    std::vector<std::vector<int>> inSeg(db.segs.size());
    for (int v : db.movable) {
        int s = p.seg[v];
        if (s < 0 || s >= (int)db.segs.size()) return db.nodes[v].name + " is not on a row";
        const Segment& sg = db.segs[s];
        if (std::fabs(db.nodes[v].h - sg.height) > 1e-6) return db.nodes[v].name + " height differs from row";
        if (p.site[v] < 0 || p.site[v] + db.sitesOf(v, sg) > sg.numSites)
            return db.nodes[v].name + " is outside its subrow";
        inSeg[s].push_back(v);
    }
    for (size_t s = 0; s < inSeg.size(); ++s) {
        auto& c = inSeg[s];
        std::sort(c.begin(), c.end(), [&](int a, int b) { return p.site[a] < p.site[b]; });
        for (size_t i = 1; i < c.size(); ++i)
            if (p.site[c[i - 1]] + db.sitesOf(c[i - 1], db.segs[s]) > p.site[c[i]])
                return db.nodes[c[i - 1]].name + " overlaps " + db.nodes[c[i]].name;
    }
    return "";
}

bool writePl(const PlaceDB& db, const Placement& p, const std::string& path) {
    std::ofstream out(path);
    if (!out) return false;
    out << "UCLA pl 1.0\n\n";
    for (int v = 0; v < (int)db.nodes.size(); ++v) {
        const Node& n = db.nodes[v];
        out << n.name << '\t' << fmtNum(p.x(db, v)) << '\t' << fmtNum(p.y(db, v)) << "\t: " << n.orient;
        if (!n.plSuffix.empty()) out << ' ' << n.plSuffix;
        out << '\n';
    }
    return (bool)out;
}

bool writeOutput(const PlaceDB& db, const Placement& legal, const Placement& finalP,
                 const std::string& outPrefix, std::string& err) {
    std::string base = baseOf(outPrefix);
    std::error_code ec;
    if (!dirOf(outPrefix).empty()) std::filesystem::create_directories(dirOf(outPrefix), ec);
    std::ofstream aux(outPrefix + ".aux");
    if (!aux) { err = "cannot write " + outPrefix + ".aux"; return false; }
    aux << "RowBasedPlacement : " << base << ".nodes " << base << ".nets ";
    if (!db.wtsFile.empty()) aux << base << ".wts ";
    aux << base << ".pl " << base << ".scl\n";
    aux.close();

    bool ok = copyFile(db.nodesFile, outPrefix + ".nodes") && copyFile(db.netsFile, outPrefix + ".nets") &&
              copyFile(db.sclFile, outPrefix + ".scl") &&
              (db.wtsFile.empty() || copyFile(db.wtsFile, outPrefix + ".wts"));
    if (!ok) { err = "cannot copy input files to " + outPrefix + ".*"; return false; }
    if (!writePl(db, legal, outPrefix + "_legal.pl") || !writePl(db, finalP, outPrefix + ".pl")) {
        err = "cannot write " + outPrefix + ".pl";
        return false;
    }
    return true;
}

namespace {
const auto kProgramStart = std::chrono::steady_clock::now();
}
double wallSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - kProgramStart).count();
}
