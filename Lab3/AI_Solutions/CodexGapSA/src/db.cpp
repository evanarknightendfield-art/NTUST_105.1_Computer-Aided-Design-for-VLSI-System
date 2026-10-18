#include "db.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace fs = std::filesystem;
namespace {
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::vector<std::string> tokens(std::string s) {
    s = s.substr(0, s.find('#'));
    std::string spaced;
    for (char c : s) spaced += c == ':' ? " : " : std::string(1,c);
    std::istringstream in(spaced);
    std::vector<std::string> result;
    while (in >> s) result.push_back(s);
    return result;
}
template<class F> void lines(const fs::path& path, F f) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    std::string line;
    while (std::getline(in,line)) {
        auto t = tokens(line);
        if (!t.empty() && lower(t[0]) != "ucla") f(t);
    }
}
double number(const std::string& s) {
    size_t used;
    double n = std::stod(s, &used);
    if (used != s.size() || !std::isfinite(n)) throw std::runtime_error("invalid number: " + s);
    return n;
}
int integer(const std::string& s) {
    double n = number(s);
    if (n < 0 || n > 100000000 || n != std::floor(n)) throw std::runtime_error("invalid count: " + s);
    return int(n);
}
}

void DB::read(fs::path input) {
    if (fs::is_directory(input)) input /= input.filename().string() + ".aux";
    else if (input.extension() != ".aux") input += ".aux";
    lines(input, [&](const auto& t) {
        for (const auto& name : t) {
            auto ext = lower(fs::path(name).extension().string());
            if (ext==".nodes" || ext==".nets" || ext==".pl" || ext==".scl" || ext==".wts")
                files[ext] = input.parent_path() / name;
        }
    });
    for (const auto* ext : {".nodes", ".nets", ".pl", ".scl"})
        if (!files.count(ext)) throw std::runtime_error(std::string("aux missing ") + ext);
    std::unordered_map<std::string,int> ids;
    int expectedNodes = -1, expectedNets = -1, expectedPins = -1, totalPins = 0;
    lines(files.at(".nodes"), [&](const auto& t) {
        if (lower(t[0])=="numnodes") { expectedNodes=integer(t.at(2)); return; }
        if (lower(t[0])=="numterminals") return;
        if (ids.count(t[0])) throw std::runtime_error("duplicate cell " + t[0]);
        Cell c;
        c.name=t[0]; c.w=number(t.at(1)); c.h=number(t.at(2));
        c.fixed=t.size()>3 && lower(t[3]).find("terminal")==0;
        if (c.w<0 || c.h<0 || (!c.fixed && (c.w==0 || c.h==0))) throw std::runtime_error("invalid cell size");
        ids[c.name]=int(cells.size()); cells.push_back(c);
    });
    if (expectedNodes != int(cells.size())) throw std::runtime_error("NumNodes mismatch");
    std::vector<bool> seen(cells.size());
    lines(files.at(".pl"), [&](const auto& t) {
        auto it=ids.find(t[0]);
        if (it==ids.end() || seen[it->second]) throw std::runtime_error("unknown/duplicate placement " + t[0]);
        int v=it->second; seen[v]=true;
        auto& c=cells[v]; c.gx=number(t.at(1)); c.gy=number(t.at(2));
        for (size_t k=3;k<t.size();++k) {
            if (t[k]==":") continue;
            if (t[k][0]=='/') {
                if (!c.suffix.empty()) c.suffix+=' ';
                c.suffix+=t[k];
                if (lower(t[k]).find("/fixed")==0) c.fixed=true;
            } else c.orient=t[k];
        }
    });
    if (std::find(seen.begin(),seen.end(),false)!=seen.end()) throw std::runtime_error("missing cell in input .pl");
    for (int v=0;v<int(cells.size());++v) if (!cells[v].fixed) movable.push_back(v);
    int remaining=0;
    auto checkDegree=[&]() {
        if(remaining) std::cerr<<"warning: NetDegree differs from actual pins in net "<<nets.size()-1
            <<"; using listed pins (course toy contains this inconsistency)\n";
    };
    lines(files.at(".nets"), [&](const auto& t) {
        auto key=lower(t[0]);
        if (key=="numnets") { expectedNets=integer(t.at(2)); return; }
        if (key=="numpins") { expectedPins=integer(t.at(2)); return; }
        if (key=="netdegree") {
            checkDegree();
            remaining=integer(t.at(2)); nets.emplace_back();
        } else {
            if (nets.empty() || !ids.count(t[0])) throw std::runtime_error("invalid net pin " + t[0]);
            nets.back().push_back(ids.at(t[0])); --remaining; ++totalPins;
        }
    });
    checkDegree();
    if (expectedNets!=int(nets.size()) || expectedPins!=totalPins) throw std::runtime_error("net/pin count mismatch");
    incident.resize(cells.size());
    for (int n=0;n<int(nets.size());++n) {
        auto& net=nets[n]; std::sort(net.begin(),net.end());
        net.erase(std::unique(net.begin(),net.end()),net.end());
        for (int v:net) incident[v].push_back(n);
    }
    // Collect whole rows first: fields may appear in any order within CoreRow.
    double y=0,h=0,step=0,sw=1;
    bool inRow=false;
    int rowCount=0, expectedRows=-1;
    std::vector<std::pair<double,int>> subs;
    std::vector<Segment> raw;
    lines(files.at(".scl"), [&](const auto& t) {
        auto k=lower(t[0]);
        if (k=="numrows") expectedRows=integer(t.at(2));
        else if (k=="corerow") {
            if (inRow || lower(t.at(1))!="horizontal") throw std::runtime_error("invalid/unsupported CoreRow");
            inRow=true; y=h=step=0; sw=1; subs.clear();
        } else if (k=="coordinate") y=number(t.at(2));
        else if (k=="height") h=number(t.at(2));
        else if (k=="sitespacing") step=number(t.at(2));
        else if (k=="sitewidth") sw=number(t.at(2));
        else if (k=="subroworigin") {
            int count=-1;
            for (size_t j=3;j+2<t.size();++j) if (lower(t[j])=="numsites") count=integer(t[j+2]);
            if (count<=0) throw std::runtime_error("invalid subrow size");
            subs.emplace_back(number(t.at(2)),count);
        } else if (k=="end") {
            if (!inRow || h<=0 || sw<=0 || subs.empty()) throw std::runtime_error("invalid row");
            if (step==0) step=sw;
            if (step<=0) throw std::runtime_error("invalid site spacing");
            for (auto [x,n]:subs) {
                // Conservative whole-site envelope, also valid when sitewidth != spacing.
                int cap=int(std::floor(((n-1)*step+sw)/step+1e-9));
                if (cap>0) raw.push_back({x,y,h,step,cap});
            }
            inRow=false; ++rowCount;
        }
    });
    if (inRow || rowCount!=expectedRows || raw.empty()) throw std::runtime_error("invalid row count");
    // Remove every site envelope intersecting a fixed object, including fractional macros.
    for (const auto& s:raw) {
        std::vector<std::pair<int,int>> blocked;
        for (const auto& c:cells) if (c.fixed && c.w>0 && c.h>0 && c.gy<s.y+s.h-1e-8 && s.y<c.gy+c.h-1e-8) {
            int lo=std::max(0,int(std::floor((c.gx-s.x)/s.step+1e-9)));
            int hi=std::min(s.capacity,int(std::ceil((c.gx+c.w-s.x)/s.step-1e-9)));
            if (lo<hi) blocked.emplace_back(lo,hi);
        }
        std::sort(blocked.begin(),blocked.end());
        int edge=0;
        auto add=[&](int lo,int hi) { if(lo<hi) segments.push_back({s.x+lo*s.step,s.y,s.h,s.step,hi-lo}); };
        for (auto [lo,hi]:blocked) { add(edge,lo); edge=std::max(edge,hi); }
        add(edge,s.capacity);
    }
    std::sort(segments.begin(),segments.end(),[](const auto& a,const auto& b){ return a.y!=b.y ? a.y<b.y : a.x<b.x; });
    if (segments.empty() && !movable.empty()) throw std::runtime_error("no usable row space");
    // This placer supports the single-height, non-overlapping rows in the assignment.
    for (size_t i=0;i<raw.size();++i) for (size_t j=0;j<i;++j) {
        const auto& a=raw[i]; const auto& b=raw[j];
        if (a.x<b.x+b.capacity*b.step-1e-8 && b.x<a.x+a.capacity*a.step-1e-8 &&
            a.y<b.y+b.h-1e-8 && b.y<a.y+a.h-1e-8) throw std::runtime_error("overlapping input subrows");
    }
}

