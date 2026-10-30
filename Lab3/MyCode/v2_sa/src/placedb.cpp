// placedb.cpp — Bookshelf 讀寫、row 切 segment、評分
//
// M1：讀 .nodes / .pl / .nets / .scl，建 rows / segments（fixed 物件擋住的 sites 會挖掉）
// M2：位移、HPWL、合法性檢查、寫 .pl
#include "placedb.hpp"

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

// 一行切成 token：先刪掉 # 之後的註解；':' 一律當成獨立的 token，
// 所以 "a:b" 和 "a : b" 切出來一樣，都是 {"a", ":", "b"}。
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

// 空行或檔頭 "UCLA nodes 1.0" 之類的
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
    // rows 依 y 排序：二分搜尋找最接近的
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
    // ---- .aux（寫好了）：接受 "toy"、"toy.aux"、或資料夾 "bench/toy"
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

    std::unordered_map<std::string, int> id;   // 名字 -> node 編號

    // ---- M1-a .nodes：name  width  height  [terminal]
    {
        std::ifstream in(nodesFile);
        if (!in) { err = "cannot open " + nodesFile; return false; }
        while (std::getline(in, line)) {
            auto t = tokens(line);
            if (isHeader(t) || lower(t[0]).rfind("num", 0) == 0) continue;   // NumNodes / NumTerminals
            if (t.size() < 3) continue;
            Node n;
            n.name = t[0];
            n.w = std::stod(t[1]);
            n.h = std::stod(t[2]);
            n.fixed = t.size() > 3 && lower(t[3]).rfind("terminal", 0) == 0;   // terminal、terminal_NI
            id[n.name] = (int)nodes.size();
            nodes.push_back(n);
        }
    }

    // ---- M1-b .pl：name  x  y  :  orient  [/FIXED]
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

    // ---- M1-c .nets：NetDegree : k [name]，後面 k 行 pin，每行第一個 token 是 node 名字
    {
        std::ifstream in(netsFile);
        if (!in) { err = "cannot open " + netsFile; return false; }
        nodeNets.assign(nodes.size(), {});
        int remaining = 0;
        std::vector<int> cur;
        auto flush = [&]() {   // 一條 net 讀完：去掉重複的 node，存起來
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
                if (remaining > 0 || !cur.empty()) flush();   // 上一條 net 的 pin 比宣告的少，也收下
                remaining = t.size() >= 3 ? std::stoi(t[2]) : 0;
                if (remaining == 0) flush();
                continue;
            }
            if (lower(t[0]).rfind("num", 0) == 0) continue;   // NumNets / NumPins
            if (remaining <= 0) continue;
            auto it = id.find(t[0]);
            if (it == id.end()) { err = "unknown node in nets: " + t[0]; return false; }
            cur.push_back(it->second);
            if (--remaining == 0) flush();
        }
        if (!cur.empty()) flush();
    }

    // ---- M1-d .scl：CoreRow ... End；關鍵字一律轉小寫再比（ibm05 寫 Numsites）
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
            auto val = [&](size_t i) { return i < t.size() ? std::stod(t[i]) : 0.0; };   // t = {key, ":", value}
            if (k == "corerow") { y = h = 0; sw = 1; sp = 0; }
            else if (k == "coordinate") y = val(2);
            else if (k == "height") h = val(2);
            else if (k == "sitewidth") sw = val(2);
            else if (k == "sitespacing") sp = val(2);
            else if (k == "subroworigin") {   // SubrowOrigin : x0  NumSites : n（同一行）
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

    // fixed 物件（有面積的）蓋到 row 的話，把被擋住的 sites 挖掉，subrow 切成好幾個 segment
    std::vector<int> obstacles;
    for (int v = 0; v < (int)nodes.size(); ++v)
        if (nodes[v].fixed && nodes[v].w > 0 && nodes[v].h > 0) obstacles.push_back(v);

    for (const SubRow& r : subrows) {
        if (rows.empty() || std::fabs(rows.back().y - r.y) > 1e-9) rows.push_back(Row{r.y, r.h, {}});
        std::vector<char> blocked(r.n, 0);
        for (int v : obstacles) {
            const Node& o = nodes[v];
            if (!(o.gy < r.y + r.h - 1e-9 && r.y < o.gy + o.h - 1e-9)) continue;   // y 範圍沒重疊
            // site k 被擋住：x0 + k*sp < ox + ow 且 x0 + (k+1)*sp > ox
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

    // 讀完之後：movable 清單、site 寬度
    for (int v = 0; v < (int)nodes.size(); ++v)
        if (!nodes[v].fixed) movable.push_back(v);
    siteSpacing = segs.empty() ? 1 : segs[0].sp;
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
    // 每條 net：所有 pin（cell 中心）的 bounding box 寬 + 高；terminal 也算 pin
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
    // 每個 cell 在自己的 segment 裡；同一個 segment 依 site 排序後，前一個的結尾 <= 下一個的開頭
    std::vector<std::vector<int>> inSeg(db.segs.size());
    for (int v : db.movable) {
        int s = p.seg[v];
        if (s < 0 || s >= (int)db.segs.size()) return db.nodes[v].name + " is not on a row";
        const Segment& sg = db.segs[s];
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
    // UCLA pl 1.0、空行，之後每個 node（包含 fixed）一行：name<TAB>x<TAB>y<TAB>: orient [plSuffix]
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
    // 寫好了：複製其他檔案、寫 .aux，再呼叫你的 writePl
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
