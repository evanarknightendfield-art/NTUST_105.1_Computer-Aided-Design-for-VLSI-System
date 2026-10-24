#pragma once
// Measurement-only helpers. Production src/ is kept byte-identical to v5.
#include "capsearch.hpp"
#include <fstream>
#include <iomanip>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
namespace diag {
inline std::string prefix() { const char* s=std::getenv("CAP_DIAG_PREFIX"); return s?s:""; }
template<class T> void vec(std::ostream& f,const std::vector<T>& v) { size_t n=v.size(); f.write((char*)&n,sizeof(n)); f.write((char*)v.data(),n*sizeof(T)); }
template<class T> void vec(std::istream& f,std::vector<T>& v) { size_t n=0; f.read((char*)&n,sizeof(n)); v.resize(n); f.read((char*)v.data(),n*sizeof(T)); }
inline void saveMemo(const WindowMemo& m,const std::string& path) {
 std::ofstream f(path,std::ios::binary); vec(f,m.mod); for(auto& a:m.seen) for(auto& b:a) vec(f,b); vec(f,m.seg);vec(f,m.site);f.write((char*)&m.clock,sizeof(m.clock));
}
inline WindowMemo loadMemo(const std::string& path) {
 WindowMemo m;std::ifstream f(path,std::ios::binary);if(!f) throw std::runtime_error("missing memo "+path);
 vec(f,m.mod);for(auto& a:m.seen)for(auto& b:a)vec(f,b);vec(f,m.seg);vec(f,m.site);f.read((char*)&m.clock,sizeof(m.clock));return m;
}
inline void snapshot(const PlaceDB& db,const Placement& p,const WindowMemo& m,const char* name) {
 if(prefix().empty())return;writePl(db,p,prefix()+"_"+name+".pl");saveMemo(m,prefix()+"_"+name+".memo");
}
struct Attempt {int id=0,round=0,seed=0,how=0;std::string kind;double D=0,startMax=0,startTotal=0,rowSec=0,siteSec=0,dpSec=0,rawTotal=0,rawMax=0,postTotal=0,postMax=0,limit=0;bool ok=false,accept=false;};
inline std::vector<Attempt> attempts;
inline Attempt current;
using Memberships=std::vector<std::pair<std::string,std::vector<std::vector<int>>>>;
inline bool detail=false;
inline Memberships memberships;
inline void saveMembers(const Memberships& a,const std::string& path){std::ofstream f(path,std::ios::binary);size_t n=a.size();f.write((char*)&n,sizeof(n));for(auto& v:a){std::vector<char> name(v.first.begin(),v.first.end());vec(f,name);size_t m=v.second.size();f.write((char*)&m,sizeof(m));for(auto& l:v.second)vec(f,l);}}
inline void loadMembers(const std::string& path){memberships.clear();std::ifstream f(path,std::ios::binary);if(!f)return;size_t n=0;f.read((char*)&n,sizeof(n));for(size_t i=0;i<n;++i){std::vector<char> name;vec(f,name);size_t m=0;f.read((char*)&m,sizeof(m));std::vector<std::vector<int>> ls(m);for(auto& l:ls)vec(f,l);memberships.emplace_back(std::string(name.begin(),name.end()),std::move(ls));}}
struct Candidate {int id; Placement from,raw; WindowMemo memo; Memberships member;};
inline std::vector<Candidate> candidates;
inline void capture(const Placement& from,const Placement& raw,const WindowMemo& memo) {candidates.push_back({current.id,from,raw,memo,memberships});}
inline void begin(const PlaceDB& db,const Placement& p,double D,int round,int seed,const char* kind) {
 memberships.clear();current=Attempt();current.id=int(attempts.size())+1;current.round=round;current.seed=seed;current.kind=kind;current.D=D;
 auto b=displacement(db,p);current.startMax=b.max;current.startTotal=b.total;
}
inline void finish(const PlaceDB& db,const Placement& p,bool ok,bool accept,int how) {
 current.ok=ok;current.accept=accept;current.how=how;
 if(ok){auto d=displacement(db,p);current.postTotal=d.total;current.postMax=d.max;}
 attempts.push_back(current);
}
inline void flush(const PlaceDB& db) {
 if(prefix().empty())return;std::ofstream f(prefix()+"_attempts.csv");f<<std::setprecision(15);
 f<<"id,round,kind,seed,D,start_max,start_total,row_s,site_s,dp_s,raw_total,raw_max,post_total,post_max,feasible,accepted,method,total_limit,reason\n";
 for(auto&a:attempts)f<<a.id<<','<<a.round<<','<<a.kind<<','<<a.seed<<','<<a.D<<','<<a.startMax<<','<<a.startTotal<<','<<a.rowSec<<','<<a.siteSec<<','<<a.dpSec<<','<<a.rawTotal<<','<<a.rawMax<<','<<a.postTotal<<','<<a.postMax<<','<<a.ok<<','<<a.accept<<','<<a.how<<','<<a.limit<<','<<(!a.ok?"unresolved":a.kind=="probe"?"probe_feasible":a.accept?"accepted":"total_budget")<<'\n';
 for(auto& c:candidates){auto name=prefix()+"_attempt"+std::to_string(c.id);writePl(db,c.from,name+"_from.pl");writePl(db,c.raw,name+"_raw.pl");saveMemo(c.memo,name+".memo");saveMembers(c.member,name+".members");}
}
inline void member(const char* stage,const std::vector<std::vector<int>>& lists) {if(detail||current.kind=="full")memberships.emplace_back(stage,lists);}
inline long solvedWindows=0;
inline bool localEnabled=false;
inline std::vector<char> localMask;
inline void cells(const PlaceDB& db,const Placement& base,const Placement& p,double D,const std::string& path) {
 std::ofstream f(path);f<<std::setprecision(15)<<"cell,violator,old_segment,new_segment,old_site,new_site,old_disp,new_disp,delta_disp,travel,dx,dy\n";
 for(int v:db.movable){double a=displacementOf(db,base,v),b=displacementOf(db,p,v);double dx=p.x(db,v)-base.x(db,v),dy=p.y(db,v)-base.y(db,v);
 if(dx||dy||a>D+1e-9)f<<db.nodes[v].name<<','<<(a>D+1e-9)<<','<<base.seg[v]<<','<<p.seg[v]<<','<<base.site[v]<<','<<p.site[v]<<','<<a<<','<<b<<','<<b-a<<','<<std::abs(dx)+std::abs(dy)<<','<<dx<<','<<dy<<'\n';}
}
}
void diagnosticMaterialize(const PlaceDB&,const Placement&,double,const std::string&);
