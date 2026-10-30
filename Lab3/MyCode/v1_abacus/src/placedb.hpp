// placedb.hpp — 電路資料、Bookshelf 讀寫、rows / segments、placement 與評分
//
// 這個檔案已經寫好，不用改。先讀懂這幾個 struct，後面所有程式都靠它們。
#pragma once
#include <cmath>
#include <string>
#include <vector>

struct Node {
    std::string name;
    double w = 0, h = 0;
    bool fixed = false;        // .nodes 有 "terminal"，或 .pl 有 /FIXED
    double gx = 0, gy = 0;     // 輸入的 global placement（左下角）
    std::string orient = "N";  // 方向：原封不動寫回去
    std::string plSuffix;      // 例如 "/FIXED"：原封不動寫回去
};

// 一段「連續、沒有被擋住」的 sites。一個 subrow 如果被 fixed 物件擋住，
// 會被切成好幾個 segment。segment 的第 k 個 site 從 x = x0 + k * sp 開始。
struct Segment {
    int row = 0;
    double y = 0, height = 0;
    double x0 = 0, sp = 1;
    int numSites = 0;
};

struct Row {
    double y = 0, height = 0;
    std::vector<int> segs;     // 這個 row 的 segment 編號，依 x 排序
};

struct PlaceDB {
    std::vector<Node> nodes;
    std::vector<std::vector<int>> nets;       // 每條 net 連到的 node 編號（去掉重複）
    std::vector<std::vector<int>> nodeNets;   // 每個 node 在哪些 net 上
    std::vector<int> movable;                 // 可移動的 node 編號
    std::vector<Row> rows;                    // 依 y 排序
    std::vector<Segment> segs;
    double siteSpacing = 1;

    std::string auxPath, nodesFile, netsFile, wtsFile, plFile, sclFile;   // 輸入檔的實際路徑

    bool read(const std::string& inputPrefix, std::string& err);
    // cell 寬度換算成 site 數（cell 一定對齊 site）
    int sitesOf(int node, const Segment& s) const {
        return uniformSites ? wSites[node] : (int)std::ceil(nodes[node].w / s.sp - 1e-9);
    }
    bool uniformSites = false;                // 所有 segment 的 site spacing 都一樣
    std::vector<int> wSites;                  // 一樣的話，先把寬度（site 數）算好
    int nearestRow(double y) const;           // y 最接近的 row
};

// 可移動 cell 的位置用「在哪個 segment 的第幾個 site」表示，
// 這樣 x 一定對齊 site、y 一定在 row 上，不可能寫出不合法的座標。
// fixed node 的 seg = -1，位置就是 (gx, gy)。
struct Placement {
    std::vector<int> seg, site;   // 以 node 編號當 index
    double x(const PlaceDB& db, int v) const {
        if (seg[v] < 0) return db.nodes[v].gx;
        const Segment& s = db.segs[seg[v]];
        return s.x0 + site[v] * s.sp;
    }
    double y(const PlaceDB& db, int v) const {
        return seg[v] < 0 ? db.nodes[v].gy : db.segs[seg[v]].y;
    }
};

struct DispStats { double total = 0, max = 0; int argmax = -1; };

DispStats displacement(const PlaceDB& db, const Placement& p);
double displacementOf(const PlaceDB& db, const Placement& p, int v);
double totalHPWL(const PlaceDB& db, const Placement& p);
// 自己檢查合不合法；合法回傳空字串，不合法回傳原因
std::string checkLegal(const PlaceDB& db, const Placement& p);

bool writePl(const PlaceDB& db, const Placement& p, const std::string& path);
// 複製 .nodes/.nets/.wts/.scl，並寫出 OUT.aux、OUT.pl、OUT_legal.pl
bool writeOutput(const PlaceDB& db, const Placement& legal, const Placement& finalP,
                 const std::string& outPrefix, std::string& err);

std::string fmtNum(double v);   // 數字轉字串：整數就不印小數點、不會出現 1e+07
