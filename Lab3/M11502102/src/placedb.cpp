// placedb.cpp — Bookshelf 讀寫、row 切 segment、評分
//
// 要補的地方：TODO(M1)、TODO(M2)。小工具都寫好了，直接用。
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
[[maybe_unused]] bool isHeader(const std::vector<std::string>& t) {
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

    // ---- TODO(M1-a) .nodes
    //   每一行：name  width  height  [terminal]
    //   跳過檔頭（isHeader）和 "NumNodes : ..."、"NumTerminals : ..."
    //   有第 4 個 token 而且是 terminal（或 terminal_NI）→ fixed
    //   記得 id[name] = 編號

    // ---- TODO(M1-b) .pl
    //   每一行：name  x  y  :  orient  [/FIXED]
    //   設定 gx、gy、orient；有 /FIXED 就存進 plSuffix，並設 fixed = true
    //   注意：ibm05 的 pad 沒有 /FIXED，只有 .nodes 的 terminal 標記

    // ---- TODO(M1-c) .nets（只需要「誰連到誰」，pin offset 不用）
    //   "NetDegree : k [name]" 之後接 k 行 pin，每行第一個 token 是 node 名字
    //   同一個 node 在一條 net 出現兩次 → 去掉重複
    //   填 nets 和 nodeNets（nodeNets 要先 assign(nodes.size(), {})）

    // ---- TODO(M1-d) .scl
    //   每個 "CoreRow Horizontal" 到 "End" 是一個 row：
    //     Coordinate（y）、Height、Sitewidth、Sitespacing、SubrowOrigin（x0）和 NumSites
    //   注意大小寫：ibm05 寫 "Numsites"，所以比對前先 lower()
    //   一個 row 可以有好幾個 SubrowOrigin（中間有洞）
    //   產生 rows（依 y 排序）和 segs：
    //     最簡單：一個 subrow = 一個 segment
    //     完整版：fixed node 如果蓋到 row，把它擋住的 sites 挖掉，subrow 會被切成幾段
    //     （課程給的測資沒有這種情況，但 TA 的隱藏測資可能有）

    (void)id;
    err = "TODO(M1): parse .nodes/.pl/.nets/.scl";
    return false;

    // 讀完之後（寫好了，把上面兩行刪掉就會跑到這裡）：
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
    // TODO(M2) |x - gx| + |y - gy|
    (void)db; (void)p; (void)v;
    return 0;
}

DispStats displacement(const PlaceDB& db, const Placement& p) {
    // TODO(M2) 所有 movable cell 的總和與最大值（argmax 記是哪個 cell，debug 用）
    (void)db; (void)p;
    return {};
}

double totalHPWL(const PlaceDB& db, const Placement& p) {
    // TODO(M2) 每條 net：所有 pin 的「cell 中心」(x + w/2, y + h/2) 的 bounding box 寬 + 高
    //          只有 1 個 pin 的 net 是 0；terminal 也算 pin
    (void)db; (void)p;
    return 0;
}

std::string checkLegal(const PlaceDB& db, const Placement& p) {
    // TODO(M2) 每個 movable cell：
    //   seg 合法、0 <= site、site + 寬度(site 數) <= numSites
    //   同一個 segment 裡依 site 排序後，前一個的結尾 <= 下一個的開頭
    (void)db; (void)p;
    return "TODO(M2): checkLegal";
}

bool writePl(const PlaceDB& db, const Placement& p, const std::string& path) {
    // TODO(M2) 格式：
    //   UCLA pl 1.0
    //   （空行）
    //   name<TAB>x<TAB>y<TAB>: orient [plSuffix]
    //   每個 node 都要寫（包含 fixed）；數字用 fmtNum()
    (void)db; (void)p; (void)path;
    return false;
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
