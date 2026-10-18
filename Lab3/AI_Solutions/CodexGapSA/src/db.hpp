#pragma once
#include <cmath>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct Cell {
    std::string name, orient = "N", suffix;
    double w = 0, h = 0, gx = 0, gy = 0;
    bool fixed = false;
};
struct Segment { double x, y, h, step; int capacity; };
struct Slot { int seg = -1, site = 0; };
struct DB {
    std::vector<Cell> cells;
    std::vector<int> movable;
    std::vector<std::vector<int>> nets, incident;
    std::vector<Segment> segments;
    std::map<std::string, std::filesystem::path> files;
    void read(std::filesystem::path input);
    int width(int v, int s) const {
        return int(std::ceil(cells[v].w / segments[s].step - 1e-9));
    }
    double x(int v, Slot p) const { return p.seg < 0 ? cells[v].gx : segments[p.seg].x + p.site * segments[p.seg].step; }
    double y(int v, Slot p) const { return p.seg < 0 ? cells[v].gy : segments[p.seg].y; }
    double displacement(int v, Slot p) const { return std::abs(x(v,p)-cells[v].gx) + std::abs(y(v,p)-cells[v].gy); }
    double netHPWL(int n, const std::vector<Slot>& p) const;
    double hpwl(const std::vector<Slot>& p) const;
    void validate(const std::vector<Slot>& p) const;
    void write(const std::string& prefix, const std::vector<Slot>& legal, const std::vector<Slot>& final) const;
};
