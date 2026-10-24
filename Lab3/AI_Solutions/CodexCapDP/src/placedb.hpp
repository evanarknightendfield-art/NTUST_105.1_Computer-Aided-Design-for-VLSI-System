// placedb.hpp — circuit data, Bookshelf I/O, rows/segments, placement and metrics.
#pragma once
#include <cmath>
#include <string>
#include <vector>

struct Node {
    std::string name;
    double w = 0, h = 0;
    bool fixed = false;        // "terminal" in .nodes, or /FIXED in .pl
    double gx = 0, gy = 0;     // input (global placement) bottom-left corner
    std::string orient = "N";
    std::string plSuffix;      // e.g. "/FIXED", written back unchanged
};

// One run of contiguous free sites in a row. Fixed objects that overlap a row
// split its subrow into several segments. Site k of the segment starts at
// x = x0 + k * sp.
struct Segment {
    int row = 0;
    double y = 0, height = 0;
    double x0 = 0, sp = 1;
    int numSites = 0;
};

struct Row {
    double y = 0, height = 0;
    std::vector<int> segs;     // segment ids in this row, sorted by x
};

struct PlaceDB {
    std::vector<Node> nodes;
    std::vector<std::vector<int>> nets;       // node ids of each net (deduplicated)
    std::vector<std::vector<int>> nodeNets;   // net ids of each node
    std::vector<int> movable;                 // ids of movable nodes
    std::vector<Row> rows;                    // sorted by y
    std::vector<Segment> segs;
    double siteSpacing = 1;                   // of the first row (for messages)

    std::string auxPath, nodesFile, netsFile, wtsFile, plFile, sclFile;   // resolved input paths

    bool read(const std::string& inputPrefix, std::string& err);
    // Cell width in sites of segment s (cells are aligned to the site grid).
    int sitesOf(int node, const Segment& s) const {
        return uniformSites ? wSites[node] : (int)std::ceil(nodes[node].w / s.sp - 1e-9);
    }
    bool uniformSites = false;                // all segments share one site spacing
    std::vector<int> wSites;                  // cached widths in sites (when uniform)
    int nearestRow(double y) const;
    double utilization() const;               // movable cell width / free sites
};

// Positions of movable cells: segment + site index. Fixed nodes stay at (gx, gy).
struct Placement {
    std::vector<int> seg, site;   // indexed by node id; -1 for fixed nodes
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
// Independent legality check. Returns an empty string when legal.
std::string checkLegal(const PlaceDB& db, const Placement& p);

bool writePl(const PlaceDB& db, const Placement& p, const std::string& path);
// Copies .nodes/.nets/.wts/.scl and writes OUT.aux, OUT.pl, OUT_legal.pl.
bool writeOutput(const PlaceDB& db, const Placement& legal, const Placement& finalP,
                 const std::string& outPrefix, std::string& err);

std::string fmtNum(double v);   // shortest fixed-point form, no exponent

// v4: seconds since the program started; every stage checks it against its budget.
double wallSeconds();