double DB::netHPWL(int n,const std::vector<Slot>& p) const {
    if (nets[n].size()<2) return 0;
    double lx=std::numeric_limits<double>::infinity(),ly=lx,hx=-lx,hy=-lx;
    for (int v:nets[n]) {
        double cx=x(v,p[v])+cells[v].w/2, cy=y(v,p[v])+cells[v].h/2;
        lx=std::min(lx,cx); hx=std::max(hx,cx); ly=std::min(ly,cy); hy=std::max(hy,cy);
    }
    return hx-lx+hy-ly;
}
double DB::hpwl(const std::vector<Slot>& p) const {
    double sum=0; for(int n=0;n<int(nets.size());++n) sum+=netHPWL(n,p); return sum;
}
void DB::validate(const std::vector<Slot>& p) const {
    std::vector<std::vector<std::pair<int,int>>> occ(segments.size());
    for(int v=0;v<int(cells.size());++v) {
        Slot q=p.at(v);
        if(cells[v].fixed) { if(q.seg!=-1) throw std::runtime_error("fixed cell moved"); continue; }
        if(q.seg<0 || q.seg>=int(segments.size())) throw std::runtime_error("unplaced cell " + cells[v].name);
        const auto& s=segments[q.seg];
        if(q.site<0 || q.site+width(v,q.seg)>s.capacity || std::abs(s.h-cells[v].h)>1e-6)
            throw std::runtime_error("cell outside row " + cells[v].name);
        occ[q.seg].emplace_back(q.site,q.site+width(v,q.seg));
    }
    for(auto& intervals:occ) {
        std::sort(intervals.begin(),intervals.end()); int end=0;
        for(auto [lo,hi]:intervals) { if(lo<end) throw std::runtime_error("overlap"); end=hi; }
    }
}
void DB::write(const std::string& prefix,const std::vector<Slot>& legal,const std::vector<Slot>& final) const {
    fs::path path(prefix);
    if(!path.parent_path().empty()) fs::create_directories(path.parent_path());
    // Reject accidental overwriting of the benchmark (including symlink aliases).
    std::vector<fs::path> outputs;
    for(const auto* ext:{".aux",".nodes",".nets",".wts",".scl",".pl","_legal.pl"}) outputs.emplace_back(prefix+ext);
    for(const auto& out:outputs) for(const auto& entry:files)
        if(fs::weakly_canonical(out)==fs::weakly_canonical(entry.second)) throw std::runtime_error("output aliases an input file");
    auto save=[&](const std::string& name,const std::vector<Slot>& p) {
        std::ofstream f(name); f << std::setprecision(15) << "UCLA pl 1.0\n\n";
        for(int v=0;v<int(cells.size());++v) {
            const auto& c=cells[v];
            f<<c.name<<'\t'<<x(v,p[v])<<'\t'<<y(v,p[v])<<" : "<<c.orient;
            if(!c.suffix.empty()) f<<' '<<c.suffix;
            f<<'\n';
        }
        f.close(); if(!f) throw std::runtime_error("cannot write " + name);
    };
    save(prefix+"_legal.pl",legal); save(prefix+".pl",final);
    for(const auto* ext:{".nodes",".nets",".scl",".wts"}) {
        if(files.count(ext)) fs::copy_file(files.at(ext),prefix+ext,fs::copy_options::overwrite_existing);
        else { std::ofstream f(prefix+ext); f<<"UCLA wts 1.0\n"; if(!f) throw std::runtime_error("cannot write weights"); }
    }
    std::ofstream aux(prefix+".aux"); aux<<"RowBasedPlacement :";
    for(const auto* ext:{".nodes",".nets",".wts",".pl",".scl"}) aux<<' '<<path.filename().string()<<ext;
    aux<<'\n'; aux.close(); if(!aux) throw std::runtime_error("cannot write aux");
}
